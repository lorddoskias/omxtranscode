
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "packet_queue.h"

void packet_queue_init(struct packet_queue_t* queue) 
{
    INIT_LIST_HEAD(&queue->queue);
    queue->queue_count = 0;
    queue->queue_finished = 0;
    pthread_mutex_init(&queue->queue_mutex, NULL);
    pthread_cond_init(&queue->queue_count_cv, NULL);
}

void packet_queue_add_item(struct packet_queue_t* queue, struct packet_t* packet) 
{
    
    if (packet == NULL) {
        fprintf(stderr, "ERROR: Adding NULL packet to queue, skipping\n");
        return;
    }

    pthread_mutex_lock(&queue->queue_mutex);

    if (list_empty(&queue->queue)) {
        list_add_tail(&packet->list, &queue->queue);
        pthread_cond_signal(&queue->queue_count_cv);
    } else {
        list_add_tail(&packet->list, &queue->queue);
    }

    queue->queue_count++;

    pthread_mutex_unlock(&queue->queue_mutex);
}

void packet_queue_free_item(struct packet_t* item) 
{
    if (item == NULL)
        return;
    
    if (item->data) {
        free(item->data);
    }

    free(item);
}

struct packet_t* packet_queue_get_next_item(struct packet_queue_t* queue) 
{
    struct packet_t *item;
    
    pthread_mutex_lock(&queue->queue_mutex);
    while (list_empty(&queue->queue))
        pthread_cond_wait(&queue->queue_count_cv, &queue->queue_mutex);

    item = list_entry(queue->queue.next,struct packet_t, list);

    list_del(queue->queue.next);
    queue->queue_count--;

    pthread_mutex_unlock(&queue->queue_mutex);

    return item;
}

void 
packet_flush_queue(struct packet_queue_t* queue) {
    
    /* Empty the queue */
    pthread_mutex_lock(&queue->queue_mutex);
    struct list_head *current_entry, *n;
    struct packet_t *current_packet;

    list_for_each_safe(current_entry, n, &queue->queue) {
        current_packet = list_entry(current_entry, struct packet_t, list);
        list_del(current_entry);
        packet_queue_free_item(current_packet);
    }


    queue->queue_count = 0;

    pthread_mutex_unlock(&queue->queue_mutex);
}