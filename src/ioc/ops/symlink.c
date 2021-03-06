/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "iof_common.h"
#include "ioc.h"
#include "log.h"

#define REQ_NAME request
#define POOL_NAME symlink_pool
#define TYPE_NAME entry_req
#define RESTOCK_ON_SEND
#include "ioc_ops.h"

static const struct ioc_request_api api = {
	.on_send	= post_send,
	.on_result	= iof_entry_cb,
	.on_evict	= ioc_simple_resend
};

#define STAT_KEY symlink

void
ioc_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
	       const char *name)
{
	struct iof_projection_info	*fs_handle = fuse_req_userdata(req);
	struct entry_req		*desc = NULL;
	struct iof_two_string_in	*in;
	int rc;

	IOF_TRACE_INFO(req, "Parent:%lu '%s'", parent, name);
	IOC_REQ_INIT_LL(desc, fs_handle, api, in, req, rc);
	if (rc)
		D_GOTO(err, rc);

	strncpy(in->common.name.name, name, NAME_MAX);
	desc->dest = strdup(link);
	if (!desc->dest)
		D_GOTO(err, rc = ENOMEM);
	in->oldpath = desc->dest;

	desc->pool = fs_handle->symlink_pool;
	strncpy(desc->ie->name, name, NAME_MAX);
	desc->ie->parent = parent;

	/* Find the GAH of the parent */
	rc = find_gah_ref(fs_handle, parent, &in->common.gah);
	if (rc != 0)
		D_GOTO(err, 0);

	rc = iof_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	IOF_FUSE_REPLY_ERR(req, rc);
	if (desc)
		iof_pool_release(fs_handle->symlink_pool, desc);
}
