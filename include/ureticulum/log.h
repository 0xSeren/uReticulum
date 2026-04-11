#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <string>

/* Source-compatible with microReticulum's Log.h so ported files need no rewrite.
 * The level macros are guarded so test frameworks (doctest, etc.) that define
 * INFO/WARN/etc. of their own can be included alongside without redefinition. */

#define RNS_LOG_BUFFER_SIZE 512

#ifndef LOG
  #define LOG(msg, level)       (RNS::log(msg, level))
  #define LOGF(level, msg, ...) (RNS::logf(level, msg, __VA_ARGS__))
#endif

#ifndef CRITICAL
  #define CRITICAL(msg)       (RNS::log(msg, RNS::LOG_CRITICAL))
  #define CRITICALF(msg, ...) (RNS::logf(RNS::LOG_CRITICAL, msg, __VA_ARGS__))
#endif
#ifndef ERROR
  #define ERROR(msg)          (RNS::log(msg, RNS::LOG_ERROR))
  #define ERRORF(msg, ...)    (RNS::logf(RNS::LOG_ERROR, msg, __VA_ARGS__))
#endif
#ifndef WARNING
  #define WARNING(msg)        (RNS::log(msg, RNS::LOG_WARNING))
  #define WARNINGF(msg, ...)  (RNS::logf(RNS::LOG_WARNING, msg, __VA_ARGS__))
#endif
#ifndef NOTICE
  #define NOTICE(msg)         (RNS::log(msg, RNS::LOG_NOTICE))
  #define NOTICEF(msg, ...)   (RNS::logf(RNS::LOG_NOTICE, msg, __VA_ARGS__))
#endif
#ifndef INFO
  #define INFO(msg)           (RNS::log(msg, RNS::LOG_INFO))
  #define INFOF(msg, ...)     (RNS::logf(RNS::LOG_INFO, msg, __VA_ARGS__))
#endif
#ifndef VERBOSE
  #define VERBOSE(msg)        (RNS::log(msg, RNS::LOG_VERBOSE))
  #define VERBOSEF(msg, ...)  (RNS::logf(RNS::LOG_VERBOSE, msg, __VA_ARGS__))
#endif

#ifndef DEBUG
  #ifndef NDEBUG
    #define DEBUG(msg)        (RNS::log(msg, RNS::LOG_DEBUG))
    #define DEBUGF(msg, ...)  (RNS::logf(RNS::LOG_DEBUG, msg, __VA_ARGS__))
  #else
    #define DEBUG(ignore)     ((void)0)
    #define DEBUGF(...)       ((void)0)
  #endif
#endif

#ifndef TRACE
  #ifndef NDEBUG
    #define TRACE(msg)        (RNS::log(msg, RNS::LOG_TRACE))
    #define TRACEF(msg, ...)  (RNS::logf(RNS::LOG_TRACE, msg, __VA_ARGS__))
  #else
    #define TRACE(ignore)     ((void)0)
    #define TRACEF(...)       ((void)0)
  #endif
#endif

#ifndef MEM
  #if defined(RNS_MEM_LOG) && !defined(NDEBUG)
    #define MEM(msg)          (RNS::log(msg, RNS::LOG_MEM))
    #define MEMF(msg, ...)    (RNS::logf(RNS::LOG_MEM, msg, __VA_ARGS__))
  #else
    #define MEM(ignore)       ((void)0)
    #define MEMF(...)         ((void)0)
  #endif
#endif

namespace RNS {

    enum LogLevel {
        LOG_NONE     = 0,
        LOG_CRITICAL = 1,
        LOG_ERROR    = 2,
        LOG_WARNING  = 3,
        LOG_NOTICE   = 4,
        LOG_INFO     = 5,
        LOG_VERBOSE  = 6,
        LOG_DEBUG    = 7,
        LOG_TRACE    = 8,
        LOG_MEM      = 9,
    };

    using log_callback = void (*)(const char* msg, LogLevel level);

    const char* getLevelName(LogLevel level);
    const char* getTimeString();

    void     loglevel(LogLevel level);
    LogLevel loglevel();

    void set_log_callback(log_callback on_log = nullptr);

    void doLog(LogLevel level, const char* msg);
    void doHeadLog(LogLevel level, const char* msg);

    inline void log(const char* msg, LogLevel level = LOG_NOTICE)        { doLog(level, msg); }
    inline void log(const std::string& msg, LogLevel level = LOG_NOTICE) { doLog(level, msg.c_str()); }
    inline void logf(LogLevel level, const char* msg, ...) {
        va_list vl; va_start(vl, msg);
        char buf[RNS_LOG_BUFFER_SIZE];
        vsnprintf(buf, sizeof(buf), msg, vl);
        va_end(vl);
        doLog(level, buf);
    }

    inline void head(const char* msg, LogLevel level = LOG_NOTICE)        { doHeadLog(level, msg); }
    inline void head(const std::string& msg, LogLevel level = LOG_NOTICE) { doHeadLog(level, msg.c_str()); }
    inline void headf(LogLevel level, const char* msg, ...) {
        va_list vl; va_start(vl, msg);
        char buf[RNS_LOG_BUFFER_SIZE];
        vsnprintf(buf, sizeof(buf), msg, vl);
        va_end(vl);
        doHeadLog(level, buf);
    }

}
