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
#ifndef __IOF_H__
#define __IOF_H__

#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <gurt/list.h>
#include <gurt/hash.h>

#include "cnss_plugin.h"
#include "ios_gah.h"
#include "iof_atomic.h"
#include "iof_fs.h"
#include "iof_bulk.h"
#include "iof_pool.h"

struct iof_stats {
	ATOMIC unsigned int opendir;
	ATOMIC unsigned int readdir;
	ATOMIC unsigned int closedir;
	ATOMIC unsigned int getattr;
	ATOMIC unsigned int create;
	ATOMIC unsigned int readlink;
	ATOMIC unsigned int rmdir;
	ATOMIC unsigned int mkdir;
	ATOMIC unsigned int statfs;
	ATOMIC unsigned int unlink;
	ATOMIC unsigned int ioctl;
	ATOMIC unsigned int open;
	ATOMIC unsigned int release;
	ATOMIC unsigned int symlink;
	ATOMIC unsigned int rename;
	ATOMIC unsigned int read;
	ATOMIC unsigned int write;
	ATOMIC uint64_t read_bytes;
	ATOMIC uint64_t write_bytes;
	ATOMIC unsigned int il_ioctl;
	ATOMIC unsigned int fsync;
	ATOMIC unsigned int lookup;
	ATOMIC unsigned int forget;
	ATOMIC unsigned int setattr;
};

/* A common structure for holding a cart context and thread details */
struct iof_ctx {
	/* cart context */
	crt_context_t			crt_ctx;
	pthread_t			thread;
	struct iof_pool			*pool;
	struct iof_tracker		thread_start_tracker;
	struct iof_tracker		thread_stop_tracker;
	struct iof_tracker		thread_shutdown_tracker;
	uint32_t			poll_interval;
	crt_progress_cond_cb_t		callback_fn;
};

/*For IOF Plugin*/
struct iof_state {
	struct cnss_plugin_cb		*cb;
	struct proto			*proto;
	struct iof_ctx			iof_ctx;
	d_list_t			fs_list;
	/* CNSS Prefix */
	char				*cnss_prefix;
	struct ctrl_dir			*ionss_dir;
	struct ctrl_dir			*projections_dir;
	struct iof_group_info		*groups;
	uint32_t			num_groups;
	uint32_t			num_proj;
};

struct iof_group_info {
	struct iof_service_group	grp;
	char				*grp_name;

	/* Set to true if the CaRT group attached */
	bool				crt_attached;

	/* Set to true if registered with the IONSS */
	bool				iof_registered;
};

struct iof_rb {
	d_list_t			list;
	struct iof_projection_info	*fs_handle;
	struct iof_file_handle		*handle;
	struct fuse_bufvec		fbuf;
	struct iof_local_bulk		lb;
	crt_rpc_t			*rpc;
	fuse_req_t			req;
	struct iof_pool_type		*pt;
	size_t				buf_size;
	bool				failure;
};

struct iof_wb {
	d_list_t			list;
	struct iof_projection_info	*fs_handle;
	struct iof_file_handle		*handle;
	struct iof_local_bulk		lb;
	crt_rpc_t			*rpc;
	fuse_req_t			req;
	bool				failure;
};

enum iof_failover_state {
	iof_failover_running,
	iof_failover_offline,
	iof_failover_in_progress,
	iof_failover_complete,
};

struct iof_projection_info {
	struct iof_projection		proj;
	struct iof_ctx			ctx;
	struct iof_state		*iof_state;
	struct ios_gah			gah;
	d_list_t			link;
	struct ctrl_dir			*fs_dir;
	struct ctrl_dir			*stats_dir;
	struct iof_stats		*stats;
	struct fuse_session		*session;
	/* The name of the mount directory */
	struct ios_name			mnt_dir;
	char				*mount_point;

	enum iof_failover_state		failover_state;

	/* The name of the ctrlfs direcory */
	struct ios_name			ctrl_dir;
	/* fuse client implementation */
	struct fuse_lowlevel_ops	*fuse_ops;
	/* Feature Flags */
	uint64_t			flags;
	int				fs_id;
	struct iof_pool			pool;
	struct iof_pool_type		*dh_pool;
	struct iof_pool_type		*fgh_pool;
	struct iof_pool_type		*close_pool;
	struct iof_pool_type		*lookup_pool;
	struct iof_pool_type		*mkdir_pool;
	struct iof_pool_type		*symlink_pool;
	struct iof_pool_type		*fh_pool;
	struct iof_pool_type		*rb_pool_page;
	struct iof_pool_type		*rb_pool_large;
	struct iof_pool_type		*write_pool;
	uint32_t			max_read;
	uint32_t			max_iov_read;
	uint32_t			readdir_size;
	/* set to error code if projection is off-line */
	int				offline_reason;
	struct d_hash_table		inode_ht;

