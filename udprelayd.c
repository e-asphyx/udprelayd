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
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <syslog.h>
#include <libgen.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>

#include "debug.h"
#include "config.h"
#include "utils.h"
#include "relay.h"
#include "seen_lookup.h"

typedef struct _udprelay_t udprelay_t;
typedef struct _header_t header_t;

struct _header_t {
    uint16_t seq;
#ifdef DEBUG
    uint16_t pkt_num;
    uint16_t pkts_in_series;
#endif
    uint8_t payload[0];
};

struct _udprelay_t
{
    relay_t *outward;
    relay_t *relays;

    lookup_t *lookup;

    int relays_num;
    uint16_t seq;
};

static void udprelay_cleanup(udprelay_t *udprelay);

#ifdef DEBUG
static void rawdump(const void *buf, size_t size) {
    fwrite(buf, 1, size, stderr);
}
#else
#   define rawdump(buf,sz)
#endif

static int udprelay_init(udprelay_t *udprelay, const char *conf_file) {
    memset(udprelay, 0, sizeof(udprelay_t));

    config_t *config = parse_config(conf_file);
    if(!config) {
        syslog(LOG_ERR, "Incorrect config file");
        return -1;
    }

    /* Add outward interface specified with "listen" and "forward" directives */
    udprelay->outward = new_relay(&config->outward);
    if(!udprelay->outward) {
        free_config(config);
        return -1;
    }
    
    syslog(LOG_INFO, "Outward interface: listen to %s, forward to %s",
        udprelay->outward->local_addr ? udprelay->outward->local_addr : "<unspec>",
        udprelay->outward->remote_addr ? udprelay->outward->remote_addr : "<dynamic>");

    /* Add relays */
    relay_config_t *c;
    CLIST_FOREACH(c, config->relay_config) {
        relay_t *relay = new_relay(c);
        if(!relay) {
            udprelay_cleanup(udprelay);
            free_config(config);
            return -1;
        }

        CLIST_ADD_LAST(udprelay->relays, relay);
        udprelay->relays_num++;
        syslog(LOG_INFO, "Add relay from %s to %s",
            relay->local_addr ? relay->local_addr : "<unspec>",
            relay->remote_addr ? relay->remote_addr : "<dynamic>");
    }

    udprelay->lookup = new_lookup(config->track);

    free_config(config);

    return 0;
}

static void udprelay_cleanup(udprelay_t *udprelay) {
    relay_t *r;
    while((r = udprelay->relays) != NULL) {
        CLIST_DEL(udprelay->relays, r);
        free_relay(r);
    }
    if(udprelay->outward) free_relay(udprelay->outward);
    if(udprelay->lookup) free_lookup(udprelay->lookup);
}

/* Handle packet received from peers */
static int udprelay_dispatch_relayed(udprelay_t *udprelay, const void *buffer, size_t sz) {
    X_DBG("%lu bytes\n", (unsigned long)sz);
    if(sz < sizeof(header_t)) return 0; /* Drop */

    const header_t *hdr = (header_t*)buffer;
    
    int seq = ntohs(hdr->seq);
    /* Check for duplicates here */
    if(!lookup_push(udprelay->lookup, seq)) {
        X_DBG("Skip duplicated %d (%d of %d)\n", seq, ntohs(hdr->pkt_num), ntohs(hdr->pkts_in_series));
        return 0;
    }
    X_DBG("Received %d\n", seq);
    rawdump(&hdr->payload, sz - sizeof(header_t));

    /* Strip header and forward */
    return relay_enqueue(udprelay->outward, &hdr->payload, sz - sizeof(header_t));
}

/* Handle packet received from outward interface */
static int udprelay_dispatch_inbound(udprelay_t *udprelay, const void *buffer, size_t sz) {
    uint8_t pkt[sizeof(header_t) + sz] __attribute__((aligned(sizeof(uint16_t))));
    header_t *hdr = (header_t*)pkt;

    memcpy(hdr->payload, buffer, sz);
    hdr->seq = htons(udprelay->seq);
#ifdef DEBUG
    hdr->pkts_in_series = htons(udprelay->relays_num);
#endif

    int i = 0;
    relay_t *r;
    /* Circular list can be iterated starting from any member */
    CLIST_FOREACH(r, udprelay->relays) {
#ifdef DEBUG
        hdr->pkt_num = htons(i);
#endif
        if(X_UNLIKELY(relay_enqueue(r, hdr, sizeof(header_t) + sz) < 0)) {
            syslog(LOG_WARNING, "Relay disabled");
            
            /* Remove from list */
            CLIST_DEL(udprelay->relays, r);
            free_relay(r);
            udprelay->relays_num--;
        } else {
            X_DBG("Sent %d (%d of %d), %lu bytes\n", udprelay->seq, i, udprelay->relays_num, sizeof(header_t) + (unsigned long)sz);
            rawdump(buffer, sz);
        }
        i++;
    }

    udprelay->seq++;
    udprelay->relays = udprelay->relays->_next; /* Round-robin trip */

    return 0;    
}

