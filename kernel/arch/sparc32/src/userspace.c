/*
 * Copyright (c) 2010 Martin Decky
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

/** @addtogroup abs32le
 * @{
 */
/** @file
 */

#include <userspace.h>
#include <typedefs.h>
#include <arch.h>
#include <arch/asm.h>
#include <abi/proc/uarg.h>
#include <mm/as.h>

void userspace(uspace_arg_t *kernel_uarg)
{
	printf("userspace(): entry=%p, stack=%p, stacksize=%d\n", kernel_uarg->uspace_entry, kernel_uarg->uspace_stack, kernel_uarg->uspace_stack_size);
	/* On real hardware this switches the CPU to user
	   space mode and jumps to kernel_uarg->uspace_entry. */

	uint32_t psr = psr_read();

	psr &= ~(1 << 7);
	psr &= ~(1 << 6);

	asm volatile (
		"mov %[stack], %%sp\n"
		"mov %[psr], %%psr\n"
		"nop\n"
		"jmp %[entry]\n"
		"nop\n" :: [entry] "r" (kernel_uarg->uspace_entry),
			   [psr] "r" (psr),
			   [stack] "r" (kernel_uarg->uspace_stack + kernel_uarg->uspace_stack_size));

	while (true);
}

/** @}
 */
