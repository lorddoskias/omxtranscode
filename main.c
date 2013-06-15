/**
 * demux video and write it to a separate file
 * @param argc
 * @param argv
 * @return 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/bcm_host.h"
#include "include/libavformat/avformat.h"

static
void
query_components() {
    bcm_host_init();
    OMX_Init();
    int i;
    char name[130];
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    for (i = 0; OMX_ErrorNone == eError; i++) {
        eError = OMX_ComponentNameEnum(name, 130, i);

        if (eError != OMX_ErrorNoMore) {
            printf("found component %s\n", name);
        } else {
            break;
        }
    }

    printf("enumerated all components \n");

    OMX_Deinit();
}

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
extract_video_stream(AVFormatContext *fmt_ctx, AVStream *video_stream) {
    AVPacket packet;
    FILE *output_file = NULL;

    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    output_file = fopen("myfile.h264", "w+b");

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

int main(int argc, char **argv) {

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    
    av_register_all();
    if (avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0) {
        abort();
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        abort();
    }

    av_dump_format(fmt_ctx, 0, argv[1], 0);

    if (init_streams_and_codecs(fmt_ctx, &video_stream, &audio_stream, &video_codec_ctx, &audio_codec_ctx) < 0) {
        printf("Error identifying video/audio streams\n");
        return -1;
    }
    
    extract_video_stream(fmt_ctx, video_stream);
    
    //clean up 
    avcodec_close(video_codec_ctx);
    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);
    
}


