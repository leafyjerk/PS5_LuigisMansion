/*
 * Luigi's Mansion for the PlayStation 5 - more libc names no module a title loads exports.
 *
 * As ps5/src/shims.c: ps5/tools/link-title.sh binds each name to its ps5dl_ answer here (its
 * `shims` list names every function below), kept local, since a title may export nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_DIR "/app0"

/* The working folder, as far as anything asks: the title's. libc's getcwd calls __getcwd,
 * which only libkernel_sys exports, a module a title does not load: the call jumps to address 0
 * (std::filesystem::current_path did, from the Hidden Mansion's overlay lookup, at the first
 * frame). A title cannot chdir either; chdir to its own folder succeeds. (The Super Mario Galaxy
 * port answers both the same way.) */
char *
ps5dl_getcwd(char *buffer, size_t size)
{
   if (buffer == NULL) {
      size = size ? size : sizeof(TITLE_DIR);
      buffer = malloc(size);
      if (buffer == NULL)
         return NULL;
   }
   if (size < sizeof(TITLE_DIR)) {
      errno = ERANGE;
      return NULL;
   }
   memcpy(buffer, TITLE_DIR, sizeof(TITLE_DIR));
   return buffer;
}

int
ps5dl_chdir(const char *path)
{
   if (path != NULL && (strcmp(path, TITLE_DIR) == 0 || strcmp(path, TITLE_DIR "/") == 0 || strcmp(path, ".") == 0))
      return 0;
   errno = EPERM;
   return -1;
}

/* The crash handler names the signal it caught. */
char *
ps5dl_strsignal(int sig)
{
   static char text[32];
   switch (sig) {
   case SIGSEGV:
      return "Segmentation fault";
   case SIGBUS:
      return "Bus error";
   case SIGILL:
      return "Illegal instruction";
   case SIGFPE:
      return "Floating point exception";
   case SIGABRT:
      return "Abort trap";
   default:
      snprintf(text, sizeof(text), "Signal %d", sig);
      return text;
   }
}
