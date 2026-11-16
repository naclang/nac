#ifndef NAC_TIME_BUILTIN_H
#define NAC_TIME_BUILTIN_H

#include <stdbool.h>

#include "../runtime/value.h"

/* Date/time builtins (now() itself lives in builtin.c):
 *
 *   nowMs()                              milliseconds since 1970-01-01 UTC, as
 *                                        a float (the value does not fit a
 *                                        32-bit int; it prints in full)
 *   formatTime(ts [, fmt [, zone]])      timestamp -> text
 *   parseTime(text [, fmt])              text -> timestamp, or null if the
 *                                        text does not match
 *
 * `ts` is seconds since the epoch: an int, or a float for sub-second
 * precision (nowMs() / 1000). `zone` is "utc" (default), "local", or a fixed
 * offset such as "+03:00". Supported format specifiers:
 *   %Y %y %m %d %e %H %M %S %I %p %j %a %A %b %B %u %w
 *   %L (milliseconds) %s (epoch seconds) %z %Z %F (%Y-%m-%d) %T (%H:%M:%S) %%
 * The default format is ISO 8601: "%Y-%m-%dT%H:%M:%SZ". */
bool is_time_builtin(const char *name);
Value call_time_builtin(const char *name, Value *args, int arg_count);

#endif
