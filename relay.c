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

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "relay.h"
#include "utils.h"
#include "clist.h"
#include "debug.h"

#define BUF_SZ 65536

struct _queue_t {
    void *buffer;
    size_t length;

    queue_t *_prev;
    queue_t *_next;
};

static bool relay_queued(relay_t *relay);

static void split_addr(char *src, char **host, char **service) {
    *host = src;
    if((*service = strchr(src, ':')) != NULL) *((*service)++) = '\0';
}

#ifdef DEBUG
static void dump_sockaddr(const struct sockaddr *sa) {
    char buf[INET6_ADDRSTRLEN];
    if(sa->sa_family == AF_INET6) {
        inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr, buf, sizeof(buf));
    } else {
        inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr, buf, sizeof(buf));
    }

    fprintf(stderr, "%s:%u\n", buf, (unsigned int)ntohs(((struct sockaddr_in*)sa)->sin_port));
}
#else
#   define dump_sockaddr(sa)
#endif

/* Create new relay */
relay_t *new_relay(const relay_config_t *config) {
    /* Resolve local address */
    char *local_addr = NULL, *local_host = NULL, *local_service = NULL;
    char *remote_addr = NULL, *remote_host = NULL, *remote_service = NULL;

    if(config->local_addr) {
        local_addr = xstrdup(config->local_addr);
        split_addr(local_addr, &local_host, &local_service);
    }

    if(config->remote_addr) {
        remote_addr = xstrdup(config->remote_addr);
        split_addr(remote_addr, &remote_host, &remote_service);
    }

    struct addrinfo hints, *res_local;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = local_host ? AI_PASSIVE : 0;
    
    int err;
    if(X_UNLIKELY((err = getaddrinfo(local_host ? (local_host[0] != '*' ? local_host : NULL) : remote_host,
            local_service ? local_service : remote_service,
            &hints, &res_local)) != 0)) {

        syslog(LOG_ERR, "%s: %s", config->local_addr ? config->local_addr : config->remote_addr, gai_strerror(err));
        if(local_addr) free(local_addr);
        if(remote_addr) free(remote_addr);
        return NULL;
    }

    int fd = -1;
    struct addrinfo *r, local_ai;
    sockaddr_t remote_sa;
    socklen_t remote_sa_len = 0;

    /* Choose first suitable address */
    for(r = res_local; r != NULL; r = r->ai_next) {
        if(X_UNLIKELY((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) < 0)) {
            continue;
        }

        X_DBG("fd[%d] af=AF_INET%s\n", fd, r->ai_family == AF_INET6 ? "6" : "");

        if(!config->local_addr) {
            memcpy(&remote_sa, r->ai_addr, r->ai_addrlen);
            remote_sa_len = r->ai_addrlen;
            break;
        }

        if(X_UNLIKELY(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)) {
            close(fd);
            if(local_addr) free(local_addr);
            if(remote_addr) free(remote_addr);
            syslog(LOG_ERR, "%m");
            return NULL;
        }

        if(X_UNLIKELY(bind(fd, r->ai_addr, r->ai_addrlen) < 0)) {
            close(fd);
            fd = -1;
            continue;
        } else {
            X_DBG("fd[%d] bind ", fd);
            dump_sockaddr(r->ai_addr);

            local_ai = *r;
            break;
        }
    }
    freeaddrinfo(res_local);

    if(fd < 0) {
        syslog(LOG_ERR, "%s: %m", config->local_addr ? config->local_addr : config->remote_addr);
        if(local_addr) free(local_addr);
        if(remote_addr) free(remote_addr);
        return NULL;
    }

    /* Resolve remote address */
    if(config->remote_addr && config->local_addr) {
        struct addrinfo *res_remote;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = local_ai.ai_family;
        hints.ai_socktype = local_ai.ai_socktype;
        hints.ai_protocol = local_ai.ai_protocol;

        if(X_UNLIKELY((err = getaddrinfo(remote_host, remote_service, &hints, &res_remote)) != 0)) {
            syslog(LOG_ERR, "%s: %s", remote_host, gai_strerror(err));
            close(fd);
            if(local_addr) free(local_addr);
            if(remote_addr) free(remote_addr);
            return NULL;
        }

        memcpy(&remote_sa, res_remote->ai_addr, res_remote->ai_addrlen);
        remote_sa_len = res_remote->ai_addrlen;

        freeaddrinfo(res_remote);
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* New relay */
    relay_t *relay = calloc(1, sizeof(relay_t));

    if(remote_sa_len) {
        X_DBG("fd[%d] remote ", fd);
        dump_sockaddr(&remote_sa.sa);

        memcpy(&relay->remote_sa, &remote_sa, remote_sa_len);
        relay->remote_sa_len = remote_sa_len;
    } else {
        relay->dynamic_out_addr = true;
    }

    relay->fd = fd;
    if(config->local_addr) relay->local_addr = xstrdup(config->local_addr);
    if(config->remote_addr) relay->remote_addr = xstrdup(config->remote_addr);

    if(local_addr) free(local_addr);
    if(remote_addr) free(remote_addr);

    return relay;
}

void free_relay(relay_t *relay) {
    close(relay->fd);

    if(relay->send_buffer) free(relay->send_buffer);
    if(relay->recv_buffer) free(relay->recv_buffer);

    queue_t *q;
    while((q = relay->queue) != NULL) {
        CLIST_DEL(relay->queue, q);
        free(q->buffer);
        free(q);
    }
    if(relay->local_addr) free(relay->local_addr);
    if(relay->remote_addr) free(relay->remote_addr);
    free(relay);
}

static bool relay_queued(relay_t *relay) {
    return relay->queue || relay->send_size;
}

void relay_fd_set(relay_t *relay, fd_set *rfds, fd_set *wfds) {
    if(relay_queued(relay)) FD_SET(relay->fd, wfds);
    if(!relay->recv_size) FD_SET(relay->fd, rfds);
}

ssize_t relay_enqueue(relay_t *relay, const void *buffer, size_t length) {
    if(!relay->remote_sa_len) {
        /* Drop */
        return 0;
    }

    /* TODO try to send immediately */

    /* Add to queue */
    if(relay_queued(relay)) {
        queue_t *item = malloc(sizeof(queue_t));
        item->buffer = malloc(length);
        memcpy(item->buffer, buffer, length);
        item->length = length;

        CLIST_ADD_LAST(relay->queue, item);
    } else {
        if(!relay->send_buffer) {
            relay->send_buffer_size = length;
            relay->send_buffer = malloc(length);
        } else if(length > relay->send_buffer_size) {
            free(relay->send_buffer);
            relay->send_buffer_size = length + length / 2;
            relay->send_buffer = malloc(relay->send_buffer_size);
        }

        memcpy(relay->send_buffer, buffer, length);
        relay->send_size = length;
    }

    return 0;
}

/* Returns pointer to internal buffer! */
ssize_t relay_receive(relay_t *relay, void **buffer) {
    if(!relay->recv_size) return 0;

    ssize_t sz = relay->recv_size;
    *buffer = relay->recv_buffer;
    relay->recv_size = 0;
    return sz;
}

int relay_handle(relay_t *relay, const fd_set *rfds, const fd_set *wfds) {
    /* Read event */
    if(FD_ISSET(relay->fd, rfds) && !relay->recv_size) {
        if(!relay->recv_buffer) {
            relay->recv_buffer = malloc(BUF_SZ);
        }

        sockaddr_t sa;
        socklen_t salen;

        ssize_t sz = recvfrom(relay->fd, relay->recv_buffer, BUF_SZ, 0, &sa.sa, &salen);

        if(X_UNLIKELY(sz <= 0)) {
            if(sz < 0 && errno != EAGAIN) {
                syslog(LOG_ERR, "%s: %m", relay->remote_addr);
                return -1;
            }
        } else {
            relay->recv_size = sz;
            /* Update out address */
            if(relay->dynamic_out_addr) {
                memcpy(&relay->remote_sa, &sa, salen);
                relay->remote_sa_len = salen;
                X_DBG("Recv from ");
                dump_sockaddr(&sa.sa);
            }
        }
    }

    /* Write event */
    if(FD_ISSET(relay->fd, wfds)) {
        if(relay->send_size) {
            ssize_t sz = sendto(relay->fd, relay->send_buffer, relay->send_size, 0,
                &relay->remote_sa.sa, relay->remote_sa_len);

            if(X_UNLIKELY(!sz || (sz < 0 && errno != EMSGSIZE))) {
                if(!sz || (sz < 0 && errno != EAGAIN)) {
                    if(sz < 0) syslog(LOG_ERR, "%s: %m", relay->remote_addr);
                    return -1;
                }
            } else {
                relay->send_size = 0;
            }
        } else if(relay->queue) {
            queue_t *item = relay->queue;

            ssize_t sz = sendto(relay->fd, item->buffer, item->length, 0,
                &relay->remote_sa.sa, relay->remote_sa_len);

            if(X_UNLIKELY(!sz || (sz < 0 && errno != EMSGSIZE))) {
                if(!sz || (sz < 0 && errno != EAGAIN)) {
                    if(sz < 0) syslog(LOG_ERR, "%s: %m", relay->remote_addr);
                    return -1;
                }
            } else {
                CLIST_DEL(relay->queue, item);
                free(item->buffer);
                free(item);
            }
        }
    }

    return 0;
}