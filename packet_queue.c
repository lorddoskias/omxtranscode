
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "packet_queue.h"

void packet_queue_init(struct packet_queue_t* component) 
{
    INIT_LIST_HEAD(&component->queue);
    component->queue_count = 0;

    pthread_mutex_init(&component->queue_mutex, NULL);
    pthread_cond_init(&component->queue_count_cv, NULL);
}

void packet_queue_add_item(struct packet_queue_t* component, struct packet_t* packet) 
{
    
    if (packet == NULL) {
        fprintf(stderr, "ERROR: Adding NULL packet to queue, skipping\n");
        return;
    }

    pthread_mutex_lock(&component->queue_mutex);

    if (list_empty(&component->queue)) {
        list_add_tail(&packet->list, &component->queue);
        pthread_cond_signal(&component->queue_count_cv);
    } else {
        list_add_tail(&packet->list, &component->queue);
    }

    component->queue_count++;

    pthread_mutex_unlock(&component->queue_mutex);
}

void packet_queue_free_item(struct packet_t* item) 
{
    if (item == NULL)
        return;
    
    if (item->buf) {
        free(item->buf);
    }

    free(item);
}

struct packet_t* packet_queue_get_next_item(struct packet_queue_t* component) 
{
    struct packet_t *item;
    pthread_mutex_lock(&component->queue_mutex);

    while (list_empty(&component->queue))
        pthread_cond_wait(&component->queue_count_cv, &component->queue_mutex);

    item = list_entry(component->queue.next,struct packet_t, list);

    list_del(component->queue.next);
    component->queue_count--;

    pthread_mutex_unlock(&component->queue_mutex);

    return item;
}

void 
packet_flush_queue(struct packet_queue_t* component) {
    
    /* Empty the queue */
    pthread_mutex_lock(&component->queue_mutex);
    struct list_head *current_entry, *n;
    struct packet_t *current_packet;

    list_for_each_safe(current_entry, n, &component->queue) {
        current_packet = list_entry(current_entry, struct packet_t, list);
        list_del(current_entry);
        packet_queue_free_item(current_packet);
    }


    component->queue_count = 0;

    pthread_mutex_unlock(&component->queue_mutex);
}