/* ----------------------------------------------------------------------------- */

static volatile bool sigterm_evt = false;
static void sigterm_handler(int signum) {
    sigterm_evt = true;
}

static void usage(const char *argv0) {
    char *tmp = xstrdup(argv0);
    printf("Usage: %s [-d|--detach] [-p|--pidfile pidfile] config\n", basename(tmp));
    free(tmp);
}

int main(int argc, char **argv) {
    static const struct option longopts[] = {
        {"detach",  no_argument,        NULL,   'd'},
        {"help",    no_argument,        NULL,   'h'},
        {"pidfile", required_argument,  NULL,   'p'},
        {NULL, 0, NULL, 0}
    };

    const char *conf_file = NULL;
    const char *pid_file = NULL;
    bool detach = false;

    int ch;
    while((ch = getopt_long(argc, argv, "dhp:", longopts, NULL)) != -1) {
        switch(ch) {
            case 'd':
                detach = true;
                break;

            case 'p':
                pid_file = optarg;
                break;

            case 'h':
            default:
                usage(argv[0]);
                exit(EXIT_SUCCESS);
        }
    }
    if(optind < argc) conf_file = argv[optind];

    openlog(NULL, LOG_PID | (detach ? 0 : LOG_PERROR), LOG_DAEMON);

    if(!conf_file) {
        syslog(LOG_ERR, "Missing config file");
        exit(EXIT_FAILURE);
    }

    /* main object */
    udprelay_t udprelay;
    if(udprelay_init(&udprelay, conf_file) < 0) {
        exit(EXIT_FAILURE);
    }

    if(detach) xdaemon(pid_file);

    void (*old_sigterm)(int);
    void (*old_sigint)(int);
    old_sigterm = signal(SIGTERM, sigterm_handler);
    old_sigint = signal(SIGINT, sigterm_handler);

    /* main loop */
    fd_set rfds, wfds;

    while(1) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        int maxfd = 0;

        relay_t *r;
        CLIST_FOREACH(r, udprelay.relays) {
            relay_fd_set(r, &rfds, &wfds);
            maxfd = MAX(maxfd, r->fd);
        }

        relay_fd_set(udprelay.outward, &rfds, &wfds);
        maxfd = MAX(maxfd, udprelay.outward->fd);

        int ret = select(maxfd + 1, &rfds, &wfds, NULL, NULL);

        if(X_UNLIKELY((ret < 0 && errno != EAGAIN) || sigterm_evt)) {
            /* signal or error */
            if(ret < 0 && errno != EINTR) syslog(LOG_ERR, "%m");
            break;

        } else if(ret > 0) {
            /* Handle outward interface */
            if(X_UNLIKELY(relay_handle(udprelay.outward, &rfds, &wfds) < 0)) {
                break;
            }

            /* Handle relays */
            CLIST_FOREACH(r, udprelay.relays) {
                if(X_UNLIKELY(relay_handle(r, &rfds, &wfds) < 0)) {
                    syslog(LOG_WARNING, "Relay disabled");
                    /* Remove from list */
                    CLIST_DEL(udprelay.relays, r);
                    free_relay(r);
                    udprelay.relays_num--;
                }
            }

            /* Dispatch inbound */
            void *buffer;
            ssize_t sz = relay_receive(udprelay.outward, &buffer);
            if(sz) {
                if(X_UNLIKELY(udprelay_dispatch_inbound(&udprelay, buffer, sz) < 0)) {
                    break;
                }
            }

            /* Dispatch relayed */
            CLIST_FOREACH(r, udprelay.relays) {
                void *buffer;
                ssize_t sz = relay_receive(r, &buffer);
                if(!sz) continue;

                if(X_UNLIKELY(udprelay_dispatch_relayed(&udprelay, buffer, sz) < 0)) {
                    syslog(LOG_WARNING, "Relay disabled");
                    CLIST_DEL(udprelay.relays, r);
                    free_relay(r);
                    udprelay.relays_num--;
                }
            }
        }
    }

    syslog(LOG_INFO, "Terminating");

    udprelay_cleanup(&udprelay);

    return 0;
}