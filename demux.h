/* 
 * File:   demux.h
 * Author: lorddoskias
 *
 * Created on June 16, 2013, 3:21 PM
 */

#ifndef DEMUX_H
#define	DEMUX_H

#include "avcodec.h"

#ifdef	__cplusplus
extern "C" {
#endif

    struct audio_config_t { 
        enum CodecID codec_id;
        int sample_rate;
        int channels;
        int bit_rate;
        enum AVSampleFormat sample_fmt;
    };
    
    struct av_demux_t {
        char *input_filename;
        struct packet_queue_t *video_queue;
        struct packet_queue_t *audio_queue;
        
        struct audio_config_t audio_codec;
    };

    void *demux_thread(void *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* DEMUX_H */

