/* DebugLog.cpp — "MacPlayer Log", a text file next to the application.

   Off unless MacPlayer is built with MACPLAYER_LOG (cmake -DMACPLAYER_LOG=ON);
   otherwise DebugLog() does nothing and no file is written. The fatal-error
   handlers below work either way.

   The network core logs each step through DebugLog() (Claude-86 shows these
   in a console window). Writing them to a file means a run that ends badly
   can be read back afterwards, from the host too when MacPlayer runs from a
   Basilisk II shared folder. The log restarts at every launch and stops
   growing at kMaxLogSize. The file stays open (opening and closing it per
   line meant a disk write per line, slow on a real Mac); it is flushed every
   couple of seconds and whenever an error is shown, so a crash loses at most
   the last moments, and closed by the system when MacPlayer quits.

   newlib's assert(), a failed allocation and std::terminate() all end the
   app; on Retro68 abort() returns to the Finder silently. The handlers
   below log the reason and show it in an alert first. MacPlayer builds
   with -fno-exceptions like Claude-86, so nothing here throws or catches. */

#include <Files.h>
#include <Events.h>
#include <MacMemory.h>
#include <Dialogs.h>
#include <Script.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exception>
#include <new>
#include "DebugLog.h"

const char* BuildStamp();  /* BuildStamp.cpp, recompiled on every build */

static const unsigned char kLogName[] = "\pMacPlayer Log";
static const long kMaxLogSize = 256L * 1024L;
enum { kFatalAlert = 1030 };  /* resources.r: "Sorry, something went wrong.\r^0" */

static bool  sInited  = false;
static short sVRefNum = 0;
static long  sDirID   = 0;
static short sLogRef  = 0;   /* open for the whole run; 0 = no log */
static long  sLogSize = 0;
static unsigned long sLastFlush = 0;
enum { kFlushTicks = 120 };   /* ~2 s */

static void LogInit()
{
	sInited = true;

	/* At launch the default directory is the application's folder */
	if (HGetVol(NULL, &sVRefNum, &sDirID) != noErr)
		return;

	FSSpec spec;
	OSErr err = FSMakeFSSpec(sVRefNum, sDirID, kLogName, &spec);
	if (err == noErr)
		FSpDelete(&spec);  /* fresh log for every launch */
	if (err != noErr && err != fnfErr)
		return;

	err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
	if ((err == noErr || err == dupFNErr) &&  /* dupFNErr: old log couldn't be deleted */
	    FSpOpenDF(&spec, fsRdWrPerm, &sLogRef) == noErr)
		SetEOF(sLogRef, 0);
	else
		sLogRef = 0;
}

static void LogWrite(const char* text, long len)
{
	if (!sInited)
		LogInit();
	if (sLogRef == 0 || sLogSize > kMaxLogSize)
		return;

	long count = len;
	FSWrite(sLogRef, &count, text);
	sLogSize += len;

	if (TickCount() - sLastFlush > kFlushTicks)
		DebugLogFlush();
}

void DebugLogFlush()
{
	if (sLogRef == 0)
		return;

	ParamBlockRec pb;
	memset(&pb, 0, sizeof(pb));
	pb.ioParam.ioRefNum = sLogRef;
	PBFlushFileSync(&pb);  /* this file only, cheaper than FlushVol */
	sLastFlush = TickCount();
}

void DebugLog(const char* func, const char* msg)
{
#ifndef MACPLAYER_LOG
	(void)func;
	(void)msg;
	return;
#endif
	/* ticks, free heap and remaining stack (KB), then the message */
	char line[256];
	int n = snprintf(line, sizeof(line) - 1, "%7lu %5ldK %4ldK %-14s %s",
	                 (unsigned long)TickCount(), FreeMem() / 1024,
	                 StackSpace() / 1024, func ? func : "", msg ? msg : "");
	if (n < 0)
		return;
	if (n > (int)sizeof(line) - 2)
		n = sizeof(line) - 2;
	line[n++] = '\r';
	LogWrite(line, n);
}

/* Log hooks the C files of the network core call */
extern "C" void uecc_con_log(const char* tag, const char* msg) { DebugLog(tag, msg); }
extern "C" void fast_alloc_log(const char* func, const char* msg) { DebugLog(func, msg); }

static void (*sRestoreOnFatal)() = NULL;

void DebugLogOnFatal(void (*restore)())
{
	sRestoreOnFatal = restore;
}

/* Log, show the reason, then quit the way abort() would have */
static void FatalError(const char* what, const char* detail)
{
	DebugLog(what, detail);
	DebugLogFlush();
	if (sRestoreOnFatal)
		sRestoreOnFatal();

	char text[256];
	snprintf(text, sizeof(text), "%s: %s", what, detail);
	Str255 ptext;
	size_t len = strlen(text);
	if (len > 255) len = 255;
	ptext[0] = (unsigned char)len;
	memcpy(ptext + 1, text, len);

	InitCursor();
	ParamText(ptext, "\p", "\p", "\p");
	StopAlert(kFatalAlert, NULL);
	ExitToShell();
}

/* Replaces newlib's version (app objects link before libc) */
extern "C" void __assert_func(const char* file, int line, const char* func, const char* expr)
{
	const char* base = file ? strrchr(file, '/') : NULL;
	char detail[200];
	snprintf(detail, sizeof(detail), "%s line %d, %s: %s",
	         base ? base + 1 : (file ? file : "?"), line, func ? func : "?", expr ? expr : "?");
	FatalError("Assertion failed", detail);
}

/* Replaces libstdc++'s operator new (malloc is NewPtr here). Without
   exceptions a failed allocation can't be recovered, so log what failed
   (size asked for, state of the heap) and stop with an alert. */
void* operator new(std::size_t size)
{
	if (size == 0)
		size = 1;
	void* p = malloc(size);
	if (!p)
	{
		char detail[120];
		snprintf(detail, sizeof(detail), "new(%lu) failed: FreeMem=%ld MaxBlock=%ld MemError=%d",
		         (unsigned long)size, FreeMem(), MaxBlock(), (int)MemError());
		FatalError("Out of memory", detail);
	}
	return p;
}

void operator delete(void* p) noexcept
{
	free(p);
}

/* A library call threw (e.g. std::out_of_range); with -fno-exceptions
   nothing in MacPlayer can catch it */
static void OnTerminate()
{
	FatalError("Uncaught exception", "a C++ library call threw");
}

void DebugLogInstallHandlers()
{
	std::set_terminate(OnTerminate);

	char detail[96];
	snprintf(detail, sizeof(detail), "build %s, MaxBlock=%ldK StackSpace=%ldK",
	         BuildStamp(), MaxBlock() / 1024, StackSpace() / 1024);
	DebugLog("MacPlayer", detail);
}
