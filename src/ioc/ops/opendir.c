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
#include "ios_gah.h"

#define REQ_NAME open_req
#define POOL_NAME dh_pool
#define TYPE_NAME iof_dir_handle
#define RESTOCK_ON_SEND
#include "ioc_ops.h"

#define STAT_KEY opendir

static void
opendir_ll_cb(struct ioc_request *request)
{
	struct TYPE_NAME	*dh = CONTAINER(request);
	struct iof_opendir_out	*out = crt_reply_get(request->rpc);
	struct fuse_file_info	fi = {0};

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc == 0) {
		dh->gah = out->gah;
		H_GAH_SET_VALID(dh);
		dh->handle_valid = 1;
		dh->ep = dh->open_req.fsh->proj.grp->psr_ep;
		D_MUTEX_LOCK(&dh->open_req.fsh->od_lock);
		d_list_add_tail(&dh->dh_od_list, &dh->open_req.fsh->opendir_list);
		D_MUTEX_UNLOCK(&dh->open_req.fsh->od_lock);
		fi.fh = (uint64_t)dh;
		IOF_FUSE_REPLY_OPEN(request->req, fi);
	} else {
		IOF_FUSE_REPLY_ERR(request->req, request->rc);
		iof_pool_release(dh->open_req.fsh->dh_pool, dh);
	}
}

static const struct ioc_request_api api = {
	.on_send	= post_send,
	.on_result	= opendir_ll_cb,
	.on_evict	= ioc_simple_resend
};

void ioc_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct iof_projection_info	*fs_handle = fuse_req_userdata(req);
	struct TYPE_NAME		*dh = NULL;
	struct iof_gah_in		*in;
	int rc;

	IOF_TRACE_INFO(req, "ino %lu", ino);
	IOC_REQ_INIT_LL(dh, fs_handle, api, in, req, rc);
	if (rc)
		D_GOTO(err, rc);

	/* Find the GAH of the parent */
	rc = find_gah(fs_handle, ino, &in->gah);
	if (rc != 0)
		D_GOTO(err, 0);

	dh->inode_no = ino;

	rc = iof_fs_send(&dh->open_req);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	if (dh)
		iof_pool_release(fs_handle->dh_pool, dh);

	IOF_FUSE_REPLY_ERR(req, rc);
}
