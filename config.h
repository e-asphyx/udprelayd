#ifndef CONFIG_H
#define CONFIG_H

#include "clist.h"

typedef struct _relay_config_t relay_config_t;
struct _relay_config_t {
	char *local_addr;
	char *remote_addr;

	relay_config_t *_prev;
	relay_config_t *_next;
};

typedef struct _config_t config_t;
struct _config_t {
	relay_config_t outward;
	relay_config_t *relay_config;
	int track;
};

config_t *parse_config(const char *file);
void free_config(config_t *config);

#endif