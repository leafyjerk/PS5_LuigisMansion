/*
 * Luigi's Mansion for the PlayStation 5 - the title's log, in klog and in a file.
 *
 * The game logs through printf and fprintf(stdout/stderr). On the console, printf, puts and
 * putchar write to the C library's own standard output whatever `stdout` names, which goes
 * nowhere: they are wrapped at link time (ps5/tools/link-title.sh: --wrap) to write to stderr.
 *
 * stderr itself becomes a pipe whose reader thread copies every byte to klog (the stream
 * platform_init made) and to /app0/userdata/log.txt, for a player who copies the title folder
 * back rather than watching klog. A write to the title folder is slow on the console, so the
 * file is buffered and flushed once a second, and at exit.
 *
 * SPDX-License-Identifier: MIT
 */
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int sceKernelUsleep(unsigned int microseconds);

static FILE *klog_stream;
static FILE *log_file;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void *
log_reader(void *arg)
{
   const int fd = (int)(long)arg;
   char buffer[4096];
   for (;;) {
      const ssize_t count = read(fd, buffer, sizeof(buffer));
      if (count <= 0) {
         sceKernelUsleep(1000);
         continue;
      }
      fwrite(buffer, 1, (size_t)count, klog_stream);
      fflush(klog_stream);
      pthread_mutex_lock(&log_lock);
      if (log_file != NULL)
         fwrite(buffer, 1, (size_t)count, log_file);
      pthread_mutex_unlock(&log_lock);
   }
   return NULL;
}

static void *
log_flusher(void *unused)
{
   (void)unused;
   for (;;) {
      sceKernelUsleep(1000000);
      log_flush();
   }
   return NULL;
}

void
log_flush(void)
{
   pthread_mutex_lock(&log_lock);
   if (log_file != NULL)
      fflush(log_file);
   pthread_mutex_unlock(&log_lock);
}

void
log_start(const char *path)
{
   static char file_buffer[1 << 20];
   int fds[2];
   pthread_t thread;

   mkdir("/app0/userdata", 0777);
   log_file = fopen(path, "w");
   if (log_file != NULL)
      setvbuf(log_file, file_buffer, _IOFBF, sizeof(file_buffer));
   klog_stream = stderr;
   if (pipe(fds) != 0)
      return;
   FILE *pipe_stream = fdopen(fds[1], "w");
   if (pipe_stream == NULL)
      return;
   setvbuf(pipe_stream, NULL, _IOLBF, 0);
   pthread_create(&thread, NULL, log_reader, (void *)(long)fds[0]);
   pthread_detach(thread);
   pthread_create(&thread, NULL, log_flusher, NULL);
   pthread_detach(thread);
   stderr = pipe_stream;
   stdout = pipe_stream;
}

/* ---------------------------------------------------------------- the exit */

/* A title may not exit: the exit system call ends it with SIGSYS, and the console reports a
 * crash with nothing of why. exit, _exit and _Exit are wrapped (link-title.sh): what is in the
 * pipe reaches the file, then the shell is asked to close the title, as when main returns
 * (platform.c). The game's abort handler (crash_handler.cpp onAbort) prints its report and ends
 * in _Exit, so a fatal error's message is in log.txt. */
void catchReturnFromMain(int status);

static void
log_drain(void)
{
   fflush(stdout);
   fflush(stderr);
   sceKernelUsleep(300000); /* the reader thread copies what is left in the pipe */
   log_flush();
}

static void
title_exit(int status, const char *how)
{
   fprintf(stderr, "[ps5] %s(%d): closing the title\n", how, status);
   log_drain();
   catchReturnFromMain(status);
}

void __wrap_exit(int status) { title_exit(status, "exit"); }
void __wrap__exit(int status) { title_exit(status, "_exit"); }
void __wrap__Exit(int status) { title_exit(status, "_Exit"); }

/* The game's crash handler re-raises a fatal signal after writing its report: the file gets the
 * report first. */
int __real_raise(int sig);
int
__wrap_raise(int sig)
{
   fprintf(stderr, "[ps5] raise(%d)\n", sig);
   log_drain();
   return __real_raise(sig);
}

/* ------------------------------------------------------------------ stdout */

int
__wrap_printf(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   const int written = vfprintf(stderr, format, args);
   va_end(args);
   return written;
}

int
__wrap_vprintf(const char *format, va_list args)
{
   return vfprintf(stderr, format, args);
}

int
__wrap_puts(const char *text)
{
   return fprintf(stderr, "%s\n", text) < 0 ? EOF : 0;
}

int
__wrap_putchar(int character)
{
   return fputc(character, stderr);
}
