/* cache.c
 *
 * tianyinj@andrew.cmu.edu
 *
 * A cache memory for server response of a unique uri
 *
 * All caches are saved in memory from cache_hd to cache_tail
 * All caches are in a doublely linked list structure
 *
 * Used a LRU(not strictly) eviction policy
 *
 * The most recently hitted elem is near cache_hd so evict from tail.
 *
 */

#include "cache.h"

static elem *cache_hd;
static elem *cache_tail;
static size_t cache_size;
/*
 * cache_hd and tail are edge cases without useful data saved
 *
 * Initialize total cache size to 0;
 */
int init(){
    cache_hd=malloc(sizeof(elem));
    cache_tail=malloc(sizeof(elem));
    cache_hd->next=cache_tail;
    cache_tail->prev=cache_hd;
    cache_size=0;
    return 1;
}

/*
 * Given a unique uri u, traverse the doublely linked list
 *  from head to tail.
 *
 * Return a pointer to the elem with u as uri if it is already cached
 * Otherwise, NULL shows this uri has not been saved yet
 *
 */
elem* find(char* u){
    elem *ptr;
    for(ptr=cache_hd->next;ptr!=cache_tail;ptr=ptr->next){
        if (!strcmp(ptr->uri,u)){

            ptr->prev->next=ptr->next;
            ptr->next->prev=ptr->prev;

            elem *tmp = cache_hd->next;
            cache_hd->next=ptr;
            ptr->prev=cache_hd;
            ptr->next=tmp;
            tmp->prev=ptr;
            return ptr;
        }
    }
    return NULL;
}

/*
 *
 * Insert an elem with u as uri, and file as server response
 *  to the head of the linked list
 *
 * If has size issues call evict until it made enough space.
 */
void insert(char *u, int len, char* file){
    elem *new=Malloc(sizeof(elem));
    new->uri=Malloc(MAXLINE);
    strcpy(new->uri,u);
    new->content=Malloc(len);
    memcpy(new->content,file,len);
    new->size=len;

    if(MAX_CACHE_SIZE-cache_size<len){
        evict(MAX_CACHE_SIZE-len);
    }
    new->next=cache_hd->next;
    new->prev=cache_hd;
    cache_hd->next=new;
    new->next->prev=new;

    cache_size+=len;

    //free_elem(new);
    return;
}

/*
 * Take a desired cache size as arguemnt,
 *  Keep evicting until the actual cache size is less than
 *  the desired size. (More space for the incoming elem!)
 *
 */
void evict(size_t least){
    while ((cache_size>least)&&(cache_tail->prev!=cache_hd)){
        cache_size-=cache_tail->prev->size;
        elem* tmp=cache_tail->prev;
        cache_tail->prev=tmp->prev;
        tmp->prev->next=cache_tail;
        free_elem(tmp);
    }
    return;
}

/*
 *
 * This elem is just hitted so put it to the head
 *
 */
void update(elem *update){
    update->next=cache_hd->next;
    update->prev=cache_hd;
    cache_hd->next=update;
    update->next->prev=update;
    return;
}

void free_elem(elem *e){
    Free(e->content);
    Free(e->uri);
    Free(e);
}
