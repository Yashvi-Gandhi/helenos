/*
 * Copyright (C) 2006 Ondrej Palkovsky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Lock ordering
 *
 * First the answerbox, then the phone
 */

#include <synch/spinlock.h>
#include <synch/waitq.h>
#include <ipc/ipc.h>
#include <errno.h>
#include <mm/slab.h>
#include <arch.h>
#include <proc/task.h>
#include <memstr.h>
#include <debug.h>

#include <print.h>
#include <proc/thread.h>

/* Open channel that is assigned automatically to new tasks */
answerbox_t *ipc_phone_0 = NULL;

static slab_cache_t *ipc_call_slab;

/* Initialize new call */
static void _ipc_call_init(call_t *call)
{
	memsetb((__address)call, sizeof(*call), 0);
	call->callerbox = &TASK->answerbox;
	call->sender = TASK;
}

/** Allocate & initialize call structure
 * 
 * The call is initialized, so that the reply will be directed
 * to TASK->answerbox
 */
call_t * ipc_call_alloc(void)
{
	call_t *call;

	call = slab_alloc(ipc_call_slab, 0);
	_ipc_call_init(call);

	return call;
}

/** Initialize allocated call */
void ipc_call_static_init(call_t *call)
{
	_ipc_call_init(call);
	call->flags |= IPC_CALL_STATIC_ALLOC;
}

/** Deallocate call stracuture */
void ipc_call_free(call_t *call)
{
	slab_free(ipc_call_slab, call);
}

/** Initialize answerbox structure
 */
void ipc_answerbox_init(answerbox_t *box)
{
	spinlock_initialize(&box->lock, "ipc_box_lock");
	waitq_initialize(&box->wq);
	list_initialize(&box->connected_phones);
	list_initialize(&box->calls);
	list_initialize(&box->dispatched_calls);
	list_initialize(&box->answers);
	box->task = TASK;
}

/** Connect phone to answerbox */
void ipc_phone_connect(phone_t *phone, answerbox_t *box)
{
	spinlock_lock(&phone->lock);

	ASSERT(!phone->callee);
	phone->busy = IPC_BUSY_CONNECTED;
	phone->callee = box;

	spinlock_lock(&box->lock);
	list_append(&phone->list, &box->connected_phones);
	spinlock_unlock(&box->lock);

	spinlock_unlock(&phone->lock);
}

/** Initialize phone structure and connect phone to answerbox
 */
void ipc_phone_init(phone_t *phone)
{
	spinlock_initialize(&phone->lock, "phone_lock");
	phone->callee = NULL;
	phone->busy = IPC_BUSY_FREE;
	atomic_set(&phone->active_calls, 0);
}

/** Helper function to facilitate synchronous calls */
void ipc_call_sync(phone_t *phone, call_t *request)
{
	answerbox_t sync_box; 

	ipc_answerbox_init(&sync_box);

	/* We will receive data on special box */
	request->callerbox = &sync_box;

	ipc_call(phone, request);
	ipc_wait_for_call(&sync_box, 0);
}

/** Answer message that was not dispatched and is not entered in
 * any queue
 */
static void _ipc_answer_free_call(call_t *call)
{
	answerbox_t *callerbox = call->callerbox;

	call->flags |= IPC_CALL_ANSWERED;

	spinlock_lock(&callerbox->lock);
	list_append(&call->list, &callerbox->answers);
	spinlock_unlock(&callerbox->lock);
	waitq_wakeup(&callerbox->wq, 0);
}

/** Answer message, that is in callee queue
 *
 * @param box Answerbox that is answering the message
 * @param call Modified request that is being sent back
 */
void ipc_answer(answerbox_t *box, call_t *call)
{
	/* Remove from active box */
	spinlock_lock(&box->lock);
	list_remove(&call->list);
	spinlock_unlock(&box->lock);
	/* Send back answer */
	_ipc_answer_free_call(call);
}

/** Simulate sending back a message
 *
 * Most errors are better handled by forming a normal backward
 * message and sending it as a normal answer.
 */
void ipc_backsend_err(phone_t *phone, call_t *call, __native err)
{
	call->data.phone = phone;
	atomic_inc(&phone->active_calls);
	if (phone->busy == IPC_BUSY_CONNECTED)
		IPC_SET_RETVAL(call->data, EHANGUP);
	else
		IPC_SET_RETVAL(call->data, ENOENT);

	_ipc_answer_free_call(call);
}

/* Unsafe unchecking ipc_call */
static void _ipc_call(phone_t *phone, answerbox_t *box, call_t *call)
{
	if (! (call->flags & IPC_CALL_FORWARDED)) {
		atomic_inc(&phone->active_calls);
		call->data.phone = phone;
	}

	spinlock_lock(&box->lock);
	list_append(&call->list, &box->calls);
	spinlock_unlock(&box->lock);
	waitq_wakeup(&box->wq, 0);
}

/** Send a asynchronous request using phone to answerbox
 *
 * @param phone Phone connected to answerbox
 * @param request Request to be sent
 */
int ipc_call(phone_t *phone, call_t *call)
{
	answerbox_t *box;

	spinlock_lock(&phone->lock);

	box = phone->callee;
	if (!box) {
		/* Trying to send over disconnected phone */
		spinlock_unlock(&phone->lock);
		if (call->flags & IPC_CALL_FORWARDED) {
			IPC_SET_RETVAL(call->data, EFORWARD);
			_ipc_answer_free_call(call);
		} else { /* Simulate sending back a message */
			if (phone->busy == IPC_BUSY_CONNECTED)
				ipc_backsend_err(phone, call, EHANGUP);
			else
				ipc_backsend_err(phone, call, ENOENT);
		}

		return ENOENT;
	}
	_ipc_call(phone, box, call);
	
	spinlock_unlock(&phone->lock);
	return 0;
}

