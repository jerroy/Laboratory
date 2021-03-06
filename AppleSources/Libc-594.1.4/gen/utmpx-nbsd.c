/*	$NetBSD: utmpx.c,v 1.21 2003/09/06 16:42:10 wiz Exp $	 */

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>

#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: utmpx.c,v 1.21 2003/09/06 16:42:10 wiz Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef LEGACY_UTMP_APIS
#include <utmp.h>
#endif /* LEGACY_UTMP_APIS */
#include <utmpx.h>
#include <utmpx-darwin.h>
#include <errno.h>
#include <vis.h>
#include <notify.h>

static FILE *fp;
static int readonly = 0;
static struct utmpx ut;
static char utfile[MAXPATHLEN] = _PATH_UTMPX;
__private_extern__ int utfile_system = 1; /* are we using _PATH_UTMPX? */
__private_extern__ pthread_mutex_t utmpx_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct utmpx *_getutxid(const struct utmpx *);

__private_extern__ const char _utmpx_vers[] = "utmpx-1.00";

__private_extern__ void
_setutxent()
{

	(void)memset(&ut, 0, sizeof(ut));
	if (fp == NULL)
		return;
#ifdef __LP64__
	(void)fseeko(fp, (off_t)sizeof(struct utmpx32), SEEK_SET);
#else /* __LP64__ */
	(void)fseeko(fp, (off_t)sizeof(ut), SEEK_SET);
#endif /* __LP64__ */
}


void
setutxent()
{
	UTMPX_LOCK;
	_setutxent();
	UTMPX_UNLOCK;
}


__private_extern__ void
_endutxent()
{

	(void)memset(&ut, 0, sizeof(ut));
	if (fp != NULL) {
		(void)fclose(fp);
		fp = NULL;
		readonly = 0;
	}
}


void
endutxent()
{
	UTMPX_LOCK;
	_endutxent();
	UTMPX_UNLOCK;
}


static struct utmpx *
_getutxent()
{
#ifdef __LP64__
	struct utmpx32 ut32;
#endif /* __LP64__ */

	if (fp == NULL) {
		struct stat st;

		if ((fp = fopen(utfile, "r+")) == NULL)
			if ((fp = fopen(utfile, "w+")) == NULL) {
				if ((fp = fopen(utfile, "r")) == NULL)
					goto fail;
				else
					readonly = 1;
			}

		fcntl(fileno(fp), F_SETFD, 1); /* set close-on-exec flag */

		/* get file size in order to check if new file */
		if (fstat(fileno(fp), &st) == -1)
			goto failclose;

		if (st.st_size == 0) {
			/* new file, add signature record */
#ifdef __LP64__
			(void)memset(&ut32, 0, sizeof(ut32));
			ut32.ut_type = SIGNATURE;
			(void)memcpy(ut32.ut_user, _utmpx_vers, sizeof(_utmpx_vers));
			if (fwrite(&ut32, sizeof(ut32), 1, fp) != 1)
#else /* __LP64__ */
			(void)memset(&ut, 0, sizeof(ut));
			ut.ut_type = SIGNATURE;
			(void)memcpy(ut.ut_user, _utmpx_vers, sizeof(_utmpx_vers));
			if (fwrite(&ut, sizeof(ut), 1, fp) != 1)
#endif /* __LP64__ */
				goto failclose;
		} else {
			/* old file, read signature record */
#ifdef __LP64__
			if (fread(&ut32, sizeof(ut32), 1, fp) != 1)
#else /* __LP64__ */
			if (fread(&ut, sizeof(ut), 1, fp) != 1)
#endif /* __LP64__ */
				goto failclose;
#ifdef __LP64__
			if (memcmp(ut32.ut_user, _utmpx_vers, sizeof(_utmpx_vers)) != 0 ||
			    ut32.ut_type != SIGNATURE)
#else /* __LP64__ */
			if (memcmp(ut.ut_user, _utmpx_vers, sizeof(_utmpx_vers)) != 0 ||
			    ut.ut_type != SIGNATURE)
#endif /* __LP64__ */
				goto failclose;
		}
	}

#ifdef __LP64__
	if (fread(&ut32, sizeof(ut32), 1, fp) != 1)
#else /* __LP64__ */
	if (fread(&ut, sizeof(ut), 1, fp) != 1)
#endif /* __LP64__ */
		goto fail;

#ifdef __LP64__
	_utmpx32_64(&ut32, &ut);
#endif /* __LP64__ */
	return &ut;
failclose:
	(void)fclose(fp);
	fp = NULL;
fail:
	(void)memset(&ut, 0, sizeof(ut));
	return NULL;
}


