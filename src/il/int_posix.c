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
#include "intercept.h"
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

IOIL_FORWARD_DECL(int, open, (const char *pathname, int flags, ...));
IOIL_FORWARD_DECL(int, open64, (const char *pathname, int flags, ...));
IOIL_FORWARD_DECL(int, close, (int fd));

int IOIL_DECL(open)(const char *pathname, int flags, ...)
{
	unsigned int mode; /* mode_t gets "promoted" to unsigned int
			    * for va_arg routine
			    */

	IOIL_FORWARD_MAP_OR_FAIL(open);

	DEBUG_PRINT("fopen %s intercepted\n", pathname);

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		return __real_open(pathname, flags, mode);
	}

	return __real_open(pathname, flags);
}

int IOIL_DECL(open64)(const char *pathname, int flags, ...)
{
	unsigned int mode; /* mode_t gets "promoted" to unsigned int
			    * for va_arg routine
			    */
	IOIL_FORWARD_MAP_OR_FAIL(open64);

	DEBUG_PRINT("fopen64 %s intercepted\n", pathname);

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, unsigned int);
		va_end(ap);

		return __real_open64(pathname, flags, mode);
	}

	return __real_open64(pathname, flags);
}

int IOIL_DECL(creat)(const char *pathname, mode_t mode)
{
	IOIL_FORWARD_MAP_OR_FAIL(open);

	DEBUG_PRINT("creat %s intercepted\n", pathname);
	/* Same as open with O_CREAT|O_WRONLY|O_TRUNC */
	return __real_open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int IOIL_DECL(creat64)(const char *pathname, mode_t mode)
{
	IOIL_FORWARD_MAP_OR_FAIL(open64);

	DEBUG_PRINT("creat64 %s intercepted\n", pathname);
	/* Same as open with O_CREAT|O_WRONLY|O_TRUNC */
	return __real_open64(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

int IOIL_DECL(close)(int fd)
{
	IOIL_FORWARD_MAP_OR_FAIL(close);

	DEBUG_PRINT("close intercepted\n");

	return __real_close(fd);
}