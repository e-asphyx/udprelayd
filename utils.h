#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>

#ifndef MIN
#   define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#   define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

char *strdup_printf(const char *format, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
char *strconcat(const char *src, ...);
char *strreplace(char **dest, const char *str);
char *strappend(char *dst, const char *src);
int str_index(const char *array, const char *str);
char *xstrdup(const char *str);

/* process control */
int spawn_and_wait(char *const argv[]);
int pipe_open(char *const argv[], pid_t *child_pid);
int spawn_bg(char* const *argv, char* const *extra_env);
int xdaemon(const char *pid_file);

#if defined _WIN32 || defined __CYGWIN__
    #ifdef __GNUC__
        #define DLL_PUBLIC __attribute__ ((dllexport))
    #else
        #define DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
    #define DLL_LOCAL
#else
    #if __GNUC__ >= 4
        #define DLL_PUBLIC __attribute__ ((visibility ("default")))
        #define DLL_LOCAL  __attribute__ ((visibility ("hidden")))
    #else
        #define DLL_PUBLIC
        #define DLL_LOCAL
    #endif
#endif

#endif /* UTILS_H */
