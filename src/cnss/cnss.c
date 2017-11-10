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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <dlfcn.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>
#include <sys/xattr.h>
#include <cart/api.h>
#include <gurt/common.h>

#include "cnss_plugin.h"
#include "version.h"
#include "log.h"
#include "ctrl_common.h"

#include "cnss.h"

/* A descriptor for the plugin */
struct plugin_entry {
	/* The callback functions, as provided by the plugin */
	struct cnss_plugin *fns;
	/* The size of the fns struct */
	size_t fns_size;

	/* The dl_open() reference to this so it can be closed cleanly */
	void *dl_handle;

	/* The list of plugins */
	d_list_t list;

	/* Flag to say if plugin is active */
	int active;

	/* The copy of the plugin->cnss callback functions this
	 * plugin uses
	 */
	struct cnss_plugin_cb self_fns;

	d_list_t fuse_list;
};

struct fs_info {
	char		*mnt;
	struct fuse	*fuse;
	struct fuse_session *session;
	pthread_t	thread;
	pthread_mutex_t	lock;
	void		*private_data;
	d_list_t	entries;
	int		running:1,
			mt:1;
};

#define FN_TO_PVOID(fn) (*((void **)&(fn)))

/*
 * Helper macro only, do not use other then in CALL_PLUGIN_*
 *
 * Unfortunately because of this macros use of continue it does not work
 * correctly if contained in a do - while loop.
 */
#define CHECK_PLUGIN_FUNCTION(ITER, FN)					\
	if (!ITER->active)						\
		continue;						\
	if (!ITER->fns->FN)						\
		continue;						\
	if ((offsetof(struct cnss_plugin, FN) + sizeof(void *)) > ITER->fns_size) \
		continue;						\
	IOF_LOG_INFO("Plugin %s(%p) calling %s at %p",			\
		ITER->fns->name,					\
		(void *)ITER->fns->handle,				\
		#FN,							\
		FN_TO_PVOID(ITER->fns->FN))

/*
 * Call a function in each registered and active plugin
 */
