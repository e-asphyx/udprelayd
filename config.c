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

#include "utils.h"
#include "config.h"

#define READBUF_SZ 4096
#define DEF_TRACK 1024

typedef enum {
    OPT_LISTEN = 0,
    OPT_FORWARD,
    OPT_RELAY,
    OPT_LOCAL,
    OPT_REMOTE,
    OPT_TRACK,
} opt_t;

config_t *parse_config(const char *file) {
    static const char *lexemes = "listen\0forward\0relay\0local\0remote\0track\0";
    static const char *delim = " \t\n";

    FILE *fp;
    if(!(fp = fopen(file, "r"))) return NULL;

    config_t *conf = calloc(1, sizeof(config_t));
    conf->track = DEF_TRACK;

    char buf[READBUF_SZ];
    while(fgets(buf, sizeof(buf), fp)) {
        char *eol = strchr(buf, '#');
        if(eol) *eol = '\0';

        char *last = NULL;
        char *param = strtok_r(buf, delim, &last);
        if(!param) continue;

        int opt = str_index(lexemes, param);
        if(opt < 0 || opt == OPT_LOCAL || opt == OPT_REMOTE) continue;

        if(opt == OPT_RELAY) {
            relay_config_t *relay_conf = calloc(1, sizeof(relay_config_t));

            char *arg_str;
            while((arg_str = strtok_r(NULL, delim, &last)) != NULL) {
                int arg = str_index(lexemes, arg_str);
                if(arg != OPT_LOCAL && arg != OPT_REMOTE) continue;

                char *addr = strtok_r(NULL, delim, &last);
                if(!addr) break;

                if(arg == OPT_LOCAL) {
                    strreplace(&relay_conf->local_addr, addr);
                } else {
                    strreplace(&relay_conf->remote_addr, addr);
                }
            }

            if(relay_conf->local_addr && relay_conf->remote_addr) {
                CLIST_ADD_LAST(conf->relay_config, relay_conf);
            } else {
                if(relay_conf->local_addr) free(relay_conf->local_addr);
                if(relay_conf->remote_addr) free(relay_conf->remote_addr);
                free(relay_conf);
            }
        } else {
            char *arg = strtok_r(NULL, delim, &last);
            if(!arg) continue;

            switch(opt) {
                case OPT_LISTEN:
                    strreplace(&conf->outward.local_addr, arg);
                    break;

                case OPT_FORWARD:
                    strreplace(&conf->outward.remote_addr, arg);
                    break;

                case OPT_TRACK:
                    conf->track = strtol(arg, NULL, 0);
            }
        }
    }

    fclose(fp);

    if((!conf->outward.local_addr && !conf->outward.remote_addr) || !conf->relay_config) {
        /* Missing critical parameters */
        free_config(conf);
        return NULL;
    }
    
    return conf;
}

void free_config(config_t *config) {
    relay_config_t *r;
    while((r = config->relay_config) != NULL) {
        CLIST_DEL(config->relay_config, r);
        
        if(r->local_addr) free(r->local_addr);
        if(r->remote_addr) free(r->remote_addr);
        free(r);
    }
    if(config->outward.local_addr) free(config->outward.local_addr);
    if(config->outward.remote_addr) free(config->outward.remote_addr);

    free(config);
}