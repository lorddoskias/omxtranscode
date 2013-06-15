
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "video_queue.h"

void codec_queue_init(struct component_t* codec) 
{
    INIT_LIST_HEAD(&codec->queue);
    codec->queue_count = 0;

    pthread_mutex_init(&codec->queue_mutex, NULL);
    pthread_cond_init(&codec->queue_count_cv, NULL);
}

void codec_queue_add_item(struct component_t* codec, struct packet_t* packet) 
{
    
    if (packet == NULL) {
        fprintf(stderr, "ERROR: Adding NULL packet to queue, skipping\n");
        return;
    }

    pthread_mutex_lock(&codec->queue_mutex);

    if (list_empty(&codec->queue)) {
        list_add_tail(&packet->list, &codec->queue);
        pthread_cond_signal(&codec->queue_count_cv);
    } else {
        list_add_tail(&packet->list, &codec->queue);
    }

    codec->queue_count++;

    pthread_mutex_unlock(&codec->queue_mutex);
}


void codec_queue_free_item(struct component_t* codec,struct packet_t* item)
{
  if (item == NULL)
    return;

    free(item->buf);
    free(item);
}

struct packet_t* codec_queue_get_next_item(struct component_t* codec) 
{
    struct packet_t *item;
    pthread_mutex_lock(&codec->queue_mutex);

    while (list_empty(&codec->queue))
        pthread_cond_wait(&codec->queue_count_cv, &codec->queue_mutex);

    item = container_of(codec->queue.next,struct packet_t, list);

    list_del(codec->queue.next);
    codec->queue_count--;

    pthread_mutex_unlock(&codec->queue_mutex);

    return item;
}