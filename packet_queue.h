/* 
 * File:   video_queue.h
 * Author: lorddoskias
 *
 * Created on June 15, 2013, 7:13 PM
 */

#ifndef VIDEO_QUEUE_H
#define	VIDEO_QUEUE_H

#include <stdint.h>
#include <pthread.h>
#include "list.h"


#ifdef	__cplusplus
extern "C" {
#endif


struct packet_t
{
  uint8_t *data; /* The buffer to be freed after use */
  int data_length; /* Number of bytes in packet */
  struct list_head list;
  int64_t PTS;
  int64_t DTS;
  uint32_t flags; //copy of OMX buffer flags
};

struct packet_queue_t
{
  pthread_mutex_t queue_mutex;
  pthread_cond_t queue_count_cv;
  struct list_head queue;
  int queue_count;
  int first_packet;
  int queue_finished;
};

void packet_queue_init(struct packet_queue_t* component);
void packet_queue_add_item(struct packet_queue_t* component, struct packet_t* packet);
void packet_queue_free_packet(struct packet_t* item, int free_data);
struct packet_t* packet_queue_get_next_item(struct packet_queue_t* component);
struct packet_t* packet_queue_get_next_item_asynch(struct packet_queue_t* queue); 
void packet_queue_flush(struct packet_queue_t* component);


#ifdef	__cplusplus
}
#endif

#endif	/* VIDEO_QUEUE_H */

