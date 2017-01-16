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

#include "iof_common.h"

/*input/output format for RPC's*/

/*
 * Re-use the CMF_UUID type when using a GAH as they are both 128 bit types
 * but define CMF_GAH here so it's clearer in the code what is happening.
 */
#define CMF_GAH CMF_UUID

struct crt_msg_field *string_in[] = {
	&CMF_STRING,
	&CMF_UINT64
};

struct crt_msg_field *create_in[] = {
	&CMF_STRING,
	&CMF_UINT64,
	&CMF_INT,
};

struct crt_msg_field *iov_pair[] = {
	&CMF_IOVEC,
	&CMF_INT,
	&CMF_INT
};

struct crt_msg_field *gah_pair[] = {
	&CMF_GAH,
	&CMF_INT,
	&CMF_INT
};

struct crt_msg_field *readdir_in[] = {
	&CMF_GAH,
	&CMF_UINT64,
	&CMF_INT,
};

struct crt_msg_field *readdir_out[] = {
	&CMF_IOVEC,
	&CMF_INT,
};

struct crt_msg_field *psr_out[] = {
	&CMF_IOVEC
};

struct crt_msg_field *closedir_in[] = {
	&CMF_GAH,
	&CMF_UINT64
};

struct crt_msg_field *read_in[] = {
	&CMF_GAH,
	&CMF_UINT64,
	&CMF_UINT64,
};

struct crt_msg_field *status_out[] = {
	&CMF_INT,
	&CMF_INT,
};

/*query RPC format*/
struct crt_req_format QUERY_RPC_FMT = DEFINE_CRT_REQ_FMT("psr_query",
							 NULL,
							 psr_out);

struct crt_req_format READDIR_FMT = DEFINE_CRT_REQ_FMT("readdir",
						       readdir_in,
						       readdir_out);

struct crt_req_format CLOSEDIR_FMT = DEFINE_CRT_REQ_FMT("closedir",
							closedir_in,
							NULL);

struct crt_req_format OPEN_FMT = DEFINE_CRT_REQ_FMT("open",
						string_in,
						gah_pair);

struct crt_req_format CLOSE_FMT = DEFINE_CRT_REQ_FMT("close",
						closedir_in,
						NULL);

struct crt_req_format CREATE_FMT = DEFINE_CRT_REQ_FMT("create",
						create_in,
						gah_pair);

struct crt_req_format READ_FMT = DEFINE_CRT_REQ_FMT("read",
						    read_in,
						    iov_pair);

struct crt_req_format MKDIR_FMT = DEFINE_CRT_REQ_FMT("mkdir",
						     create_in,
						     status_out);

#define RPC_TYPE(NAME, in, out) .NAME = { .fmt = \
					  DEFINE_CRT_REQ_FMT(#NAME, in, out) }
static struct proto proto = {
	.name = "IOF",
	.id_base = 0x10F00,
	.mt = {
		RPC_TYPE(getattr, string_in, iov_pair),
		RPC_TYPE(opendir, string_in, gah_pair),
	},
};

struct proto *iof_register()
{
	return &proto;
}

int iof_proto_commit(struct proto *proto)
{
	int i;
	int rpc_count = sizeof(struct my_types) / sizeof(struct rpc_data);
	struct rpc_data *rp = (struct rpc_data *)&proto->mt;

	for (i = 0 ; i < rpc_count ; i++) {
		int rc;

		rp->op_id = proto->id_base + i;
		if (rp->fn)
			rc = crt_rpc_srv_register(rp->op_id, &rp->fmt, rp->fn);
		else
			rc = crt_rpc_register(rp->op_id, &rp->fmt);
		if (rc != 0)
			printf("Failed to register");
		rp++;
	}

	return 0;
}