	/* List of directory handles owned by FUSE */
	pthread_mutex_t			od_lock;
	d_list_t			opendir_list;

	/* List of open file handles owned by FUSE */
	pthread_mutex_t			of_lock;
	d_list_t			openfile_list;

	/* Held for any access/modification to a gah on any inode/file/dir */
	pthread_mutex_t			gah_lock;
};

#define FS_IS_OFFLINE(HANDLE) ((HANDLE)->offline_reason != 0)

/*
 * Returns the correct RPC Type ID from the protocol registry.
 */
#define FS_TO_OP(HANDLE, FN) ((HANDLE)->proj.proto->rpc_types[DEF_RPC_TYPE(FN)].op_id)

int iof_is_mode_supported(uint8_t flags);

struct fuse_lowlevel_ops *iof_get_fuse_ops(uint64_t);

/* Everything above here relates to how the ION plugin communicates with the
 * CNSS, everything below here relates to internals to the plugin.  At some
 * point we should split this header file up into two.
 */

#define STAT_ADD(STATS, STAT) atomic_inc(&STATS->STAT)
#define STAT_ADD_COUNT(STATS, STAT, COUNT) atomic_add(&STATS->STAT, COUNT)

/* Helper macros for open() and creat() to log file access modes */
#define LOG_MODE(HANDLE, FLAGS, MODE) do {			\
		if ((FLAGS) & (MODE))				\
			IOF_LOG_DEBUG("%p " #MODE, HANDLE);	\
		FLAGS &= ~MODE;					\
	} while (0)

/* Dump the file open mode to the logile
 *
 * On a 64 bit system O_LARGEFILE is assumed so always set but defined to zero
 * so set LARGEFILE here for debugging
 */
#define LARGEFILE 0100000
#define LOG_FLAGS(HANDLE, INPUT) do {					\
		int _flag = (INPUT);					\
		LOG_MODE((HANDLE), _flag, O_APPEND);			\
		LOG_MODE((HANDLE), _flag, O_RDONLY);			\
		LOG_MODE((HANDLE), _flag, O_WRONLY);			\
		LOG_MODE((HANDLE), _flag, O_RDWR);			\
		LOG_MODE((HANDLE), _flag, O_ASYNC);			\
		LOG_MODE((HANDLE), _flag, O_CLOEXEC);			\
		LOG_MODE((HANDLE), _flag, O_CREAT);			\
		LOG_MODE((HANDLE), _flag, O_DIRECT);			\
		LOG_MODE((HANDLE), _flag, O_DIRECTORY);			\
		LOG_MODE((HANDLE), _flag, O_DSYNC);			\
		LOG_MODE((HANDLE), _flag, O_EXCL);			\
		LOG_MODE((HANDLE), _flag, O_LARGEFILE);			\
		LOG_MODE((HANDLE), _flag, LARGEFILE);			\
		LOG_MODE((HANDLE), _flag, O_NOATIME);			\
		LOG_MODE((HANDLE), _flag, O_NOCTTY);			\
		LOG_MODE((HANDLE), _flag, O_NONBLOCK);			\
		LOG_MODE((HANDLE), _flag, O_PATH);			\
		LOG_MODE((HANDLE), _flag, O_SYNC);			\
		LOG_MODE((HANDLE), _flag, O_TRUNC);			\
		if (_flag)						\
			IOF_LOG_ERROR("%p Flags 0%o", (HANDLE), _flag);	\
	} while (0)

/* Dump the file mode to the logfile
 */
#define LOG_MODES(HANDLE, INPUT) do {					\
		int _flag = (INPUT) & S_IFMT;				\
		LOG_MODE((HANDLE), _flag, S_IFREG);			\
		LOG_MODE((HANDLE), _flag, S_ISUID);			\
		LOG_MODE((HANDLE), _flag, S_ISGID);			\
		LOG_MODE((HANDLE), _flag, S_ISVTX);			\
		if (_flag)						\
			IOF_LOG_ERROR("%p Mode 0%o", (HANDLE), _flag);	\
	} while (0)

#define IOF_UNSUPPORTED_CREATE_FLAGS (O_ASYNC | O_CLOEXEC | O_DIRECTORY | \
					O_NOCTTY | O_PATH)

#define IOF_UNSUPPORTED_OPEN_FLAGS (IOF_UNSUPPORTED_CREATE_FLAGS | O_CREAT | \
					O_EXCL)

