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

/* Create new relay */
relay_t *new_relay(const relay_config_t *config) {
    /* Resolve local address */
    char *local_addr = xstrdup(config->local_addr);
    char *local_host, *local_service;
    split_addr(local_addr, &local_host, &local_service);

    struct addrinfo hints, *res_local;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    
    int err;
    if(X_UNLIKELY((err = getaddrinfo(local_host[0] != '*' ? local_host : NULL, local_service, &hints, &res_local)) != 0)) {
        syslog(LOG_ERR, "%s: %s", local_host, gai_strerror(err));
        free(local_addr);
        return NULL;
    }
    free(local_addr);

    int fd = -1;
    struct addrinfo *r, local_ai;

    /* Choose first suitable local address */
    for(r = res_local; r != NULL; r = r->ai_next) {
        if(X_UNLIKELY((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) < 0)) {
            continue;
        }

        if(X_UNLIKELY(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)) {
            close(fd);
            syslog(LOG_ERR, "%s: %m", config->local_addr);
            return NULL;
        }

        if(X_UNLIKELY(bind(fd, r->ai_addr, r->ai_addrlen) < 0)) {
            close(fd);
            fd = -1;
            continue;
        } else {
            local_ai = *r;
            break;
        }
    }
    freeaddrinfo(res_local);

    if(fd < 0) {
        syslog(LOG_ERR, "%s: %m", config->local_addr);
        return NULL;
    }

    /* Resolve remote address */
    char *remote_addr = xstrdup(config->remote_addr);
    char *remote_host, *remote_service;
    split_addr(remote_addr, &remote_host, &remote_service);

    struct addrinfo *res_remote;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = local_ai.ai_family;
    hints.ai_socktype = local_ai.ai_socktype;
    hints.ai_protocol = local_ai.ai_protocol;

    if(X_UNLIKELY((err = getaddrinfo(remote_host, remote_service, &hints, &res_remote)) != 0)) {
        syslog(LOG_ERR, "%s: %s", remote_host, gai_strerror(err));
        close(fd);
        free(remote_addr);
        return NULL;
    }
    free(remote_addr);

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* New relay */
    relay_t *relay = calloc(1, sizeof(relay_t));

    relay->remote_sa = malloc(res_remote->ai_addrlen);
    memcpy(relay->remote_sa, res_remote->ai_addr, res_remote->ai_addrlen);
    relay->remote_sa_len = res_remote->ai_addrlen;
    freeaddrinfo(res_remote);

    relay->fd = fd;
    relay->local_addr = xstrdup(config->local_addr);
    relay->remote_addr = xstrdup(config->remote_addr);

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
    free(relay->remote_sa);
    free(relay->local_addr);
    free(relay->remote_addr);
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

        ssize_t sz = recv(relay->fd, relay->recv_buffer, BUF_SZ, 0);

        if(X_UNLIKELY(sz <= 0)) {
            if(sz < 0 && errno != EAGAIN) {
                syslog(LOG_ERR, "%s: %m", relay->remote_addr);
                return -1;
            }
        } else {
            relay->recv_size = sz;
        }
    }

    /* Write event */
    if(FD_ISSET(relay->fd, wfds)) {
        if(relay->send_size) {
            ssize_t sz = sendto(relay->fd, relay->send_buffer, relay->send_size, 0,
                relay->remote_sa, relay->remote_sa_len);

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
                relay->remote_sa, relay->remote_sa_len);

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