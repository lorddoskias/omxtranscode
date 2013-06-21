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
        char *output_filename; //output files will be written here
        struct packet_queue_t *video_queue;
        struct packet_queue_t *audio_queue;
        struct omx_pipeline_t pipeline;
       
        int first_packet;
        //FIXME: used to synchronise the encoded component
        //move it to per-component
        pthread_mutex_t is_running_mutex;
        pthread_cond_t is_running_cv;
    };

void * video_thread(void *ctx);
void *writer_thread(void *thread_ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* VIDEO_H */

