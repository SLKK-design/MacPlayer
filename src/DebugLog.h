#ifndef __DEBUG_LOG__
#define __DEBUG_LOG__

/* Append a line to "MacPlayer Log" next to the application.
   Also the hook the network core (ported from Claude-86) logs through.
   Does nothing unless built with -DMACPLAYER_LOG=ON (CMakeLists.txt). */
void DebugLog(const char* func, const char* msg);

/* Write buffered log lines to disk, e.g. before showing an error */
void DebugLogFlush();

/* Turn failed assert()s and uncaught C++ exceptions, which otherwise quit
   the app without a word, into a logged alert. Call once at startup. */
void DebugLogInstallHandlers();

/* Run restore before the fatal alert, e.g. to bring back a hidden menu bar,
   which would otherwise stay hidden for the Finder */
void DebugLogOnFatal(void (*restore)());

#endif
