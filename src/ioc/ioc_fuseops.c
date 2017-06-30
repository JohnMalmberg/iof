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

#include <stdio.h>
#include <string.h>
#ifdef IOF_USE_FUSE3
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>
#else
#include <fuse/fuse.h>
#include <fuse/fuse_lowlevel.h>
#endif

#include "iof_common.h"
#include "ioc.h"
#include "log.h"

/* Try to make the function pointer type as generic as possible */
typedef int (*fuse_func_t)(void *, ...);

#define DEF_FUSE_OP(TYPE) op_type_##TYPE
enum op_type {
	DEF_FUSE_OP(init),
	DEF_FUSE_OP(getattr),
	DEF_FUSE_OP(chmod),
	DEF_FUSE_OP(truncate),
	DEF_FUSE_OP(rename),
	DEF_FUSE_OP(utimens),
	DEF_FUSE_OP(opendir),
	DEF_FUSE_OP(readdir),
	DEF_FUSE_OP(releasedir),
	DEF_FUSE_OP(setxattr),
	DEF_FUSE_OP(open),
	DEF_FUSE_OP(release),
	DEF_FUSE_OP(create),
	DEF_FUSE_OP(fsync),
	DEF_FUSE_OP(read_buf),
	DEF_FUSE_OP(symlink),
	DEF_FUSE_OP(mkdir),
	DEF_FUSE_OP(rmdir),
	DEF_FUSE_OP(write),
	DEF_FUSE_OP(unlink),
	DEF_FUSE_OP(readlink),
	DEF_FUSE_OP(ioctl),
	DEF_FUSE_OP(statfs),
	OP_TYPES
};

struct operation {
	enum op_type op_type;
	fuse_func_t fn_ptr;
};

struct fuse_impl {
	int count;
	struct operation *ops;
};

static int string_to_bool(const char *str, int *value)
{
	if (strcmp(str, "1") == 0) {
		*value = 1;
		return 1;
	} else if (strcmp(str, "0") == 0) {
		*value = 0;
		return 1;
	}
	return 0;
}

/*
 * Temporary way of shutting down. This fuse callback is currently not going to
 * IONSS
 */
#ifdef __APPLE__

static int ioc_setxattr(const char *path,  const char *name, const char *value,
			size_t size, int options, uint32_t position)
#else
static int ioc_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
#endif
{
	struct fuse_context *context;
	int ret;

	context = fuse_get_context();
	if (strcmp(name, "user.exit") == 0) {
		if (string_to_bool(value, &ret)) {
			if (ret) {
				IOF_LOG_INFO("Exiting fuse loop");
				fuse_session_exit(
					fuse_get_session(context->fuse));
				return -EINTR;
			} else
				return 0;
		}
		return -EINVAL;
	}
	return -ENOTSUP;
}

