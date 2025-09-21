// Lightweight debug helpers for stderr tracing.
#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include "errors.h"

inline void
dbg_logf (const char *file, int line, const char *func, const char *fmt, ...)
{
  std::fprintf (stderr, "[dbg] %s:%d %s: ", file, line, func);
  va_list ap; va_start (ap, fmt);
  std::vfprintf (stderr, fmt, ap);
  va_end (ap);
  std::fprintf (stderr, "\n");
  std::fflush (stderr);
}

#define DBG_TRACE() dbg_logf (__FILE__, __LINE__, __func__, "enter")
// Accepts at least a format string; works with or without extra args
#define DBG(...) dbg_logf (__FILE__, __LINE__, __func__, __VA_ARGS__)

// ERROR: synonym for DBG for now
#define ERROR(...) DBG(__VA_ARGS__)

// CRASH: log (like DBG) and exit with provided code
#define CRASH(code, ...) do { \
    dbg_logf(__FILE__, __LINE__, __func__, __VA_ARGS__); \
    std::exit((int)(code)); \
} while(0)
