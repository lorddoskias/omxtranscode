/* 
 * File:   video_queue.h
 * Author: lorddoskias
 *
 * Created on June 15, 2013, 7:13 PM
 */

#ifndef VIDEO_QUEUE_H
#define	VIDEO_QUEUE_H

#include <stdint.h>
#include "list.h"


#ifdef	__cplusplus
extern "C" {
#endif


struct packet_t
{
  unsigned char* buf; /* The buffer to be freed after use */
  unsigned char* packet; /* Pointer to the actual video data (within buf) */
  int packetlength; /* Number of bytes in packet */
  int frametype;
  struct list_head list;
  int64_t PTS;
  int64_t DTS;
};

struct component_t
{
  pthread_t thread;
  pthread_mutex_t queue_mutex;
  pthread_cond_t queue_count_cv;
  struct list_head queue;
  int queue_count;
  int first_packet;
};

void codec_queue_init(struct component_t* component);
void codec_queue_add_item(struct component_t* component, struct packet_t* packet);
void codec_queue_free_item(struct packet_t* item);
struct packet_t* codec_queue_get_next_item(struct component_t* component);
void codec_flush_queue(struct component_t* component);


#ifdef	__cplusplus
}
#endif

#endif	/* VIDEO_QUEUE_H */

