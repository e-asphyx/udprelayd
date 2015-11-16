#ifndef SEEN_LOOKUP_H
#define SEEN_LOOKUP_H

#include <stdbool.h>

typedef struct _lookup_t lookup_t;

lookup_t *new_lookup(int size);
bool lookup_push(lookup_t *lu, int seq);
void free_lookup(lookup_t *lu);

#endif