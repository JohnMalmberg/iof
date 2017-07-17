/* Copyright (C) 2017 Intel Corporation
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
#include "log.h"
#include "ios_gah.h"
#include "intercept.h"

/*
 * This will be defined by the calling function to select
 * the correct RPC type from the protocol registry.
 * This is used in the FS_TO_OP Macro below.
 */
#ifndef IOF_PROTO_CLASS
#define IOF_PROTO_CLASS DEFAULT
#endif

/*
 * Helpers for forcing macro expansion.
 */
#define EVAL_PROTO_CLASS(CLS) DEF_PROTO_CLASS(CLS)
#define EVAL_RPC_TYPE(CLS, TYPE) DEF_RPC_TYPE(CLS, TYPE)
/*
 * Returns the correct RPC Type ID from the protocol registry.
 */
#define FS_TO_OP(HANDLE, FN) \
		((&iof_protocol_registry[EVAL_PROTO_CLASS(IOF_PROTO_CLASS)])\
		  ->rpc_types[EVAL_RPC_TYPE(IOF_PROTO_CLASS, FN)].op_id)

struct read_cb_r {
	struct iof_data_out *out;
	struct iof_file_common *f_info;
	crt_rpc_t *rpc;
	struct iof_tracker tracker;
	int err;
	int rc;
};

struct read_bulk_cb_r {
	struct iof_read_bulk_out *out;
	struct iof_file_common *f_info;
	crt_rpc_t *rpc;
	struct iof_tracker tracker;
	int err;
	int rc;
};

#define BULK_THRESHOLD 64

