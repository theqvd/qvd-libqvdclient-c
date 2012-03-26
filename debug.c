#include "qvdclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#ifdef ANDROID
#include <android/log.h>
#endif

static int qvd_global_debug_level = -1;
static FILE *global_debug_file = NULL; /* By default it becomes stderr see unistd.h */

void qvd_printf(const char *format, ...);

int _qvd_init_debug() {
  const char *log_file = getenv(DEBUG_FILE_ENV_VAR_NAME);
  global_debug_file = stdout; /* setting it to stderr */
  if (log_file) {
    if ((global_debug_file = fopen(log_file, "a")) == NULL) {
      global_debug_file = stdout;
      qvd_printf("Using stderr for debugging. Unable to open file %s, because of error: %s", log_file, strerror(errno));
    }
  }

  const char *debug_str = getenv(DEBUG_FLAG_ENV_VAR_NAME);
  if (debug_str) {
        errno = 0;
        long v = strtol(debug_str, NULL, 10);
        if ((v != LONG_MIN && v != LONG_MAX) || errno != ERANGE)
          return v;
  }
  return 0;
}


inline int get_debug_level(void) {
  if (qvd_global_debug_level < 0) {
    qvd_global_debug_level = _qvd_init_debug();
  }
  return qvd_global_debug_level;
}

inline void set_debug_level(int level)
{
  qvd_global_debug_level = level;
}
void _qvd_vprintf(const char *format, va_list args)
{
#ifdef ANDROID
  __android_log_vprint(get_debug_level(), "qvd", format args);
#else
  vfprintf(global_debug_file, format, args);
  fflush(global_debug_file);
#endif
}

void qvd_printf(const char *format, ...)
{
  if (get_debug_level() <= 0
#ifdef ANDROID
      || get_debug_level() >= ANDROID_LOG_SILENT
#endif
      )
    return;

  va_list args;
  va_start(args, format);
  _qvd_vprintf(format, args);
  va_end(args);

}
