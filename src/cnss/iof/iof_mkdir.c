/* Copyright (C) 2016-2017 Intel Corporation
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

#ifdef IOF_USE_FUSE3
#include <fuse3/fuse.h>
#else
#include <fuse/fuse.h>
#endif

#include "iof_common.h"
#include "iof.h"
#include "log.h"

int ioc_mkdir(const char *file, mode_t mode)
{
	struct fs_handle *fs_handle = ioc_get_handle();
	struct iof_create_in *in;
	struct status_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	int rc;

	IOF_LOG_INFO("dir %s mode 0%o", file, (uint32_t)mode);

	STAT_ADD(fs_handle->stats, mkdir);

	if (FS_IS_OFFLINE(fs_handle))
		return -fs_handle->offline_reason;

	if (!IOF_IS_WRITEABLE(fs_handle->flags)) {
		IOF_LOG_INFO("Attempt to modify Read-Only File System");
		return -EROFS;
	}

	rc = crt_req_create(fs_handle->crt_ctx, fs_handle->dest_ep,
			    FS_TO_OP(fs_handle, mkdir), &rpc);
	if (rc || !rpc) {
		IOF_LOG_ERROR("Could not create request, rc = %u",
			      rc);
		return -EIO;
	}

	in = crt_req_get(rpc);
	in->path = (crt_string_t)file;
	in->mode = mode;
	in->fs_id = fs_handle->fs_id;

	rc = crt_req_send(rpc, ioc_status_cb, &reply);
	if (rc) {
		IOF_LOG_ERROR("Could not send rpc, rc = %u", rc);
		return -EIO;
	}

	rc = ioc_cb_progress(fs_handle, &reply.complete);
	if (rc)
		return -rc;

	IOF_LOG_DEBUG("path %s rc %d", file, IOC_STATUS_TO_RC(reply));

	return IOC_STATUS_TO_RC(reply);
}

