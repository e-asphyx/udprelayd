#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG
#include <stdio.h>
#define X_STRINGIFY_ARG(x) #x
#define X_STRINGIFY(x) X_STRINGIFY_ARG(x)
#define X_DBG(fmt, ...) fprintf(stderr, __FILE__ ":" X_STRINGIFY(__LINE__) ":%s(): " fmt, __func__, ##__VA_ARGS__)
#else
#define X_DBG(fmt, ...)
#endif

#if defined (DEBUG) && defined(__GNUC__) && (__GNUC__ > 2)
#define X_UNLIKELY(expr) ({int _val = (expr); if(_val) X_DBG(#expr "\n"); _val;})
#define X_LIKELY(expr) ({int _val = (expr); if(!_val) X_DBG("!(" #expr ")\n"); _val;})
#else
#define X_UNLIKELY(expr) (expr)
#define X_LIKELY(expr) (expr)
#endif

#endif /* DEBUG_H */
