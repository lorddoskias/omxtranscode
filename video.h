/* 
 * File:   video.h
 * Author: lorddoskias
 *
 * Created on June 16, 2013, 5:09 PM
 */

#ifndef VIDEO_H
#define	VIDEO_H

#include "omx.h"

#ifdef	__cplusplus
extern "C" {
#endif

    struct decode_ctx_t {
        struct packet_queue_t *video_queue;
        struct packet_queue_t *audio_queue;
        struct omx_pipeline_t pipeline;
       
        int first_packet;
        pthread_mutex_t is_running_mutex;
        int is_running;
        pthread_cond_t is_running_cv;
    };

void * video_thread(void *ctx);
void *writer_thread(void *thread_ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* VIDEO_H */

