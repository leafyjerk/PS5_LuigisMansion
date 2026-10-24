/*
 * Luigi's Mansion for the PlayStation 5 - the title's log (log.c).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* stdout and stderr into klog and into the file at path (buffered, flushed every second). */
void log_start(const char *path);
/* Writes what the file's buffer holds (at exit). */
void log_flush(void);

#ifdef __cplusplus
}
#endif
