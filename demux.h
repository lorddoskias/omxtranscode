/* 
 * File:   demux.h
 * Author: lorddoskias
 *
 * Created on June 16, 2013, 3:21 PM
 */

#ifndef DEMUX_H
#define	DEMUX_H

#ifdef	__cplusplus
extern "C" {
#endif

    struct av_demux_t {
        char *input_filename;
        char *output_filename;
        struct packet_queue_t *video_queue;
        struct packet_queue_t *audio_queue;
    };

    void *demux_thread(void *ctx);

#ifdef	__cplusplus
}
#endif

#endif	/* DEMUX_H */

