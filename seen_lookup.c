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

#include "seen_lookup.h"
#include "sglib.h"
#include "clist.h"

typedef struct _lookup_item_t lookup_item_t;

struct _lookup_t {
	/* Preallocated array */
	lookup_item_t *pool;
	int pool_size;

	/* Free items from pool */
	lookup_item_t *free_items;

	/* List of items in income order */
	lookup_item_t *list;

	/* RB-tree for fast lookup */
	lookup_item_t *tree;

	int items;
};

struct _lookup_item_t {
	int seq;

	/* RB-tree */
	int _color;
	lookup_item_t *_left;
	lookup_item_t *_right;

	/* clist */
	lookup_item_t *_prev;
	lookup_item_t *_next;
};

#define LU_COMPARATOR(x,y) ((x)->seq - (y)->seq)

SGLIB_DEFINE_RBTREE_PROTOTYPES(lookup_item_t, _left, _right, _color, LU_COMPARATOR);
SGLIB_DEFINE_RBTREE_FUNCTIONS(lookup_item_t, _left, _right, _color, LU_COMPARATOR);

lookup_t *new_lookup(int size) {
	lookup_t *lu = calloc(1, sizeof(lookup_t));

	lu->pool = calloc(size, sizeof(lookup_item_t));
	lu->pool_size = size;

	/* Initialize pool */
	int i;
	for(i = 0; i < lu->pool_size; i++) {
		CLIST_ADD_LAST(lu->free_items, &lu->pool[i]);
	}

	return lu;
}

/* return true if added */
bool lookup_push(lookup_t *lu, int seq) {
	/* Already seen recently */
	if(sglib_lookup_item_t_find_member(lu->tree, &(lookup_item_t){.seq = seq})) {
		return false;
	}

	lookup_item_t *item;
	if(lu->items == lu->pool_size) {
		/* Remove first item and reuse it */
		item = lu->list;
		CLIST_DEL(lu->list, item);
		sglib_lookup_item_t_delete(&lu->tree, item);
		lu->items--;
	} else {
		/* get first free */
		item = lu->free_items;
		CLIST_DEL(lu->free_items, item);
	}

	item->seq = seq;
	CLIST_ADD_LAST(lu->list, item);
	sglib_lookup_item_t_add(&lu->tree, item);
	lu->items++;

	return true;
}

void free_lookup(lookup_t *lu) {
	free(lu->pool);
	free(lu);
}