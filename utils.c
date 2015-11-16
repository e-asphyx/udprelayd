/*
The MIT License (MIT)

Copyright (c) 2015 Eugene Zagidullin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "utils.h"

#define MAX_STR_LEN 4096

/* Because strdup is deprecated in strict C99 */
char *xstrdup(const char *str) {
    size_t sz = strlen(str) + 1;
    return (char*)memcpy(malloc(sz), str, sz);
}

/* Version of snprintf with dynamic allocation */
char *strdup_printf(const char *format, ...) {
    char buf[MAX_STR_LEN];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, sizeof(buf) - 1, format, ap);
    buf[sizeof(buf) - 1] = '\0';
    va_end(ap);

    return xstrdup(buf);
}

/* free(*dest) if necessary */
char *strreplace(char **dest, const char *str) {
    if(*dest) free(*dest);
    *dest = xstrdup(str);
    return *dest;
}

/* Concatenate strings and return dynamically allocated result */
char *strconcat(const char *src, ...) {
    va_list ap;

    int len = strlen(src);
    char *dst = malloc(len + 1);
    memcpy(dst, src, len + 1);

    va_start(ap, src);
    const char *s;
    while((s = va_arg(ap, const char *)) != NULL) {
        int l = strlen(s);
        dst = realloc(dst, len + l + 1);
        memcpy(dst + len, s, l + 1);
        len += l;
    }
    va_end(ap);

    return dst;
}

/* Append string to dynamically allocated destination (can be NULL) */
char *strappend(char *dst, const char *src) {
    int src_len = strlen(src);
    int dst_len = dst ? strlen(dst) : 0;

    dst = realloc(dst, dst_len + src_len + 1);
    memcpy(dst + dst_len, src, src_len + 1);

    return dst;
}

/*
Returns index of str in array or -1. array is 0-delimited list of lexemes. Example:
str_index("apple\0orange\0banana\0", "orange") returns 1
 */
int str_index(const char *array, const char *str) {
    int i = 0;
    while(*array) {
        const char *c = str;
        while(*array && *array == *c) {
            array++;
            c++;
        };
        if(*array == *c) return i;

        /* skip */
        while(*(array++));
        i++;
    }

    return -1;
}

/* Some process control functions */
int spawn_and_wait(char *const argv[]) {
    pid_t pid = fork();
    if(pid == 0) {
        /* child process */
        execvp(argv[0], argv);

        fprintf(stderr, "can't execute '%s'\n", argv[0]);
        fflush(stderr);
        _exit(EXIT_FAILURE);
    }

    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int pipe_open(char *const argv[], pid_t *child_pid) {
    int pipefd[2];
    /* create pipe */
    if(pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if(pid == 0) {
        /* child process */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        execvp(argv[0], argv);

        fprintf(stderr, "can't execute '%s'\n", argv[0]);
        fflush(stderr);
        _exit(EXIT_FAILURE);
    }
    if(child_pid) *child_pid = pid;
    close(pipefd[1]);

    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    return pipefd[0];
}

/* Perform double fork */
int spawn_bg(char* const *argv, char* const *extra_env) {
    int pipefd[2];
    /* create notification pipe */
    if(pipe(pipefd) < 0) return -1;

    pid_t child_pid = fork();
    if(child_pid < 0) return -1;

    if(!child_pid) {
        /* child */
        close(pipefd[0]);

        pid_t grandson_pid = fork();
        if(grandson_pid < 0) _exit(EXIT_FAILURE);
        if(!grandson_pid) {
            /* grandson */
            chdir("/");
            int fd = open("/dev/null", O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);

            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

            /* modify environment */
            if(extra_env) {
                while(*extra_env) putenv(*(extra_env++));
            }

            /* exec */
            execvp(argv[0], argv);

            fprintf(stderr, "can't execute '%s'\n", argv[0]);
            fflush(stderr);

            /* perform dummy write on error */
            char dummybyte = 0;
            write(pipefd[1], &dummybyte, 1);
        }

        _exit(EXIT_SUCCESS);
    }
    close(pipefd[1]);

    /* wait for child */
    int status;
    waitpid(child_pid, &status, 0);
    if(WEXITSTATUS(status)) {
        /* second fork failed */
        close(pipefd[0]);
        return -1;
    }

    /* wait for grandson exec */
    char dummybyte;
    ssize_t rd = read(pipefd[0], &dummybyte, 1);
    close(pipefd[0]);

    if(rd) {
        /* something written or read error */
        return -1;
    }

    return 0;
}

/* daemonize */
int xdaemon(const char *pid_file) {
    pid_t pid = fork();
    if(pid < 0) return -1;

    /* parent */
    if(pid > 0) {
        if(pid_file) {
            FILE *fp = fopen(pid_file, "w");
            if(!fp) {
                perror(pid_file);
            } else {
                fprintf(fp, "%ld\n", (long)pid);
                fclose(fp);
            }
        }

        exit(EXIT_SUCCESS);
    }

    /* child process */
    setsid();
    chdir("/");

    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    return 0;
}