#define CALL_PLUGIN_FN(LIST, FN)					\
	do {								\
		struct plugin_entry *_li;				\
		IOF_LOG_INFO("Calling plugin %s", #FN);			\
		d_list_for_each_entry(_li, LIST, list) {		\
			CHECK_PLUGIN_FUNCTION(_li, FN);			\
			_li->fns->FN(_li->fns->handle);		\
		}							\
		IOF_LOG_INFO("Finished calling plugin %s", #FN);	\
	} while (0)

/*
 * Call a function in each registered and active plugin.  If the plugin
 * return non-zero disable the plugin.
 */
#define CALL_PLUGIN_FN_CHECK(LIST, FN)					\
	do {								\
		struct plugin_entry *_li;				\
		IOF_LOG_INFO("Calling plugin %s", #FN);			\
		d_list_for_each_entry(_li, LIST, list) {		\
			int _rc;					\
			CHECK_PLUGIN_FUNCTION(_li, FN);			\
			_rc = _li->fns->FN(_li->fns->handle);	\
			if (_rc != 0) {					\
				IOF_LOG_INFO("Disabling plugin %s %d",	\
					_li->fns->name, _rc);	\
				_li->active = 0;			\
			}						\
		}							\
		IOF_LOG_INFO("Finished calling plugin %s", #FN);	\
	} while (0)

/*
 * Call a function in each registered and active plugin, providing additional
 * parameters.
 */
#define CALL_PLUGIN_FN_PARAM(LIST, FN, ...)				\
	do {								\
		struct plugin_entry *_li;				\
		IOF_LOG_INFO("Calling plugin %s", #FN);			\
		d_list_for_each_entry(_li, LIST, list) {		\
			CHECK_PLUGIN_FUNCTION(_li, FN);			\
			_li->fns->FN(_li->fns->handle, __VA_ARGS__);	\
		}							\
		IOF_LOG_INFO("Finished calling plugin %s", #FN);	\
	} while (0)

/*
 * Call a function in each registered and active plugin, providing additional
 * parameters.  If the function returns non-zero then disable the plugin
 */
#define CALL_PLUGIN_FN_START(LIST, FN)					\
	do {								\
		struct plugin_entry *_li;				\
		int _rc;						\
		IOF_LOG_INFO("Calling plugin %s", #FN);			\
		d_list_for_each_entry(_li, LIST, list) {		\
			CHECK_PLUGIN_FUNCTION(_li, FN);			\
			_rc = _li->fns->FN(_li->fns->handle,		\
					   &_li->self_fns,		\
					   sizeof(struct cnss_plugin_cb));\
			if (_rc != 0) {					\
				IOF_LOG_INFO("Disabling plugin %s %d",	\
					     _li->fns->name, _rc);	\
				_li->active = 0;			\
			}						\
		}							\
		IOF_LOG_INFO("Finished calling plugin %s", #FN);	\
	} while (0)

static const char *get_config_option(const char *var)
{
	return getenv((const char *)var);
}

static int register_fuse(void *arg,
			 struct fuse_operations *ops,
			 struct fuse_lowlevel_ops *flo,
			 struct fuse_args *args,
			 const char *mnt,
			 int threaded,
			 void *private_data);

/* Load a plugin from a fn pointer, return -1 if there was a fatal problem */
static int add_plugin(struct cnss_info *info, cnss_plugin_init_t fn,
		      void *dl_handle)
{
	struct plugin_entry *entry;
	int rc;

	IOF_LOG_INFO("Loading plugin at entry point %p", FN_TO_PVOID(fn));

	D_ALLOC_PTR(entry);
	if (!entry)
		return CNSS_ERR_NOMEM;

	rc = fn(&entry->fns, &entry->fns_size);
	if (rc != 0) {
		IOF_TRACE_DOWN(entry);
		D_FREE(entry);
		IOF_LOG_INFO("Plugin at entry point %p failed (%d)",
			     FN_TO_PVOID(fn), rc);
		return CNSS_SUCCESS;
	}

	IOF_TRACE_UP(entry, info, "plugin_entry");
	entry->dl_handle = dl_handle;

	entry->self_fns.fuse_version = 3;

	entry->self_fns.get_config_option = get_config_option;
	entry->self_fns.create_ctrl_subdir = ctrl_create_subdir;
	entry->self_fns.register_ctrl_variable = ctrl_register_variable;
	entry->self_fns.register_ctrl_event = ctrl_register_event;
	entry->self_fns.register_ctrl_tracker = ctrl_register_tracker;
	entry->self_fns.register_ctrl_constant = ctrl_register_constant;
	entry->self_fns.register_ctrl_constant_int64 =
		ctrl_register_constant_int64;
	entry->self_fns.register_ctrl_constant_uint64 =
		ctrl_register_constant_uint64;
	entry->self_fns.register_ctrl_uint64_variable =
		ctrl_register_uint64_variable;
	entry->self_fns.register_fuse_fs = register_fuse;
	entry->self_fns.handle = entry;

	d_list_add(&entry->list, &info->plugins);

	D_INIT_LIST_HEAD(&entry->fuse_list);

	IOF_LOG_INFO("Added plugin %s(%p) from entry point %p ",
		     entry->fns->name,
		     (void *)entry->fns->handle,
		     FN_TO_PVOID(fn));

	if (entry->fns->version == CNSS_PLUGIN_VERSION) {
		entry->active = 1;
	} else {
		IOF_LOG_INFO("Plugin %s(%p) version incorrect %x %x, disabling",
			     entry->fns->name,
			     (void *)entry->fns->handle,
			     entry->fns->version,
			     CNSS_PLUGIN_VERSION);
	}

	if (entry->fns->name == NULL) {
		IOF_LOG_ERROR("Disabling plugin: name is required\n");
		entry->active = 0;
	}
	rc = ctrl_create_subdir(NULL, entry->fns->name,
				&entry->self_fns.plugin_dir);
	if (rc != 0) {
		IOF_LOG_ERROR("Disabling plugin %s: ctrl dir creation failed"
			      " %d", entry->fns->name, rc);
		entry->active = 0;
	}

	if (sizeof(struct cnss_plugin) != entry->fns_size) {
		IOF_LOG_INFO("Plugin %s(%p) size incorrect %zd %zd, some functions may be disabled",
			     entry->fns->name,
			     (void *)entry->fns->handle,
			     entry->fns_size,
			     sizeof(struct cnss_plugin));
	}

	return CNSS_SUCCESS;
}

static void iof_fuse_umount(struct fs_info *info)
{
	if (info->session)
		fuse_session_unmount(info->session);
	else
		fuse_unmount(info->fuse);
}

static void *ll_loop_fn(void *args)
{
	int ret;
	struct fs_info *info = args;

	pthread_mutex_lock(&info->lock);
	info->running = 1;
	pthread_mutex_unlock(&info->lock);

	/*Blocking*/
	if (info->mt)
		ret = fuse_session_loop_mt(info->session, 0);
	else
		ret = fuse_session_loop(info->session);

	if (ret != 0)
		IOF_LOG_ERROR("Fuse loop exited with return code: %d", ret);

	pthread_mutex_lock(&info->lock);
	info->fuse = NULL;
	info->running = 0;
	pthread_mutex_unlock(&info->lock);
	return NULL;
}

static void *loop_fn(void *args)
{
	int ret;
	struct fs_info *info = (struct fs_info *)args;

	pthread_mutex_lock(&info->lock);
	info->running = 1;
	pthread_mutex_unlock(&info->lock);

	/*Blocking*/
	if (info->mt) {
		ret = fuse_loop_mt(info->fuse, 0);
	} else
		ret = fuse_loop(info->fuse);

	if (ret != 0)
		IOF_LOG_ERROR("Fuse loop exited with return code: %d", ret);

	pthread_mutex_lock(&info->lock);
	fuse_destroy(info->fuse);
	info->fuse = NULL;
	info->running = 0;
	pthread_mutex_unlock(&info->lock);

	return (void *)(uintptr_t)ret;
}

/*
 * Creates a fuse filesystem for any plugin that needs one.
 *
 * Should be called from the post_start plugin callback and creates
 * a filesystem.
 * Returns 0 on success, or non-zero on error.
 */
static int register_fuse(void *arg,
			 struct fuse_operations *ops,
			 struct fuse_lowlevel_ops *flo,
			 struct fuse_args *args,
			 const char *mnt,
			 int threaded,
			 void *private_data)
{
	struct plugin_entry *plugin = (struct plugin_entry *)arg;
	struct fs_info *info;
	int rc;

	if (!mnt) {
		IOF_LOG_ERROR("Invalid Mount point");
		return 1;
	}

	if ((mkdir(mnt, 0755) && errno != EEXIST)) {
		IOF_LOG_ERROR("Could not create directory %s for import", mnt);
		return 1;
	}

	D_ALLOC_PTR(info);
	if (!info) {
		IOF_LOG_ERROR("Could not allocate fuse info");
		return 1;
	}

	info->mt = threaded;

	info->mnt = strdup(mnt);
	if (!info->mnt) {
		IOF_LOG_ERROR("Could not allocate mnt");
		goto cleanup_no_mutex;
	}

	if (pthread_mutex_init(&info->lock, NULL)) {
		IOF_LOG_ERROR("Count not create mutex");
		goto cleanup_no_mutex;
	}

	info->private_data = private_data;

	if (flo) {
		int rc;

		info->session = fuse_session_new(args,
						 flo,
						 sizeof(*flo),
						 private_data);
		if (!info->session)
			goto cleanup;

		rc = fuse_session_mount(info->session, info->mnt);
		if (rc != 0)
			goto cleanup;
	} else {
		info->fuse = fuse_new(args, ops, sizeof(*ops), private_data);

		if (!info->fuse) {
			IOF_LOG_ERROR("Could not initialize fuse");
			fuse_opt_free_args(args);
			iof_fuse_umount(info);
			goto cleanup;
		}

		rc = fuse_mount(info->fuse, info->mnt);
		if (rc != 0) {
			IOF_LOG_ERROR("Could not successfully mount %s",
				info->mnt);
			goto cleanup;
		}
	}

	IOF_LOG_DEBUG("Registered a fuse mount point at : %s", info->mnt);
	IOF_LOG_DEBUG("Private data %p threaded %u", private_data, info->mt);

	fuse_opt_free_args(args);

	if (flo)
		rc = pthread_create(&info->thread, NULL,
				    ll_loop_fn, info);
	else
		rc = pthread_create(&info->thread, NULL,
				    loop_fn, info);

	if (rc) {
		IOF_LOG_ERROR("Could not start FUSE filesysten at %s",
			      info->mnt);
		iof_fuse_umount(info);
		goto cleanup;
	}

	d_list_add(&info->entries, &plugin->fuse_list);

	return 0;
cleanup:
	pthread_mutex_destroy(&info->lock);
cleanup_no_mutex:
	if (info->mnt)
		free(info->mnt);
	if (info)
		D_FREE(info);

	return 1;
}

static int
deregister_fuse(struct plugin_entry *plugin, struct fs_info *info)
{
	uintptr_t *rcp = NULL;
	int rc;

	pthread_mutex_lock(&info->lock);

	if (info->running) {

		IOF_LOG_DEBUG("Sending termination signal %s", info->mnt);

		/*
		 * If the FUSE thread is in the filesystem servicing requests
		 * then set the exit flag and send it a dummy operation to wake
		 * it up.  Drop the mutext before calling setxattr() as that
		 * will cause I/O activity and loop_fn() to deadlock with this
		 * function.
		 */
		if (info->session) {
			fuse_session_exit(info->session);
			fuse_session_unmount(info->session);
		} else {
			struct fuse_session *session = fuse_get_session(info->fuse);

			fuse_session_exit(session);
			fuse_session_unmount(session);
		}
		pthread_mutex_unlock(&info->lock);
	} else {
		pthread_mutex_unlock(&info->lock);
	}

	IOF_LOG_DEBUG("Unmounting FS: %s", info->mnt);
	pthread_join(info->thread, (void **)&rcp);
	d_list_del(&info->entries);

	rc = (uintptr_t)rcp;

	pthread_mutex_destroy(&info->lock);

	free(info->mnt);
	if (plugin->active && plugin->fns->deregister_fuse)
		plugin->fns->deregister_fuse(info->private_data);

	if (info->session)
		fuse_session_destroy(info->session);

	D_FREE(info);
	return rc;
}

void shutdown_fs(struct cnss_info *cnss_info)
{
	struct plugin_entry *plugin;
	struct fs_info *info, *i2;
	int rc;

	d_list_for_each_entry(plugin, &cnss_info->plugins, list) {
		d_list_for_each_entry_safe(info, i2, &plugin->fuse_list,
					     entries) {
			rc = deregister_fuse(plugin, info);
			if (rc)
				IOF_LOG_ERROR("Shutdown mount %s failed", info->mnt);
		}
	}
}

struct iof_barrier_info {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool in_barrier;
};

static void barrier_done(struct crt_barrier_cb_info *info)
{
	struct iof_barrier_info *b_info = info->bci_arg;

	if (info->bci_rc != 0)
		IOF_LOG_ERROR("Could not execute barrier: rc = %d\n",
			      info->bci_rc);

	pthread_mutex_lock(&b_info->lock);
	b_info->in_barrier = false;
	pthread_cond_signal(&b_info->cond);
	pthread_mutex_unlock(&b_info->lock);
}

static void issue_barrier(void)
{
	struct iof_barrier_info b_info;

	pthread_mutex_init(&b_info.lock, NULL);
	pthread_cond_init(&b_info.cond, NULL);
	b_info.in_barrier = true;
	crt_barrier(NULL, barrier_done, &b_info);
	/* Existing service thread will progress barrier */
	pthread_mutex_lock(&b_info.lock);
	while (b_info.in_barrier)
		pthread_cond_wait(&b_info.cond, &b_info.lock);
	pthread_mutex_unlock(&b_info.lock);

	pthread_cond_destroy(&b_info.cond);
	pthread_mutex_destroy(&b_info.lock);
}

int main(void)
{
	char *cnss = "CNSS";
	char *plugin_file = NULL;
	const char *prefix;
	char *version = iof_get_version();
	struct plugin_entry *list_iter;
	struct plugin_entry *list_iter_next;
	struct cnss_info *cnss_info;
	bool active_plugins = false;

	int ret;
	bool service_process_set = false;
	char *ctrl_prefix;

	iof_log_init("CN", "CNSS", NULL);

	IOF_LOG_INFO("CNSS version: %s", version);

	prefix = getenv("CNSS_PREFIX");

	if (prefix == NULL) {
		IOF_LOG_ERROR("CNSS_PREFIX is required");
		return CNSS_ERR_PREFIX;
	}

	ret = crt_group_config_path_set(prefix);
	if (ret != 0) {
		IOF_LOG_ERROR("Could not set group config prefix");
		return CNSS_ERR_CART;
	}

	D_ALLOC_PTR(cnss_info);
	if (!cnss_info)
		return CNSS_ERR_NOMEM;
	IOF_TRACE_UP(cnss_info, NULL, "cnss_info");

	ctrl_info_init(&cnss_info->info);

	ret = asprintf(&ctrl_prefix, "%s/.ctrl", prefix);

	if (ret == -1) {
		IOF_LOG_ERROR("Could not allocate memory for ctrl prefix");
		return CNSS_ERR_NOMEM;
	}

	ret = ctrl_fs_start(ctrl_prefix);
	if (ret != 0) {
		IOF_LOG_ERROR("Could not start ctrl fs");
		return CNSS_ERR_CTRL_FS;
	}

	register_cnss_controls(&cnss_info->info);

	D_INIT_LIST_HEAD(&cnss_info->plugins);

	if (getenv("CNSS_DISABLE_IOF") != NULL) {
		IOF_LOG_INFO("Skipping IOF plugin");
	} else {
		/* Load the build-in iof "plugin" */
		ret = add_plugin(cnss_info, iof_plugin_init, NULL);
		if (ret != 0) {
			ret = CNSS_ERR_PLUGIN;
			goto shutdown_ctrl_fs;
		}
	}

	/* Check to see if an additional plugin file has been requested and
	 * attempt to load it
	 */
	plugin_file = getenv("CNSS_PLUGIN_FILE");
	if (plugin_file) {
		void *dl_handle = dlopen(plugin_file, RTLD_LAZY);
		cnss_plugin_init_t fn = NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

		if (dl_handle)
			fn = (cnss_plugin_init_t)dlsym(dl_handle,
						       CNSS_PLUGIN_INIT_SYMBOL);

#pragma GCC diagnostic pop

		IOF_LOG_INFO("Loading plugin file %s %p %p",
			     plugin_file,
			     dl_handle,
			     FN_TO_PVOID(fn));
		if (fn) {
			ret = add_plugin(cnss_info, fn, dl_handle);
			if (ret != 0) {
				ret = CNSS_ERR_PLUGIN;
				goto shutdown_ctrl_fs;
			}
		}
	}

	/* Walk the list of plugins and if any require the use of a service
	 * process set across the CNSS nodes then create one
	 */
	d_list_for_each_entry(list_iter, &cnss_info->plugins, list) {
		if (list_iter->active && list_iter->fns->require_service) {
			service_process_set = true;
			break;
		}
	}

	IOF_LOG_INFO("Forming %s process set",
		     service_process_set ? "service" : "client");

	/*initialize CaRT*/
	ret = crt_init(cnss, service_process_set ? CRT_FLAG_BIT_SERVER : 0);
	if (ret) {
		IOF_LOG_ERROR("crt_init failed with ret = %d", ret);
		ret = CNSS_ERR_CART;
		goto shutdown_ctrl_fs;
	}

	if (service_process_set) {
		/* Need to dump the CNSS attach info for singleton
		 * CNSS clients (e.g. libcppr)
		 */
		ret = crt_group_config_save(NULL, false);
		if (ret != 0) {
			IOF_LOG_ERROR("Could not save attach info for CNSS");
			ret = CNSS_ERR_CART;
			goto shutdown_ctrl_fs;
		}
	}

	/* Call start for each plugin which should perform none-local
	 * operations only.  Plugins can choose to disable themselves
	 * at this point.
	 */
	CALL_PLUGIN_FN_START(&cnss_info->plugins, start);

	/* Wait for all nodes to finish 'start' before doing 'post_start' */
	if (service_process_set)
		issue_barrier();

	/* Call post_start for each plugin, which could communicate over
	 * the network.  Plugins can choose to disable themselves
	 * at this point.
	 */
	CALL_PLUGIN_FN_CHECK(&cnss_info->plugins, post_start);

	/* Walk the plugins and check for active ones */
	d_list_for_each_entry_safe(list_iter, list_iter_next,
				   &cnss_info->plugins, list) {
		if (list_iter->active) {
			active_plugins = true;
			continue;
		}
		if (list_iter->fns->destroy_plugin_data) {
			IOF_LOG_INFO("Plugin %s(%p) calling destroy_plugin_data at %p",
				list_iter->fns->name,
				list_iter->fns->handle,
				FN_TO_PVOID(list_iter->fns->destroy_plugin_data));
			list_iter->fns->destroy_plugin_data(list_iter->fns->handle);
		}
		d_list_del(&list_iter->list);

		if (list_iter->dl_handle)
			dlclose(list_iter->dl_handle);
		D_FREE(list_iter);
	}

	/* TODO: How to handle this case? */
	if (!active_plugins) {
		IOF_LOG_ERROR("No active plugins");
		ret = 1;
		goto shutdown_cart;
	}

	cnss_info->info.active = 1;

	wait_for_shutdown(&cnss_info->info);

	CALL_PLUGIN_FN(&cnss_info->plugins, stop_client_services);
	CALL_PLUGIN_FN(&cnss_info->plugins, flush_client_services);

	if (service_process_set)
		issue_barrier();

	CALL_PLUGIN_FN(&cnss_info->plugins, stop_plugin_services);
	CALL_PLUGIN_FN(&cnss_info->plugins, flush_plugin_services);

shutdown_cart:
	shutdown_fs(cnss_info);
	CALL_PLUGIN_FN(&cnss_info->plugins, destroy_plugin_data);

	ret = crt_finalize();

	ctrl_fs_shutdown(); /* Shuts down ctrl fs and waits */

	while (!d_list_empty(&cnss_info->plugins)) {
		struct plugin_entry *entry;

		entry = d_list_entry(cnss_info->plugins.next,
				       struct plugin_entry, list);
		d_list_del(&entry->list);

		if (entry->dl_handle != NULL)
			dlclose(entry->dl_handle);
		IOF_TRACE_DOWN(entry);
		D_FREE(entry);
	}

	IOF_TRACE_DOWN(cnss_info);
	free(ctrl_prefix);
	iof_log_close();
	D_FREE(cnss_info);

	return ret;

shutdown_ctrl_fs:
	ctrl_fs_disable();
	ctrl_fs_shutdown();
	free(ctrl_prefix);

	return ret;
}

int cnss_dump_log(struct ctrl_info *info)
{
	struct cnss_info *cnss_info = container_of(info, struct cnss_info,
						   info);

	if (!cnss_info)
		return -1;

	CALL_PLUGIN_FN(&cnss_info->plugins, dump_log);
	return 0;
}