static void
read_cb(const struct crt_cb_info *cb_info)
{
	struct read_cb_r *reply = cb_info->cci_arg;
	struct iof_data_out *out = crt_reply_get(cb_info->cci_rpc);
	int rc;

	if (cb_info->cci_rc != 0) {
		/*
		 * Error handling.  On timeout return EAGAIN, all other errors
		 * return EIO.
		 *
		 * TODO: Handle target eviction here
		 */
		IOF_LOG_INFO("Bad RPC reply %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -CER_TIMEDOUT)
			reply->err = EAGAIN;
		else
			reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	if (out->err) {
		IOF_LOG_ERROR("Error from target %d", out->err);

		if (out->err == IOF_GAH_INVALID)
			reply->f_info->gah_valid = 0;

		reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	if (out->rc) {
		reply->rc = out->rc;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	rc = crt_req_addref(cb_info->cci_rpc);
	if (rc) {
		IOF_LOG_ERROR("could not take reference on query RPC, rc = %d",
			      rc);
		reply->err = EIO;
	} else {
		reply->out = out;
		reply->rpc = cb_info->cci_rpc;
	}

	iof_tracker_signal(&reply->tracker);
}

static void
read_bulk_cb(const struct crt_cb_info *cb_info)
{
	struct read_bulk_cb_r *reply = cb_info->cci_arg;
	struct iof_read_bulk_out *out = crt_reply_get(cb_info->cci_rpc);
	int rc;

	if (cb_info->cci_rc != 0) {
		/*
		 * Error handling.  On timeout return EAGAIN, all other errors
		 * return EIO.
		 *
		 * TODO: Handle target eviction here
		 */
		IOF_LOG_INFO("Bad RPC reply %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -CER_TIMEDOUT)
			reply->err = EAGAIN;
		else
			reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	if (out->err) {
		IOF_LOG_ERROR("Error from target %d", out->err);

		if (out->err == IOF_GAH_INVALID)
			reply->f_info->gah_valid = 0;

		if (out->err == IOF_ERR_NOMEM)
			reply->err = ENOMEM;
		else
			reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	if (out->rc) {
		reply->rc = out->rc;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	rc = crt_req_addref(cb_info->cci_rpc);
	if (rc) {
		IOF_LOG_ERROR("could not take reference on query RPC, rc = %d",
			      rc);
		reply->err = EIO;
	} else {
		reply->out = out;
		reply->rpc = cb_info->cci_rpc;
	}

	iof_tracker_signal(&reply->tracker);
}

static int read_direct(char *buff, size_t len, off_t position,
		     struct iof_file_common *f_info, int *errcode)
{
	struct iof_projection *fs_handle;
	struct iof_service_group *grp;
	struct iof_read_in *in;
	struct read_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	int rc;

	fs_handle = f_info->projection;
	grp = fs_handle->grp;

	rc = crt_req_create(fs_handle->crt_ctx, grp->psr_ep,
			    FS_TO_OP(fs_handle, read), &rpc);
	if (rc || !rpc) {
		IOF_LOG_ERROR("Could not create request, rc = %u",
			      rc);
		*errcode = EIO;
		return -1;
	}

	iof_tracker_init(&reply.tracker, 1);
	in = crt_req_get(rpc);
	in->gah = f_info->gah;
	in->base = position;
	in->len = len;

	reply.f_info = f_info;

	rc = crt_req_send(rpc, read_cb, &reply);
	if (rc) {
		IOF_LOG_ERROR("Could not send open rpc, rc = %u", rc);
		*errcode = EIO;
		return -1;
	}
	iof_fs_wait(fs_handle, &reply.tracker);

	if (reply.err) {
		*errcode = reply.err;
		return -1;
	}

	if (reply.rc != 0) {
		*errcode = reply.rc;
		return -1;
	}

	len = reply.out->data.iov_len;

	if (len > 0)
		memcpy(buff, reply.out->data.iov_buf, reply.out->data.iov_len);

	rc = crt_req_decref(reply.rpc);
	if (rc)
		IOF_LOG_ERROR("decref returned %d", rc);

	return len;
}

static ssize_t read_bulk(char *buff, size_t len, off_t position,
			 struct iof_file_common *f_info, int *errcode)
{
	struct iof_projection *fs_handle;
	struct iof_service_group *grp;
	struct iof_read_bulk_in *in;
	struct read_bulk_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	crt_bulk_t bulk;
	crt_sg_list_t sgl = {0};
	crt_iov_t iov = {0};
	int rc;

	fs_handle = f_info->projection;
	grp = fs_handle->grp;

	rc = crt_req_create(fs_handle->crt_ctx, grp->psr_ep,
			    FS_TO_OP(f_info, read_bulk), &rpc);
	if (rc || !rpc) {
		IOF_LOG_ERROR("Could not create request, rc = %u",
			      rc);
		*errcode = EIO;
		return -1;
	}

	in = crt_req_get(rpc);
	in->gah = f_info->gah;
	in->base = position;
	in->len = len;

	iov.iov_len = len;
	iov.iov_buf_len = len;
	iov.iov_buf = (void *)buff;
	sgl.sg_iovs = &iov;
	sgl.sg_nr.num = 1;

	rc = crt_bulk_create(fs_handle->crt_ctx, &sgl, CRT_BULK_RW,
			     &in->bulk);
	if (rc) {
		IOF_LOG_ERROR("Failed to make local bulk handle %d", rc);
		*errcode = EIO;
		return -1;
	}

	iof_tracker_init(&reply.tracker, 1);
	bulk = in->bulk;

	reply.f_info = f_info;

	rc = crt_req_send(rpc, read_bulk_cb, &reply);
	if (rc) {
		IOF_LOG_ERROR("Could not send open rpc, rc = %u", rc);
		*errcode = EIO;
		return -1;
	}
	iof_fs_wait(fs_handle, &reply.tracker);

	if (reply.err) {
		*errcode = reply.err;
		return -1;
	}

	if (reply.rc != 0) {
		*errcode = reply.rc;
		return -1;
	}

	if (reply.out->iov_len > 0) {
		if (reply.out->data.iov_len != reply.out->iov_len) {
			IOF_LOG_ERROR("Missing IOV %d", reply.out->iov_len);
			*errcode = EIO;
			return -1;
		}
		len = reply.out->data.iov_len;
		memcpy(buff, reply.out->data.iov_buf, len);
	} else {
		len = reply.out->bulk_len;
		IOF_LOG_INFO("Received %#zx via bulk", len);
	}

	rc = crt_req_decref(reply.rpc);
	if (rc)
		IOF_LOG_ERROR("decref returned %d", rc);

	rc = crt_bulk_free(bulk);
	if (rc) {
		*errcode = EIO;
		return -1;
	}

	IOF_LOG_INFO("Read complete %#zx", len);

	return len;
}

ssize_t ioil_do_pread(char *buff, size_t len, off_t position,
		      struct iof_file_common *f_info, int *errcode)
{
	IOF_LOG_INFO("%#zx-%#zx " GAH_PRINT_STR, position, position + len - 1,
		     GAH_PRINT_VAL(f_info->gah));

	if (len >= BULK_THRESHOLD)
		return read_bulk(buff, len, position, f_info, errcode);
	else
		return read_direct(buff, len, position, f_info, errcode);
}

/* TODO: This could be optimized to send multiple RPCs at once rather than
 * sending them serially.   Get it working first.
 */
ssize_t ioil_do_preadv(const struct iovec *iov, int count, off_t position,
		       struct iof_file_common *f_info, int *errcode)
{
	ssize_t bytes_read;
	ssize_t total_read = 0;
	int i;

	for (i = 0; i < count; i++) {
		if (iov[i].iov_len >= BULK_THRESHOLD)
			bytes_read = read_bulk(iov[i].iov_base, iov[i].iov_len,
					 position, f_info, errcode);
		else
			bytes_read = read_direct(iov[i].iov_base,
						 iov[i].iov_len,
						 position, f_info, errcode);

		if (bytes_read == -1)
			return (ssize_t)-1;

		if (bytes_read == 0)
			return total_read;

		position += bytes_read;
		total_read += bytes_read;
	}

	return total_read;
}
