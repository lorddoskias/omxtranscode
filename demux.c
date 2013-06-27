/*
 
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
#include <unistd.h>
#include <pthread.h>

#include "avformat.h"
#include "mathematics.h"
#include "demux.h"
#include "packet_queue.h"

static
int
init_streams_and_codecs(AVFormatContext *fmt_ctx, AVStream **video_stream, AVStream **audio_stream,
        AVCodecContext **video_ctx, AVCodecContext **audio_ctx) {
    
    *video_ctx = NULL;
    *video_stream = NULL;
    *audio_ctx = NULL;
    *audio_stream = NULL;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            *video_stream = fmt_ctx->streams[i];
            *video_ctx = fmt_ctx->streams[i]->codec;
        } else if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            *audio_stream = fmt_ctx->streams[i];
            *audio_ctx = fmt_ctx->streams[i]->codec;
        }
    }

    if (*video_ctx != NULL && *video_stream != NULL && *audio_ctx != NULL && *audio_stream != NULL)
        return 0;

    return -1;
}

static
void
extract_streams(AVFormatContext *fmt_ctx, AVStream *video_stream, AVStream *audio_stream, struct transcoder_ctx_t *ctx) {
    AVPacket av_packet;
    AVRational omx_timebase = {1, 1000000};
    struct packet_t *packet;
#if 0
    FILE *out_file;
    out_file = fopen("test-output.h264", "wb");
#endif
    av_init_packet(&av_packet);
    av_packet.data = NULL;
    av_packet.size = 0;

    //start reading frames 
    while ((av_read_frame(fmt_ctx, &av_packet) >= 0)) {

        if (av_packet.stream_index == video_stream->index) {
            if (av_packet.pts == AV_NOPTS_VALUE) {
                fprintf(stderr, "not pts found in video packet, dropping\n");
            } else {
                packet = malloc(sizeof (*packet));
                //taken from pidvbip - rescaling the pts, dunno if correct
                packet->PTS = av_rescale_q(av_packet.pts, video_stream->time_base, omx_timebase);
                packet->DTS = -1;
                packet->data_length = av_packet.size;
                packet->data = malloc(av_packet.size);
                packet->flags = av_packet.flags;
                memcpy(packet->data, av_packet.data, av_packet.size);
#if 0
                fwrite(av_packet.data, 1, av_packet.size, out_file);
                packet_count++;
                fprintf(stderr, "packets: v: %d\r", packet_count);
#endif
                /* Fixme: The best thing will be do actually add each and every packet into a 
                 * private list in this loop and only when the duration in the queue is 
                 * below a minimum signal a cond_var to continue demuxing. 
                 */
                while (ctx->input_video_queue->queue_count > 100) {
                    usleep(100000);
                }
                packet_queue_add_item(ctx->input_video_queue, packet);
            }


        } else if (av_packet.stream_index == audio_stream->index) {
            if (av_packet.pts == AV_NOPTS_VALUE) {
                fprintf(stderr, "not pts found in audio packet, dropping\n");
            } else {
                packet = malloc(sizeof (*packet));
                packet->PTS = av_packet.pts;
                packet->DTS = -1;
                packet->data_length = av_packet.size;
                packet->data = malloc(av_packet.size);
                memcpy(packet->data, av_packet.data, av_packet.size);

                while (ctx->processed_audio_queue->queue_count > 100) {
                    usleep(100000);
                }
                packet_queue_add_item(ctx->processed_audio_queue, packet);
            }
        }

        av_free_packet(&av_packet);
    }

    ctx->input_video_queue->queue_finished = 1;
    ctx->processed_audio_queue->queue_finished = 1;
#if 0
    fclose(out_file);
#endif
}

void 
*demux_thread(void *ctx) {
    
    struct transcoder_ctx_t *demux_ctx = (struct transcoder_ctx_t *) ctx;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;

    if (init_streams_and_codecs(demux_ctx->input_context, &video_stream, &audio_stream, &video_codec_ctx, &audio_codec_ctx) < 0) {
        printf("Error identifying video/audio streams\n");
        return NULL;
    }
    
    //we set the audio codec config from here to use 
    //when muxing
    demux_ctx->audio_codec.codec_id = audio_stream->codec->codec_id;
    demux_ctx->audio_codec.bit_rate = audio_stream->codec->bit_rate;
    demux_ctx->audio_codec.channels = audio_stream->codec->channels;        
    demux_ctx->audio_codec.sample_rate = audio_stream->codec->sample_rate;
    demux_ctx->audio_codec.channels_layout = audio_stream->codec->channel_layout;

    extract_streams(demux_ctx->input_context, video_stream, audio_stream,  demux_ctx);

    //clean up 
    avcodec_close(video_codec_ctx);
    avcodec_close(audio_codec_ctx);
    
    return NULL;

}
