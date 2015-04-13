#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct elem elem;
struct elem{
    char *uri;
    char *content;
    int size;
    elem* next;
    elem* prev;
};

int init();
elem* find(char* u);
void insert(char *u, int len, char* file);
void evict(size_t least);
void update(elem *update);
void free_elem(elem *e);

#endif