#define IOF_FUSE_REPLY_ERR(req, status)					\
	do {								\
		int __err = status;					\
		int __rc;						\
		if (__err <= 0) {					\
			IOF_TRACE_ERROR(req,				\
					"Invalid call to fuse_reply_err: %d", \
					__err);				\
			__err = EIO;					\
		}							\
		if (__err == ENOTSUP || __err == EIO)			\
			IOF_TRACE_WARNING(req, "Returning %d '%s'",	\
					  __err, strerror(__err));	\
		else							\
			IOF_TRACE_DEBUG(req, "Returning %d '%s'",	\
					__err, strerror(__err));	\
		__rc = fuse_reply_err(req, __err);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_ZERO(req)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning 0");			\
		__rc = fuse_reply_err(req, 0);				\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_ATTR(req, attr)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning attr");			\
		__rc = fuse_reply_attr(req, attr, 0);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_WRITE(req, bytes)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning write(%zi)", bytes);	\
		__rc = fuse_reply_write(req, bytes);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_OPEN(req, fi)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning open");			\
		__rc = fuse_reply_open(req, &fi);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_CREATE(req, entry, fi)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning create");		\
		__rc = fuse_reply_create(req, &entry, &fi);		\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_create returned %d:%s",\
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_ENTRY(req, entry)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning entry");		\
		__rc = fuse_reply_entry(req, &entry);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_entry returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_STATFS(req, stat)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning statfs");		\
		__rc = fuse_reply_statfs(req, stat);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_statfs returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOF_FUSE_REPLY_IOCTL(req, gah_info, size)			\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning ioctl");		\
		__rc = fuse_reply_ioctl(req, 0, &gah_info, size);	\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_ioctl returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

struct ioc_request;

struct ioc_request_api {
	void				(*on_send)(struct ioc_request *req);
	void				(*on_result)(struct ioc_request *req);
	int				(*on_evict)(struct ioc_request *req);
};

enum ioc_request_state {
	RS_INIT = 1,
	RS_RESET,
	RS_LIVE
};

struct ioc_request {
	struct iof_projection_info	*fsh;
	crt_rpc_t			*rpc;
	fuse_req_t			req;
	const struct ioc_request_api	*cb;
	int				rc;
	enum ioc_request_state		rs;
};

#define IOC_REQUEST_INIT(REQUEST, FSH)		\
	do {					\
		(REQUEST)->fsh = FSH;		\
		(REQUEST)->rpc = NULL;		\
		(REQUEST)->rs = RS_INIT;	\
	} while (0)

#define IOC_REQUEST_RESET(REQUEST)					\
	do {								\
		D_ASSERT((REQUEST)->rs == RS_INIT ||			\
			(REQUEST)->rs == RS_RESET ||			\
			(REQUEST)->rs == RS_LIVE);			\
		(REQUEST)->rs = RS_RESET;				\
		(REQUEST)->rc = 0;					\
	} while (0)

/* Correctly resolve the return codes and errors from the RPC response.
 * If the error code was already non-zero, it means an error occurred on
 * the client; do nothing. A non-zero error code in the RPC response
 * denotes a server error, in which case, set the status error code to EIO.
 *
 */
#define IOC_REQUEST_RESOLVE(STATUS, OUT)				\
	do {								\
		if (((OUT) != NULL) && (!(STATUS)->rc)) {		\
			(STATUS)->rc = (OUT)->rc;			\
			if ((OUT)->err)					\
				(STATUS)->rc = EIO;			\
		}							\
	} while (0)

/* Data which is stored against an open directory handle */
struct iof_dir_handle {
	struct ioc_request		open_req;
	struct ioc_request		close_req;
	/* The handle for accessing the directory on the IONSS */
	struct ios_gah			gah;
	/* Any RPC reference held across readdir() calls */
	crt_rpc_t			*rpc;
	/* Pointer to any retreived data from readdir() RPCs */
	struct iof_readdir_reply	*replies;
	int				reply_count;
	void				*replies_base;
	/* Set to True if the current batch of replies is the final one */
	int				last_replies;
	/* Set to 1 initially, but 0 if there is a unrecoverable error */
	int				handle_valid;
	/* Set to 0 if the server rejects the GAH at any point */
	ATOMIC int			gah_ok;
	/* The inode number of the directory */
	ino_t				inode_no;
	crt_endpoint_t			ep;
	d_list_t			list;
};

/** Data which is stored for a currently open file */
struct iof_file_handle {
	/** The projection this file belongs to */
	struct iof_projection_info	*fs_handle;
	/** Common information for file handle, contains GAH and EP
	 * information.  This is shared between CNSS and IL code to allow
	 * use of some common code.
	 */
	struct iof_file_common		common;
	/** Boolean flag to indicate GAH is valid.
	 * Set to 1 when file is opened, however may be set to 0 either by
	 * ionss returning -DER_NONEXIST or by ionss failure
	 */
	ATOMIC int			gah_ok;
	/** Open RPC, precreated */
	crt_rpc_t			*open_rpc;
	/** Create RPC, precreated */
	crt_rpc_t			*creat_rpc;
	/** Release RPC, precreated */
	crt_rpc_t			*release_rpc;
	/** List of open files, stored in fs_handle->openfile_list */
	d_list_t			list;
	/** The inode number of the file */
	ino_t				inode_no;
	/** A pre-allocated inode entry.  This is created as the struct is
	 * allocated and then used on a successful create() call.  Once
	 * the file handle is in use then this field will be NULL.
	 */
	struct ioc_inode_entry		*ie;
	/** Fuse req for open/create command.  Used by the RPC callback
	 * function to reply to a FUSE request
	 */
	fuse_req_t			open_req;
};

