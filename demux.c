#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "include/libavformat/avformat.h"
#include "include/libavutil/mathematics.h"
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
extract_video_stream(AVFormatContext *fmt_ctx, AVStream *video_stream, struct av_demux_t *ctx) {
    AVPacket packet;
    FILE *output_file = NULL;
    AVRational omx_timebase = {1, 1000000};
    struct packet_t *video_packet;
   
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    output_file = fopen(ctx->output_filename, "w+b");

    //start reading frames 
    while (av_read_frame(fmt_ctx, &packet) >= 0) {

        if (packet.stream_index == video_stream->index) {
            
            video_packet = malloc(sizeof(*video_packet));
            //taken from pidvbip - rescaling the pts, dunno if correct
            video_packet->PTS = av_rescale_q(packet.pts, video_stream->time_base, omx_timebase);
            video_packet->DTS = -1;
            video_packet->data_length = packet.size;
            video_packet->data = malloc(packet.size);
            memcpy(video_packet->data, packet.data, packet.size);
            fwrite(video_packet->data, sizeof (*video_packet->data), video_packet->data_length, output_file);
            
            /* The best thing will be do actually add each and every packet into a 
             * private list in this loop and only when the duration in the queue is 
             * below a minimum signal a cond_var to continue demuxing. 
             */
            while (ctx->video_queue->queue_count > 100) { usleep(100000); } // FIXME
            packet_queue_add_item(ctx->video_queue, video_packet);
        } 
        
        av_free_packet(&packet);
    }
    
    ctx->video_queue->queue_finished = 1;
    
    fclose(output_file);
}

void 
*demux_thread(void *ctx) {
    
    struct av_demux_t *demux_ctx = (struct av_demux_t *) ctx;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;

    av_register_all();
    if (avformat_open_input(&fmt_ctx, demux_ctx->input_filename, NULL, NULL) < 0) {
        printf("Error opening input file: %s\n", demux_ctx->input_filename);
        abort();
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        printf("error finding streams\n");
        abort();
    }

    if (init_streams_and_codecs(fmt_ctx, &video_stream, &audio_stream, &video_codec_ctx, &audio_codec_ctx) < 0) {
        printf("Error identifying video/audio streams\n");
        return NULL;
    }

    extract_video_stream(fmt_ctx, video_stream, demux_ctx);

    //clean up 
    avcodec_close(video_codec_ctx);
    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);
    
    return NULL;

}
