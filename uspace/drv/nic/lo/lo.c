/*
 * Copyright (c) 2011 Radim Vansa
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

/**
 * @addtogroup drv_lo
 * @brief Loopback virtual device driver
 * @{
 */
/**
 * @file
 */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <async.h>
#include <nic.h>

#define NAME  "lo"

static nic_address_t lo_addr = {
	.address = {0, 0, 0, 0, 0, 0}
};

static ddf_dev_ops_t lo_dev_ops;

static nic_device_info_t lo_info = {
	.vendor_name = "HelenOS",
	.model_name = "loopback",
	.part_number = "N/A (virtual device)",
	.serial_number = "N/A (virtual device)"
};

static void lo_send_frame(nic_t *nic_data, void *data, size_t size)
{
	nic_report_send_ok(nic_data, 1, size);
	nic_received_noneth_frame(nic_data, data, size);
}

static int lo_set_address(ddf_fun_t *fun, const nic_address_t *address)
{
	printf("%s: Set loopback HW to " PRIMAC "\n", NAME,
	    ARGSMAC(address->address));
	return ENOTSUP;
}

static int lo_get_device_info(ddf_fun_t *fun, nic_device_info_t *info)
{
	assert(info);
	memcpy(info, &lo_info, sizeof(nic_device_info_t));
	return EOK;
}

static int lo_dev_add(ddf_dev_t *dev)
{
	ddf_fun_t *fun = NULL;
	bool bound = false;
	
	nic_t *nic = nic_create_and_bind(dev);
	if (nic == NULL) {
		printf("%s: Failed to initialize\n", NAME);
		return ENOMEM;
	}
	
	dev->driver_data = nic;
	nic_set_send_frame_handler(nic, lo_send_frame);
	
	int rc = nic_connect_to_services(nic);
	if (rc != EOK) {
		printf("%s: Failed to connect to services\n", NAME);
		goto error;
	}
	
	fun = ddf_fun_create(nic_get_ddf_dev(nic), fun_exposed, "port0");
	if (fun == NULL) {
		printf("%s: Failed creating function\n", NAME);
		rc = ENOMEM;
		goto error;
	}
	nic_set_ddf_fun(nic, fun);
	fun->ops = &lo_dev_ops;
	fun->driver_data = nic;
	
	rc = nic_report_address(nic, &lo_addr);
	if (rc != EOK) {
		printf("%s: Failed to setup loopback address\n", NAME);
		goto error;
	}
	
	rc = ddf_fun_bind(fun);
	if (rc != EOK) {
		printf("%s: Failed binding function\n", NAME);
		goto error;
	}
	bound = true;
	
	rc = ddf_fun_add_to_category(fun, DEVICE_CATEGORY_NIC);
	if (rc != EOK)
		goto error;
	
	printf("%s: Adding loopback device '%s'\n", NAME, dev->name);
	return EOK;
error:
	if (bound)
		ddf_fun_unbind(fun);
	if (fun != NULL)
		ddf_fun_destroy(fun);
	
	nic_unbind_and_destroy(dev);
	return rc;
}

static nic_iface_t lo_nic_iface;

static driver_ops_t lo_driver_ops = {
	.dev_add = lo_dev_add,
};

static driver_t lo_driver = {
	.name = NAME,
	.driver_ops = &lo_driver_ops
};

int main(int argc, char *argv[])
{
	nic_driver_init(NAME);
	nic_driver_implement(&lo_driver_ops, &lo_dev_ops, &lo_nic_iface);
	lo_nic_iface.set_address = lo_set_address;
	lo_nic_iface.get_device_info = lo_get_device_info;
	
	return ddf_driver_main(&lo_driver);
}

/** @}
 */