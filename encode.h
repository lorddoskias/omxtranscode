/* 
 * File:   encode.h
 * Author: lorddoskias
 *
 * Created on June 18, 2013, 9:27 PM
 */

#ifndef ENCODE_H
#define	ENCODE_H

#ifdef	__cplusplus
extern "C" {
#endif
    
    struct decode_ctx_t {
        char *output_filename; //output files will be written here
        struct packet_queue_t *input_video_queue;
        struct packet_queue_t *input_audio_queue;
        struct omx_pipeline_t pipeline;
       
        int first_packet;
        //FIXME: used to synchronise the encoded component
        //move it to per-component
        pthread_mutex_t is_running_mutex;
        pthread_cond_t is_running_cv;
        
        //points to the audio_config_t struct in 
        //the demux context
        struct audio_config_t *audio_codec;
    };
    
void *consumer_thread(void *thread_ctx);
void *decode_thread(void *ctx);
void *writer_thread(void *thread_ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* ENCODE_H */

