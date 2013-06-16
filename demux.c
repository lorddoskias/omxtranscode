#include <stdlib.h>

#include "include/libavformat/avformat.h"
#include "demux.h"

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
extract_video_stream(AVFormatContext *fmt_ctx, AVStream *video_stream, const char *output_file_name) {
    AVPacket packet;
    FILE *output_file = NULL;

    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    output_file = fopen(output_file_name, "w+b");

    //start reading frames 
    while (av_read_frame(fmt_ctx, &packet) >= 0) {

        if (packet.stream_index == video_stream->index) {
            //we are dealing with video frames
            fwrite(packet.data, sizeof (*packet.data), packet.size, output_file);
        } 
        
        av_free_packet(&packet);
    }
    
    fclose(output_file);
}

void 
*demux_thread(void *ctx) {
    struct av_demux_t *demux_ct = (struct av_demux_t *) ctx;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;


    av_register_all();
    if (avformat_open_input(&fmt_ctx, demux_ct->input_filename, NULL, NULL) < 0) {
        printf("Error opening input file: %s\n", demux_ct->input_filename);
        abort();
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        printf("error finding streams\n");
        abort();
    }

 //   av_dump_format(fmt_ctx, 0, ctx, 0);

    if (init_streams_and_codecs(fmt_ctx, &video_stream, &audio_stream, &video_codec_ctx, &audio_codec_ctx) < 0) {
        printf("Error identifying video/audio streams\n");
        return NULL;
    }

    extract_video_stream(fmt_ctx, video_stream, demux_ct->output_filename);

    //clean up 
    avcodec_close(video_codec_ctx);
    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);
    
    return NULL;

}
