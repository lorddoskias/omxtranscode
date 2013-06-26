/**
 * 
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
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

void packet_queue_free_packet(struct packet_t* item, int free_data) 
{
    if (item == NULL)
        return;
    
    if (item->data && free_data) {
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