struct utmpx *
getutxent()
{
	struct utmpx *ret;
	UTMPX_LOCK;
	ret = _getutxent();
	UTMPX_UNLOCK;
	return ret;
}

struct utmpx *
getutxid(const struct utmpx *utx)
{
	struct utmpx temp;
	const struct utmpx *ux;
	struct utmpx *ret;

	_DIAGASSERT(utx != NULL);

	if (utx->ut_type == EMPTY)
		return NULL;

	UTMPX_LOCK;
	/* make a copy as needed, and auto-fill if requested */
	ux = _utmpx_working_copy(utx, &temp, 1);
	if (!ux) {
		UTMPX_UNLOCK;
		return NULL;
	}

	ret = _getutxid(ux);
	UTMPX_UNLOCK;
	return ret;
}


static struct utmpx *
_getutxid(const struct utmpx *utx)
{

	do {
		if (ut.ut_type == EMPTY)
			continue;
		switch (utx->ut_type) {
		case EMPTY:
			return NULL;
		case RUN_LVL:
		case BOOT_TIME:
		case OLD_TIME:
		case NEW_TIME:
			if (ut.ut_type == utx->ut_type)
				return &ut;
			break;
		case INIT_PROCESS:
		case LOGIN_PROCESS:
		case USER_PROCESS:
		case DEAD_PROCESS:
			switch (ut.ut_type) {
			case INIT_PROCESS:
			case LOGIN_PROCESS:
			case USER_PROCESS:
			case DEAD_PROCESS:
				if (memcmp(ut.ut_id, utx->ut_id,
				    sizeof(ut.ut_id)) == 0)
					return &ut;
				break;
			default:
				break;
			}
			break;
		default:
			return NULL;
		}
	} while (_getutxent() != NULL);
	return NULL;
}


struct utmpx *
getutxline(const struct utmpx *utx)
{

	_DIAGASSERT(utx != NULL);

	UTMPX_LOCK;
	do {
		switch (ut.ut_type) {
		case EMPTY:
			break;
		case LOGIN_PROCESS:
		case USER_PROCESS:
			if (strncmp(ut.ut_line, utx->ut_line,
			    sizeof(ut.ut_line)) == 0) {
				UTMPX_UNLOCK;
				return &ut;
			}
			break;
		default:
			break;
		}
	} while (_getutxent() != NULL);
	UTMPX_UNLOCK;
	return NULL;
}


struct utmpx *
pututxline(const struct utmpx *utx)
{
	struct utmpx *ux;

	_DIAGASSERT(utx != NULL);

	if (utx == NULL) {
		errno = EINVAL;
		return NULL;
	}

	UTMPX_LOCK;
	if ((ux = _pututxline(utx)) != NULL && utfile_system) {
		_utmpx_asl(ux);	/* the equivalent of wtmpx and lastlogx */
#ifdef UTMP_COMPAT
		_write_utmp_compat(ux);
#endif /* UTMP_COMPAT */
	}
	UTMPX_UNLOCK;
	return ux;
}

