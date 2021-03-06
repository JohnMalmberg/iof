/* Copyright (C) 2017-2018 Intel Corporation
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

#include <fuse3/fuse.h>
#include "iof_common.h"
#include "ioc.h"
#include "log.h"

#define REQ_NAME request
#define POOL_NAME lookup_pool
#define TYPE_NAME entry_req
#define RESTOCK_ON_SEND
#include "ioc_ops.h"

void iof_entry_cb(struct ioc_request *request)
{
	struct entry_req		*desc = container_of(request, struct entry_req, request);
	struct iof_projection_info	*fs_handle = desc->request.fsh;
	struct iof_entry_out		*out = crt_reply_get(request->rpc);
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(out, 0);

	entry.attr = out->stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;

	desc->ie->gah = out->gah;
	desc->ie->stat = out->stat;
	D_INIT_LIST_HEAD(&desc->ie->ie_fh_list);
	D_INIT_LIST_HEAD(&desc->ie->ie_ie_children);
	D_INIT_LIST_HEAD(&desc->ie->ie_ie_list);
	H_GAH_SET_VALID(desc->ie);
	rlink = d_hash_rec_find_insert(&fs_handle->inode_ht,
				       &desc->ie->stat.st_ino,
				       sizeof(desc->ie->stat.st_ino),
				       &desc->ie->ie_htl);

	if (rlink == &desc->ie->ie_htl) {
		desc->ie = NULL;
		IOF_TRACE_INFO(request->req, "New file %lu " GAH_PRINT_STR,
			       entry.ino, GAH_PRINT_VAL(out->gah));
	} else {
		IOF_TRACE_INFO(request->req,
			       "Existing file rlink %p %lu " GAH_PRINT_STR,
			       rlink, entry.ino, GAH_PRINT_VAL(out->gah));
		drop_ino_ref(fs_handle, desc->ie->parent);
		ie_close(fs_handle, desc->ie);
	}
out:
	if (request->rc) {
		drop_ino_ref(fs_handle, desc->ie->parent);
		IOF_FUSE_REPLY_ERR(request->req, request->rc);
	} else {
		IOF_FUSE_REPLY_ENTRY(request->req, entry);
	}
	iof_pool_release(desc->pool, desc);
}

static int
lookup_presend(struct ioc_request *request)
{
	struct iof_gah_string_in *in = crt_req_get(request->rpc);
	int rc = 0;

	IOF_TRACE_DEBUG(request, "loading gah from %d %p", request->ir_ht,
			request->ir_inode);

	D_MUTEX_LOCK(&request->fsh->gah_lock);

	if (request->ir_ht == RHS_ROOT) {
		in->gah = request->fsh->gah;
	} else {
		D_ASSERT(request->ir_ht == RHS_INODE);
		if (!H_GAH_IS_VALID(request->ir_inode))
			D_GOTO(out, rc = EHOSTDOWN);

		in->gah = request->ir_inode->gah;
	}

	IOF_TRACE_DEBUG(request, GAH_PRINT_STR, GAH_PRINT_VAL(in->gah));

out:
	D_MUTEX_UNLOCK(&request->fsh->gah_lock);

	return rc;
}

static const struct ioc_request_api api = {
	.on_send	= post_send,
	.on_result	= iof_entry_cb,
	.on_evict	= ioc_simple_resend,
	.on_presend	= lookup_presend,
};

#define STAT_KEY lookup

void
ioc_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct iof_projection_info	*fs_handle = fuse_req_userdata(req);
	struct TYPE_NAME		*desc = NULL;
	struct iof_gah_string_in	*in;
	int rc;

	IOF_TRACE_INFO(req, "Parent:%lu '%s'", parent, name);
	IOC_REQ_INIT_LL(desc, fs_handle, api, in, req, rc);
	if (rc)
		D_GOTO(err, rc);

	IOF_TRACE_INFO(req, "ie %p", &desc->ie);

	if (parent == 1) {
		desc->request.ir_ht = RHS_ROOT;
	} else {
		rc = find_inode(fs_handle, parent, &desc->request.ir_inode);

		if (rc != 0)
			D_GOTO(err, 0);

		desc->request.ir_ht = RHS_INODE;
	}

	strncpy(in->name.name, name, NAME_MAX);
	strncpy(desc->ie->name, name, NAME_MAX);
	desc->ie->parent = parent;
	desc->pool = fs_handle->lookup_pool;

	rc = iof_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	if (desc)
		iof_pool_release(fs_handle->lookup_pool, desc);
	drop_ino_ref(fs_handle, parent);
	IOF_FUSE_REPLY_ERR(req, rc);
}