/* GAH ok manipulation macros. gah_ok is defined as a int but we're
 * using it as a bool and accessing it though the use of atomics.
 *
 * These macros work on both file and directory handles.
 */

/** Set the GAH so that it's valid */
#define H_GAH_SET_VALID(OH) atomic_store_release(&OH->gah_ok, 1)

/** Set the GAH so that it's invalid.  Assumes it currently valid */
#define H_GAH_SET_INVALID(OH) atomic_store_release(&OH->gah_ok, 0)

/** Check if the handle is valid by reading the gah_ok field. */
#define H_GAH_IS_VALID(OH) atomic_load_consume(&OH->gah_ok)

struct common_req {
	struct ioc_request		request;
	d_list_t			list;
};

struct ioc_inode_entry {
	struct ios_gah	gah;
	char		name[256];
	struct stat	stat;
	d_list_t	list;
	fuse_ino_t	parent;
	ATOMIC uint	ref;
	bool		failover;
};

struct entry_req {
	struct ioc_request		request;
	struct ioc_inode_entry		*ie;
	d_list_t			list;
	crt_opcode_t			opcode;
	struct iof_pool_type		*pool;
	char				*dest;
};

/* inode.c */

/* Convert from a inode to a GAH using the hash table */
int find_gah(struct iof_projection_info *, fuse_ino_t, struct ios_gah *);

/* Convert from a inode to a GAH and keep a reference using the hash table */
int find_gah_ref(struct iof_projection_info *, fuse_ino_t, struct ios_gah *);

/* Drop a reference on the GAH in the hash table */
void drop_ino_ref(struct iof_projection_info *, ino_t);

void ie_close(struct iof_projection_info *, struct ioc_inode_entry *);

int iof_fs_send(struct ioc_request *request);

int ioc_simple_resend(struct ioc_request *request);

void ioc_ll_gen_cb(const struct crt_cb_info *);

void ioc_ll_lookup(fuse_req_t, fuse_ino_t, const char *);

void ioc_ll_forget(fuse_req_t, fuse_ino_t, uint64_t);

void ioc_ll_forget_multi(fuse_req_t, size_t, struct fuse_forget_data *);

void ioc_ll_getattr(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void ioc_ll_statfs(fuse_req_t, fuse_ino_t);

void ioc_ll_readlink(fuse_req_t, fuse_ino_t);

void ioc_ll_mkdir(fuse_req_t, fuse_ino_t, const char *, mode_t);

void ioc_ll_open(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void ioc_ll_create(fuse_req_t, fuse_ino_t, const char *, mode_t,
		   struct fuse_file_info *);

void ioc_ll_read(fuse_req_t, fuse_ino_t, size_t, off_t,
		 struct fuse_file_info *);

void ioc_ll_release(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void ioc_int_release(struct iof_file_handle *);

void ioc_ll_unlink(fuse_req_t, fuse_ino_t, const char *);

void ioc_ll_rmdir(fuse_req_t, fuse_ino_t, const char *);

void ioc_ll_opendir(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void ioc_ll_readdir(fuse_req_t, fuse_ino_t, size_t, off_t,
		    struct fuse_file_info *);

void ioc_ll_rename(fuse_req_t, fuse_ino_t, const char *, fuse_ino_t,
		   const char *, unsigned int);

void ioc_ll_releasedir(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void ioc_int_releasedir(struct iof_dir_handle *);

void ioc_ll_write(fuse_req_t, fuse_ino_t, const char *,	size_t, off_t,
		  struct fuse_file_info *);

void ioc_ll_write_buf(fuse_req_t, fuse_ino_t, struct fuse_bufvec *,
		      off_t, struct fuse_file_info *);

void ioc_ll_ioctl(fuse_req_t, fuse_ino_t, int, void *, struct fuse_file_info *,
		  unsigned int, const void *, size_t, size_t);

void ioc_ll_setattr(fuse_req_t, fuse_ino_t, struct stat *, int,
		    struct fuse_file_info *);

void ioc_ll_symlink(fuse_req_t, const char *, fuse_ino_t, const char *);

void ioc_ll_fsync(fuse_req_t, fuse_ino_t, int, struct fuse_file_info *);

void iof_entry_cb(struct ioc_request *request);

#endif
