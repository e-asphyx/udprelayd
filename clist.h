/*
One day I wrote this simple circular list implementation for my own purposes.
This code is in the public domain and is thus free for use for any purpose, commercial or private.

Eugene Zagidullin
*/
#ifndef _CLIST_H_
#define _CLIST_H_

#define CLIST_ADD_BEFORE(token, item) \
do { \
	(item)->_prev = (token)->_prev; \
	(item)->_next = (token); \
	(token)->_prev->_next = (item); \
	(token)->_prev = (item); \
} while(0)

#define CLIST_ADD_AFTER(token, item) CLIST_ADD_BEFORE((token)->_next, item)

#define CLIST_ADD_LAST(list, item) \
do { \
	if((list) == NULL) { \
		(item)->_prev = (item); \
		(item)->_next = (item); \
		(list) = (item); \
	} else { \
		CLIST_ADD_BEFORE(list, item); \
	} \
} while(0)

#define CLIST_ADD_FIRST(list, item) \
do { \
	CLIST_ADD_LAST(list, item); \
	(list) = (list)->_prev; \
} while(0)

#define CLIST_DEL(list, item) \
do { \
	if((item) == (list)) { \
		if((item)->_next == (item)) { \
			(list) = NULL; \
		} else { \
			(list) = (item)->_next; \
		} \
	} \
	(item)->_prev->_next = (item)->_next; \
	(item)->_next->_prev = (item)->_prev; \
} while(0)

#define CLIST_FOREACH(item, list) \
for(__typeof__(list) _token = ((item) = (list)) != NULL ? ((list)->_next != (list) ? (list)->_next : NULL) : NULL; \
(item) != NULL; \
(item) = _token, _token = _token != NULL ? (_token->_next != (list) ? _token->_next : NULL) : NULL)

#endif /* _CLIST_H_ */