#define SHOW_FLAG(FLAGS, FLAG) do {					\
		if (FLAGS & FLAG)					\
			IOF_LOG_INFO("Flag " #FLAG " enabled");		\
		else							\
			IOF_LOG_INFO("Flag " #FLAG " disable");		\
		FLAGS &= ~FLAG;						\
	} while (0)

static void ioc_show_flags(unsigned in)
{
	IOF_LOG_INFO("Flags are 0x%x", in);
	SHOW_FLAG(in, FUSE_CAP_ASYNC_READ);
	SHOW_FLAG(in, FUSE_CAP_POSIX_LOCKS);
	SHOW_FLAG(in, FUSE_CAP_ATOMIC_O_TRUNC);
	SHOW_FLAG(in, FUSE_CAP_EXPORT_SUPPORT);
	SHOW_FLAG(in, FUSE_CAP_DONT_MASK);
	SHOW_FLAG(in, FUSE_CAP_SPLICE_WRITE);
	SHOW_FLAG(in, FUSE_CAP_SPLICE_MOVE);
	SHOW_FLAG(in, FUSE_CAP_SPLICE_READ);
	SHOW_FLAG(in, FUSE_CAP_FLOCK_LOCKS);
	SHOW_FLAG(in, FUSE_CAP_IOCTL_DIR);
#if IOF_USE_FUSE3
	SHOW_FLAG(in, FUSE_CAP_AUTO_INVAL_DATA);
	SHOW_FLAG(in, FUSE_CAP_READDIRPLUS);
	SHOW_FLAG(in, FUSE_CAP_READDIRPLUS_AUTO);
	SHOW_FLAG(in, FUSE_CAP_ASYNC_DIO);
	SHOW_FLAG(in, FUSE_CAP_WRITEBACK_CACHE);
	SHOW_FLAG(in, FUSE_CAP_NO_OPEN_SUPPORT);
	SHOW_FLAG(in, FUSE_CAP_PARALLEL_DIROPS);
	SHOW_FLAG(in, FUSE_CAP_POSIX_ACL);
	SHOW_FLAG(in, FUSE_CAP_HANDLE_KILLPRIV);
#endif
#ifdef FUSE_CAP_BIG_WRITES
	SHOW_FLAG(in, FUSE_CAP_BIG_WRITES);
#endif

	if (in)
		IOF_LOG_ERROR("Unknown flags 0x%x", in);
}


/* Called on filesystem init.  It has the ability to both observe configuration
 * options, but also to modify them.  As we do not use the FUSE command line
 * parsing this is where we apply tunables.
 *
 * The return value is used to set private_data, however as we want
 * private_data to be set by the CNSS register routine we simply read it and
 * return the value here.
 */
static void *ioc_init(struct fuse_conn_info *conn)
{
	struct iof_projection_info *fs_handle;
	struct fuse_context *context;

	context = fuse_get_context();
	fs_handle = (struct iof_projection_info *)context->private_data;

	IOF_LOG_INFO("Fuse configuration for projection srv:%d cli:%d",
		     fs_handle->fs_id, fs_handle->proj.cli_fs_id);

	IOF_LOG_INFO("Proto %d %d", conn->proto_major, conn->proto_minor);
#if IOF_USE_FUSE3

	/* This value has to be set here to the same value passed to
	 * register_fuse().  Fuse always sets this value to zero so
	 * set it before reporting the value.
	 */
	conn->max_read = fs_handle->max_read;
	conn->max_write = fs_handle->max_write;

	IOF_LOG_INFO("max read 0x%x", conn->max_read);
#endif
	IOF_LOG_INFO("max write 0x%x", conn->max_write);
	IOF_LOG_INFO("readahead 0x%x", conn->max_readahead);

	IOF_LOG_INFO("Capability supported 0x%x ", conn->capable);

	ioc_show_flags(conn->capable);

#ifdef FUSE_CAP_BIG_WRITES
	conn->want |= FUSE_CAP_BIG_WRITES;
#endif

	IOF_LOG_INFO("Capability requested 0x%x", conn->want);

	ioc_show_flags(conn->want);

	return fs_handle;
}

#if IOF_USE_FUSE3

static void *ioc_init_full(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	void *handle = ioc_init(conn);
	/* Disable caching entirely */
	cfg->entry_timeout = 0;
	cfg->negative_timeout = 0;
	cfg->attr_timeout = 0;

	/* Use FUSE provided inode numbers to match the backing filesystem */
	cfg->use_ino = 1;

	/* Do not resolve the PATH for every operation, but let IOF access
	 * the information via the fh pointer instead
	 */
	cfg->nullpath_ok = 1;

	IOF_LOG_INFO("timeouts entry %f negative %f attr %f",
		     cfg->entry_timeout,
		     cfg->negative_timeout,
		     cfg->attr_timeout);

	IOF_LOG_INFO("max_background %d", conn->max_background);
	IOF_LOG_INFO("congestion_threshold %d", conn->congestion_threshold);

	IOF_LOG_INFO("use_ino %d", cfg->use_ino);

	IOF_LOG_INFO("nullpath_ok %d", cfg->nullpath_ok);

	IOF_LOG_INFO("direct_io %d", cfg->direct_io);

	return handle;
}
#endif

/*
 * We may have different FUSE operation implementations depending on the
 * features and type of projection (which is defined by 'flags'). The idea
 * here is to make the selection of operations dynamic and data-driven --
 * The 'fuse_operations' structure is populated dynamically at runtime by
 * selecting a combination of functions based on the flags supplied.
 *
 * Note: Read-only and Failover are not treated as separate modes, because
 * they do not require separate implementations. For read-only mode, the
 * function will merely check if the 'writeable' flag for the projection is
 * set and if not, will return an error. Similarly for failover, the function
 * will re-route the operation to a different IONSS rank in case of failure
 * and if the failover flag is set.
 *
 * As of now, we only have the default_ops table representing Private Access.
 * Default also means that we're agnostic to whether the projected file system
 * is local or parallel. If the projected file system is parallel and we want
 * failover features turned on, we simply need to set the failover flag.
 *
 * For striped metadata, we only need to override the metadata operations from
 * default_ops -- so we define a new table containing only those functions.
 *
 * For striped data, we only need to define a new table with data operations,
 * and set the striped metadata feature flag. This will ensure that functions
 * are selected from both the striped data and striped metadata tables.
 *
 * For striped metadata on Lustre, we define a table with Lustre specific
 * metadata operations, and set the striped data flag. This will select data
 * operations from the default striped data table, but metadata operations
 * from the Lustre-specific table.
 *
 * This can easily be extended to support DataWarp in scratch/cache modes.
 *
 * All these tables will be referenced in a master directory (below) called
 * 'fuse_impl_list', which will be indexed using bits[2-5] of 'flags';
 * this gives us a total of 16 entries. (First two bits represent read-only
 * and failover features, hence ignored).
 *
 * [0x0]:0000 = default operations
 * [0x1]:0001 = striped metadata (Generic PFS)
 * [0x2]:0010 = striped data (Generic PFS)
 * [0x3]:0011 = empty (includes striped data [0x2] and metadata [0x1]).
 * [0x4]:0100 = empty (Lustre; include everything from [0x0]).
 * [0x5]:0101 = Lustre-specific metadata operations (FID instead of inodes)
 * [0x6]:0110 = empty (Lustre; include [0x0] overridden by [0x2]).
 * [0x7]:0111 = empty (Lustre; combination of [0x2] and [0x5]).
 * [0x8]:1000 = DataWarp [Scratch]; private.
 * [0x9]:1001 = DataWarp [Scratch]; striped metadata (load balanced).
 * [0xA]:1010 = DataWarp [Scratch]; striped data.
 * [0xB]:1011 = empty (DataWarp [scratch] includes [0x9] and [0xA]).
 * [0xC]:1100 = DataWarp [Cache]; private.
 * [0xD]:1101 = DataWarp [Cache]; striped metadata (load balanced).
 * [0xE]:1110 = DataWarp [Cache]; striped data.
 * [0xF]:1111 = empty (DataWarp [cache]; includes [0xD] and [0xE]).
 *
 * We can also define and check for invalid modes, e.g. if striped data
 * always requires striped metadata to be turned on (but not vice versa),
 * we define 0010 as an unsupported combination of flags.
*/
#define DECL_FUSE_OP(TYPE, FN) \
		[DEF_FUSE_OP(TYPE)] = { \
			.op_type = DEF_FUSE_OP(TYPE), \
			.fn_ptr = (fuse_func_t) (FN) \
		}
static struct operation default_ops[] = {
#if IOF_USE_FUSE3
	DECL_FUSE_OP(init, ioc_init_full),
	DECL_FUSE_OP(getattr, ioc_getattr),
	DECL_FUSE_OP(chmod, ioc_chmod),
	DECL_FUSE_OP(truncate, ioc_truncate),
	DECL_FUSE_OP(rename, ioc_rename3),
	DECL_FUSE_OP(utimens, ioc_utimens),
#else
	DECL_FUSE_OP(init, ioc_init),
	DECL_FUSE_OP(getattr, ioc_getattr_name),
	DECL_FUSE_OP(chmod, ioc_chmod_name),
	DECL_FUSE_OP(truncate, ioc_truncate_name),
	DECL_FUSE_OP(rename, ioc_rename),
	DECL_FUSE_OP(utimens, ioc_utimens_name),
#endif
	DECL_FUSE_OP(opendir, ioc_opendir),
	DECL_FUSE_OP(readdir, ioc_readdir),
	DECL_FUSE_OP(releasedir, ioc_closedir),
	DECL_FUSE_OP(setxattr, ioc_setxattr),
	DECL_FUSE_OP(open, ioc_open),
	DECL_FUSE_OP(release, ioc_release),
	DECL_FUSE_OP(create, ioc_create),
	DECL_FUSE_OP(fsync, ioc_fsync),
	DECL_FUSE_OP(read_buf, ioc_read_buf),
	DECL_FUSE_OP(symlink, ioc_symlink),
	DECL_FUSE_OP(mkdir, ioc_mkdir),
	DECL_FUSE_OP(rmdir, ioc_rmdir),
	DECL_FUSE_OP(write, ioc_write),
	DECL_FUSE_OP(unlink, ioc_unlink),
	DECL_FUSE_OP(readlink, ioc_readlink),
	DECL_FUSE_OP(ioctl, ioc_ioctl),
	DECL_FUSE_OP(statfs, ioc_statfs),
};

/* Ignore the first two bits (writeable and failover) */
#define FLAGS_TO_MODE_INDEX(X) (((X) & 0x3F) >> 2)

#define DEF_FUSE_IMPL(X) \
	{ .count = (sizeof(X)/sizeof(*X)), .ops = X }

/* Only supporting default (Private mode) at the moment */
static uint8_t supported_impl[] = { 0x0 };

int iof_is_mode_supported(uint8_t flags)
{
	int i, count, mode = FLAGS_TO_MODE_INDEX(flags);

	IOF_LOG_INFO("Filesystem Access: %s",
		(flags & IOF_WRITEABLE ?
		"Read-Write" : "Read-Only"));

	count = sizeof(supported_impl) / sizeof(*supported_impl);
	for (i = 0; i < count; i++) {
		if (mode == supported_impl[i])
			return 1;
	}
	return 0;
}

struct fuse_operations *iof_get_fuse_ops(uint8_t flags)
{
	struct operation client_ops[OP_TYPES];
	struct fuse_operations *fuse_ops = calloc(1, sizeof(*fuse_ops));

	/* Temporary: Copy default_ops directly into client_ops. In future,
	 * client_ops will be constructed dynamically by selecting the
	 * correct implementations based on feature flags being set or unset.
	 */
	memcpy(client_ops, default_ops, sizeof(default_ops));

#define SET_FUSE_OP(FUSE, CLI, TYPE) \
	FUSE->TYPE = *(void **) &CLI[DEF_FUSE_OP(TYPE)].fn_ptr

	SET_FUSE_OP(fuse_ops, client_ops, init);
	SET_FUSE_OP(fuse_ops, client_ops, getattr);
	SET_FUSE_OP(fuse_ops, client_ops, chmod);
	SET_FUSE_OP(fuse_ops, client_ops, truncate);
	SET_FUSE_OP(fuse_ops, client_ops, rename);
	SET_FUSE_OP(fuse_ops, client_ops, utimens);
	SET_FUSE_OP(fuse_ops, client_ops, opendir);
	SET_FUSE_OP(fuse_ops, client_ops, readdir);
	SET_FUSE_OP(fuse_ops, client_ops, releasedir);
	SET_FUSE_OP(fuse_ops, client_ops, setxattr);
	SET_FUSE_OP(fuse_ops, client_ops, open);
	SET_FUSE_OP(fuse_ops, client_ops, release);
	SET_FUSE_OP(fuse_ops, client_ops, create);
	SET_FUSE_OP(fuse_ops, client_ops, fsync);
	SET_FUSE_OP(fuse_ops, client_ops, read_buf);
	SET_FUSE_OP(fuse_ops, client_ops, symlink);
	SET_FUSE_OP(fuse_ops, client_ops, mkdir);
	SET_FUSE_OP(fuse_ops, client_ops, rmdir);
	SET_FUSE_OP(fuse_ops, client_ops, write);
	SET_FUSE_OP(fuse_ops, client_ops, unlink);
	SET_FUSE_OP(fuse_ops, client_ops, readlink);
	SET_FUSE_OP(fuse_ops, client_ops, ioctl);
	SET_FUSE_OP(fuse_ops, client_ops, statfs);
#ifndef IOF_USE_FUSE3
#ifndef __APPLE__
	fuse_ops->flag_nullpath_ok = 1;
	fuse_ops->flag_nopath = 1;
#endif
#endif
	return fuse_ops;
}