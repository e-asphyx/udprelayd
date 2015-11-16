#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "config.h"
#include "clist.h"

typedef struct _relay_t relay_t;
typedef struct _queue_t queue_t;

struct _relay_t {
    int fd;

    struct sockaddr *remote_sa;
    socklen_t remote_sa_len;

    char *local_addr;
    char *remote_addr;

    queue_t *queue;

    /* Reusable buffer for 1st item in send queue */
    void *send_buffer;
    size_t send_buffer_size;
    size_t send_size;

    /* Receive buffer */
    void *recv_buffer;
    size_t recv_size;

    relay_t *_prev;
    relay_t *_next;
};

relay_t *new_relay(const relay_config_t *config);
void free_relay(relay_t *relay);
ssize_t relay_enqueue(relay_t *relay, const void *buffer, size_t length);
ssize_t relay_receive(relay_t *relay, void **buffer);
int relay_handle(relay_t *relay, const fd_set *rfds, const fd_set *wfds);
void relay_fd_set(relay_t *relay, fd_set *rfds, fd_set *wfds);

#endif