__private_extern__ struct utmpx *
_pututxline(const struct utmpx *utx)
{
	struct utmpx temp, *u = NULL, *x;
	const struct utmpx *ux;
#ifdef __LP64__
	struct utmpx32 ut32;
#endif /* __LP64__ */
	struct flock fl;
#define gotlock		(fl.l_start >= 0)

	fl.l_start = -1; /* also means we haven't locked */
	if (utfile_system)
		if ((fp != NULL && readonly) || (fp == NULL && geteuid() != 0)) {
			errno = EPERM;
			return NULL;
		}

	if (fp == NULL) {
		(void)_getutxent();
		if (fp == NULL || readonly) {
			errno = EPERM;
			return NULL;
		}
	}

	/* make a copy as needed, and auto-fill if requested */
	ux = _utmpx_working_copy(utx, &temp, 0);
	if (!ux)
		return NULL;

	if ((x = _getutxid(ux)) == NULL) {
		_setutxent();
		if ((x = _getutxid(ux)) == NULL) {
			/*
			 * utx->ut_type has any original mask bits, while
			 * ux->ut_type has those mask bits removed.  If we
			 * are trying to record a dead process, and
			 * UTMPX_DEAD_IF_CORRESPONDING_MASK is set, then since
			 * there is no matching entry, we return NULL.
			 */
			if (ux->ut_type == DEAD_PROCESS &&
			    (utx->ut_type & UTMPX_DEAD_IF_CORRESPONDING_MASK)) {
				errno = EINVAL;
				return NULL;
			}
			/*
			 * Replace lockf() with fcntl() and a fixed start
			 * value.  We should already be at EOF.
			 */
			if ((fl.l_start = lseek(fileno(fp), 0, SEEK_CUR)) < 0)
				return NULL;
			fl.l_len = 0;
			fl.l_whence = SEEK_SET;
			fl.l_type = F_WRLCK;
			if (fcntl(fileno(fp), F_SETLKW, &fl) == -1)
				return NULL;
			if (fseeko(fp, (off_t)0, SEEK_END) == -1)
				goto fail;
		}
	}

	if (!gotlock) {
		/*
		 * utx->ut_type has any original mask bits, while
		 * ux->ut_type has those mask bits removed.  If we
		 * are trying to record a dead process, if
		 * UTMPX_DEAD_IF_CORRESPONDING_MASK is set, but the found
		 * entry is not a (matching) USER_PROCESS, then return NULL.
		 */
		if (ux->ut_type == DEAD_PROCESS &&
		    (utx->ut_type & UTMPX_DEAD_IF_CORRESPONDING_MASK) &&
		    x->ut_type != USER_PROCESS) {
			errno = EINVAL;
			return NULL;
		}
		/* we are not appending */
#ifdef __LP64__
		if (fseeko(fp, -(off_t)sizeof(ut32), SEEK_CUR) == -1)
#else /* __LP64__ */
		if (fseeko(fp, -(off_t)sizeof(ut), SEEK_CUR) == -1)
#endif /* __LP64__ */
			return NULL;
	}

#ifdef __LP64__
	_utmpx64_32(ux, &ut32);
	if (fwrite(&ut32, sizeof (ut32), 1, fp) != 1)
#else /* __LP64__ */
	if (fwrite(ux, sizeof (*ux), 1, fp) != 1)
#endif /* __LP64__ */
		goto fail;

	if (fflush(fp) == -1)
		goto fail;

	u = memcpy(&ut, ux, sizeof(ut));
	notify_post(UTMPX_CHANGE_NOTIFICATION);
fail:
	if (gotlock) {
		int save = errno;
		fl.l_type = F_UNLCK;
		if (fcntl(fileno(fp), F_SETLK, &fl) == -1)
			return NULL;
		errno = save;
	}
	return u;
}


/*
 * The following are extensions and not part of the X/Open spec.
 */
int
utmpxname(const char *fname)
{
	size_t len;

	UTMPX_LOCK;
	if (fname == NULL) {
		strcpy(utfile, _PATH_UTMPX);
		utfile_system = 1;
		_endutxent();
		UTMPX_UNLOCK;
		return 1;
	}

	len = strlen(fname);

	if (len >= sizeof(utfile)) {
		UTMPX_UNLOCK;
		return 0;
	}

	/* must end in x! */
	if (fname[len - 1] != 'x') {
		UTMPX_UNLOCK;
		return 0;
	}

	(void)strlcpy(utfile, fname, sizeof(utfile));
	_endutxent();
	utfile_system = 0;
	UTMPX_UNLOCK;
	return 1;
}

#ifdef LEGACY_UTMP_APIS
void
getutmp(const struct utmpx *ux, struct utmp *u)
{

	bzero(u, sizeof(*u));
	(void)memcpy(u->ut_name, ux->ut_user, sizeof(u->ut_name));
	(void)memcpy(u->ut_line, ux->ut_line, sizeof(u->ut_line));
	(void)memcpy(u->ut_host, ux->ut_host, sizeof(u->ut_host));
	u->ut_time = ux->ut_tv.tv_sec;
}

void
getutmpx(const struct utmp *u, struct utmpx *ux)
{

	bzero(ux, sizeof(*ux));
	(void)memcpy(ux->ut_user, u->ut_name, sizeof(u->ut_name));
	ux->ut_user[sizeof(u->ut_name)] = 0;
	(void)memcpy(ux->ut_line, u->ut_line, sizeof(u->ut_line));
	ux->ut_line[sizeof(u->ut_line)] = 0;
	(void)memcpy(ux->ut_host, u->ut_host, sizeof(u->ut_host));
	ux->ut_host[sizeof(u->ut_host)] = 0;
	ux->ut_tv.tv_sec = u->ut_time;
	ux->ut_tv.tv_usec = 0;
	ux->ut_pid = getpid();
	ux->ut_type = USER_PROCESS;
}
#endif /* LEGACY_UTMP_APIS */
