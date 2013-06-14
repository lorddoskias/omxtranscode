/**
 * will be doing some demuxing stuff here 
 * @param argc
 * @param argv
 * @return 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/bcm_host.h"
#include "libs/ilclient/ilclient.h"
//#include "include/libavutil/avutil.h"
#include "include/libavformat/avformat.h"
//#include "include/libavcodec/avcodec.h"


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
open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream in input file\n");
        return ret;
    } else {
        *stream_idx = ret;
        st = fmt_ctx->streams[*stream_idx];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find codec\n");
            return ret;
        }

        if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
            fprintf(stderr, "Failed to open codec\n");
            return ret;
        }
    }

    return 0;
}

static
int
init_streams_and_codecs(AVFormatContext *fmt_ctx, AVStream **video_stream, AVStream **audio_stream,
        AVCodecContext **video_ctx, AVCodecContext **audio_ctx) {
    // Find the first video stream

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

int main(int argc, char **argv) {

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *audio_stream = NULL;
    AVPacket packet;

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


    //AVCodec *videoDecoder = avcodec_find_decoder(video_codec_ctx->codec_id);
    
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;
    
    //start reading frames 
    while(av_read_frame(fmt_ctx, &packet) >= 0) {
        
        if(packet.stream_index == video_stream->index) {
            //we are dealing with video frames
            printf("received video frame. doing nothing\n");
        } else if (packet.stream_index == audio_stream->index) {
            printf("we have audio - skipping\n");
        }
        
        av_free_packet(&packet);
    }
            
    
    
    //clean up 
    
    avcodec_close(video_codec_ctx);
    avcodec_close(audio_codec_ctx);
    avformat_close_input(&fmt_ctx);

}


