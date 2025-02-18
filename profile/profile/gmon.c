/*-
 * Copyright (c) 1983, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if !defined(lint) && defined(LIBC_SCCS)
static char rcsid[] = "$OpenBSD: gmon.c,v 1.8 1997/07/23 21:11:27 kstailey Exp $";
#endif

/*
 * This file is taken from Cygwin distribution. Please keep it in sync.
 * The differences should be within __MINGW32__ guard.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef __MINGW32__
#include <unistd.h>
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <gmon.h>
#ifdef	UNDER_CE
#include <windows.h>
#endif

#include <profil.h>

/* XXX needed? */
//extern char *minbrk __asm ("minbrk");

#ifdef __MINGW32__
#include <string.h>
#define bzero(ptr,size) memset (ptr, 0, size);
#endif

struct gmonparam _gmonparam = { GMON_PROF_OFF };

static int	s_scale;
/* see profil(2) where this is describe (incorrectly) */
#define		SCALE_1_TO_1	0x10000L

#define ERR(s) write((int)fileno(stderr), s, sizeof(s))

void	moncontrol __P((int));

static void *
fake_sbrk(int size)
{
    return malloc(size);
}

void
monstartup(lowpc, highpc)
	u_long lowpc;
	u_long highpc;
{
	register int o;
	char *cp;
	struct gmonparam *p = &_gmonparam;

	/*
	 * round lowpc and highpc to multiples of the density we're using
	 * so the rest of the scaling (here and in gprof) stays in ints.
	 */
	p->lowpc = ROUNDDOWN(lowpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->highpc = ROUNDUP(highpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->textsize = p->highpc - p->lowpc;
	p->kcountsize = p->textsize / HISTFRACTION;
	p->hashfraction = HASHFRACTION;
	p->fromssize = p->textsize / p->hashfraction;
	p->tolimit = p->textsize * ARCDENSITY / 100;
	if (p->tolimit < MINARCS)
		p->tolimit = MINARCS;
	else if (p->tolimit > MAXARCS)
		p->tolimit = MAXARCS;
	p->tossize = p->tolimit * sizeof(struct tostruct);

	cp = fake_sbrk(p->kcountsize + p->fromssize + p->tossize);
	if (cp == (char *)-1) {
#ifdef	UNDER_CE
		MessageBoxW(0, L"monstartup", L"out of memory", 0);
#else
		ERR("monstartup: out of memory\n");
#endif
		return;
	}

	/* zero out cp as value will be added there */
	bzero(cp, p->kcountsize + p->fromssize + p->tossize);

	p->tos = (struct tostruct *)cp;
	cp += p->tossize;
	p->kcount = (u_short *)cp;
	cp += p->kcountsize;
	p->froms = (u_short *)cp;

        /* XXX minbrk needed? */
	//minbrk = fake_sbrk(0);
	p->tos[0].link = 0;

	o = p->highpc - p->lowpc;
	if (p->kcountsize < o) {
#ifndef notdef
		s_scale = ((float)p->kcountsize / o ) * SCALE_1_TO_1;
#else /* avoid floating point */
		int quot = o / p->kcountsize;

		if (quot >= 0x10000)
			s_scale = 1;
		else if (quot >= 0x100)
			s_scale = 0x10000 / quot;
		else if (o >= 0x800000)
			s_scale = 0x1000000 / (o / (p->kcountsize >> 8));
		else
			s_scale = 0x1000000 / ((o << 8) / p->kcountsize);
#endif
	} else
		s_scale = SCALE_1_TO_1;

	moncontrol(1);
}

void
_mcleanup()
{
#ifdef	UNDER_CE
	HANDLE	fh;
	DWORD	written;
	wchar_t	*wproffile;
	wchar_t	nlw[MAX_PATH];
	int	sep, i, l;
#else
	int fd;
#endif
	int hz;
	int fromindex;
	int endfrom;
	u_long frompc;
	int toindex;
	struct rawarc rawarc;
	struct gmonparam *p = &_gmonparam;
	struct gmonhdr gmonhdr, *hdr;
	char *proffile;
#ifdef DEBUG
	int log, len;
	char dbuf[200];
#endif

	if (p->state == GMON_PROF_ERROR)
	{
#ifdef	UNDER_CE
#else
		ERR("_mcleanup: tos overflow\n");
#endif
	}

        hz = PROF_HZ;
	moncontrol(0);

#ifdef nope
	if ((profdir = getenv("PROFDIR")) != NULL) {
		extern char *__progname;
		char *s, *t, *limit;
		pid_t pid;
		long divisor;

		/* If PROFDIR contains a null value, no profiling
		   output is produced */
		if (*profdir == '\0') {
			return;
		}

		limit = buf + sizeof buf - 1 - 10 - 1 -
		    strlen(__progname) - 1;
		t = buf;
		s = profdir;
		while((*t = *s) != '\0' && t < limit) {
			t++;
			s++;
		}
		*t++ = '/';

		/*
		 * Copy and convert pid from a pid_t to a string.  For
		 * best performance, divisor should be initialized to
		 * the largest power of 10 less than PID_MAX.
		 */
		pid = getpid();
		divisor=10000;
		while (divisor > pid) divisor /= 10;	/* skip leading zeros */
		do {
			*t++ = (pid/divisor) + '0';
			pid %= divisor;
		} while (divisor /= 10);
		*t++ = '.';

		s = __progname;
		while ((*t++ = *s++) != '\0')
			;

		proffile = buf;
	} else {
		proffile = "gmon.out";
	}
#else
#ifdef	UNDER_CE
	/*
	 * Provide for different file names than just "gmon.out".
	 */
	l = GetModuleFileNameW(NULL, nlw, MAX_PATH);
	if (l == 0) {
		wproffile = L"gmon.out";
	} else {
		/* Find the last directory separator */
		for (sep=i=0; i < l; i++)
			if (nlw[i] == L'\\')
				sep = i;

		if (MAX_PATH < sep + 9)
			wproffile = L"gmon.out";
		else {
			/* Append gmon.out to it */
			nlw[++sep] = L'g';
			nlw[++sep] = L'm';
			nlw[++sep] = L'o';
			nlw[++sep] = L'n';
			nlw[++sep] = L'.';
			nlw[++sep] = L'o';
			nlw[++sep] = L'u';
			nlw[++sep] = L't';
			nlw[++sep] = 0;		/* Terminate it !! */

			wproffile = &nlw[0];
		}
	}
#else	/* not UNDER_CE */
	proffile = "gmon.out";
#endif
#endif

#ifdef	UNDER_CE
	fh = CreateFile(wproffile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
#else
	fd = open(proffile , O_CREAT|O_TRUNC|O_WRONLY|O_BINARY, 0666);
	if (fd < 0)
#endif
	{	/* Error handling : we couldn't open/create the file to write into */
#ifdef	UNDER_CE
		void	*buf;

		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM
				| FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, GetLastError(),
				0, (LPTSTR)&buf, 0, NULL);
		MessageBoxW(0, buf, L"Error", 0);
		LocalFree(buf);
#else
		perror( proffile );
#endif
		return;
	}
#ifdef DEBUG
	log = open("gmon.log", O_CREAT|O_TRUNC|O_WRONLY, 0664);
	if (log < 0) {
		perror("mcount: gmon.log");
		return;
	}
	len = sprintf(dbuf, "[mcleanup1] kcount 0x%x ssiz %d\n",
	    p->kcount, p->kcountsize);
	write(log, dbuf, len);
#endif
	hdr = (struct gmonhdr *)&gmonhdr;
	hdr->lpc = p->lowpc;
	hdr->hpc = p->highpc;
	hdr->ncnt = p->kcountsize + sizeof(gmonhdr);
	hdr->version = GMONVERSION;
	hdr->profrate = hz;
#ifdef	UNDER_CE
	WriteFile(fh, (char *)hdr, sizeof *hdr, &written, NULL);
	WriteFile(fh, p->kcount, p->kcountsize, &written, NULL);
#else
	write(fd, (char *)hdr, sizeof *hdr);
	write(fd, p->kcount, p->kcountsize);
#endif
	endfrom = p->fromssize / sizeof(*p->froms);
	for (fromindex = 0; fromindex < endfrom; fromindex++) {
		if (p->froms[fromindex] == 0)
			continue;

		frompc = p->lowpc;
		frompc += fromindex * p->hashfraction * sizeof(*p->froms);
		for (toindex = p->froms[fromindex]; toindex != 0;
		     toindex = p->tos[toindex].link) {
#ifdef DEBUG
			len = sprintf(dbuf,
			"[mcleanup2] frompc 0x%x selfpc 0x%x count %d\n" ,
				frompc, p->tos[toindex].selfpc,
				p->tos[toindex].count);
			write(log, dbuf, len);
#endif
			rawarc.raw_frompc = frompc;
			rawarc.raw_selfpc = p->tos[toindex].selfpc;
			rawarc.raw_count = p->tos[toindex].count;
#ifdef	UNDER_CE
			WriteFile(fh, &rawarc, sizeof(rawarc), &written, NULL);
#else
			write(fd, &rawarc, sizeof rawarc);
#endif
		}
	}
#ifdef	UNDER_CE
	(void)CloseHandle(fh);
#else
	close(fd);
#endif
}

/*
 * Control profiling
 *	profiling is what mcount checks to see if
 *	all the data structures are ready.
 */
void
moncontrol(mode)
	int mode;
{
	struct gmonparam *p = &_gmonparam;

	if (mode) {
		/* start */
		profil((char *)p->kcount, p->kcountsize, p->lowpc,
		    s_scale);
		p->state = GMON_PROF_ON;
	} else {
		/* stop */
		profil((char *)0, 0, 0, 0);
		p->state = GMON_PROF_OFF;
	}
}