/** Disconnect phone from answerbox
 *
 * It is allowed to call disconnect on already disconnected phone
 *
 * @return 0 - phone disconnected, -1 - the phone was already disconnected
 */
int ipc_phone_hangup(phone_t *phone)
{
	answerbox_t *box;
	call_t *call;
	
	spinlock_lock(&phone->lock);
	box = phone->callee;
	if (!box) {
		if (phone->busy == IPC_BUSY_CONNECTING) {
			spinlock_unlock(&phone->lock);
			return -1;
		}
		/* Already disconnected phone */
		phone->busy = IPC_BUSY_FREE;
		spinlock_unlock(&phone->lock);
		return 0;
	}

	spinlock_lock(&box->lock);
	list_remove(&phone->list);
	phone->callee = NULL;
	spinlock_unlock(&box->lock);

	call = ipc_call_alloc();
	IPC_SET_METHOD(call->data, IPC_M_PHONE_HUNGUP);
	call->flags |= IPC_CALL_DISCARD_ANSWER;
	_ipc_call(phone, box, call);

	phone->busy = IPC_BUSY_FREE;

	spinlock_unlock(&phone->lock);

	return 0;
}

/** Forwards call from one answerbox to a new one
 *
 * @param request Request to be forwarded
 * @param newbox Target answerbox
 * @param oldbox Old answerbox
 * @return 0 on forward ok, error code, if there was error
 * 
 * - the return value serves only as an information for the forwarder,
 *   the original caller is notified automatically with EFORWARD
 */
int ipc_forward(call_t *call, phone_t *newphone, answerbox_t *oldbox)
{
	spinlock_lock(&oldbox->lock);
	list_remove(&call->list);
	spinlock_unlock(&oldbox->lock);

	return ipc_call(newphone, call);
}


/** Wait for phone call 
 *
 * @return Recived message address
 * - to distinguish between call and answer, look at call->flags
 */
call_t * ipc_wait_for_call(answerbox_t *box, int flags)
{
	call_t *request;

restart:      
	if (flags & IPC_WAIT_NONBLOCKING) {
		if (waitq_sleep_timeout(&box->wq,0,1) == ESYNCH_WOULD_BLOCK)
			return NULL;
	} else 
		waitq_sleep(&box->wq);
	
	spinlock_lock(&box->lock);
	if (!list_empty(&box->answers)) {
		/* Handle asynchronous answers */
		request = list_get_instance(box->answers.next, call_t, list);
		list_remove(&request->list);
		atomic_dec(&request->data.phone->active_calls);
	} else if (!list_empty(&box->calls)) {
		/* Handle requests */
		request = list_get_instance(box->calls.next, call_t, list);
		list_remove(&request->list);
		/* Append request to dispatch queue */
		list_append(&request->list, &box->dispatched_calls);
	} else {
		/* This can happen regularly after ipc_cleanup, remove
		 * the warning in the future when the IPC is
		 * more debugged */
		printf("WARNING: Spurious IPC wakeup.\n");
		spinlock_unlock(&box->lock);
		goto restart;
	}
	spinlock_unlock(&box->lock);
	return request;
}

/** Initilize ipc subsystem */
void ipc_init(void)
{
	ipc_call_slab = slab_cache_create("ipc_call",
					  sizeof(call_t),
					  0,
					  NULL, NULL, 0);
}

/** Answer all calls from list with EHANGUP msg */
static void ipc_cleanup_call_list(link_t *lst)
{
	call_t *call;

	while (!list_empty(lst)) {
		call = list_get_instance(lst->next, call_t, list);
		list_remove(&call->list);

		IPC_SET_RETVAL(call->data, EHANGUP);
		_ipc_answer_free_call(call);
	}
}

/** Cleans up all IPC communication of the given task
 *
 *
 */
void ipc_cleanup(task_t *task)
{
	int i;
	call_t *call;
	phone_t *phone;
	
	/* Disconnect all our phones ('ipc_phone_hangup') */
	for (i=0;i < IPC_MAX_PHONES; i++)
		ipc_phone_hangup(&task->phones[i]);

	/* Disconnect all phones connected to our answerbox */
restart_phones:
	spinlock_lock(&task->answerbox.lock);
	while (!list_empty(&task->answerbox.connected_phones)) {
		phone = list_get_instance(task->answerbox.connected_phones.next,
					  phone_t,
					  list);
		if (! spinlock_trylock(&phone->lock)) {
			spinlock_unlock(&task->answerbox.lock);
			goto restart_phones;
		}
		
		/* Disconnect phone */
		phone->callee = NULL;
		list_remove(&phone->list);

		spinlock_unlock(&phone->lock);
	}

	/* Answer all messages in 'calls' and 'dispatched_calls' queues */
	spinlock_lock(&task->answerbox.lock);
	ipc_cleanup_call_list(&task->answerbox.dispatched_calls);
	ipc_cleanup_call_list(&task->answerbox.calls);
	spinlock_unlock(&task->answerbox.lock);
	
	/* Wait for all async answers to arrive */
	while (atomic_get(&task->active_calls)) {
		call = ipc_wait_for_call(&task->answerbox, 0);
		ASSERT(call->flags & IPC_CALL_ANSWERED);
		ASSERT(! (call->flags & IPC_CALL_STATIC_ALLOC));
		
		atomic_dec(&task->active_calls);
		ipc_call_free(call);
	}
}
