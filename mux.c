#include <stdio.h>
#include <stdlib.h>

#include "omx.h"
#include "packet_queue.h"
#include "avformat.h"
#include "avcodec.h"
#include "encode.h"
#include "demux.h"

//STUF ABOUT THE WRITER THREAD BELOW THIS LINE

static 
AVFormatContext *
init_output_context(const struct transcoder_ctx_t *ctx, AVStream *video_stream, AVStream *audio_stream) {
    AVFormatContext *oc;
    AVOutputFormat *fmt;
    AVStream *input_stream, *output_stream;
    AVCodec *c;
    AVCodecContext *cc;

    fmt = av_guess_format("mpegts", NULL, NULL);
    if (!fmt) {
        fprintf(stderr, "[DEBUG] Error guessing format, dying\n");
        exit(199);
    }

    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "[DEBUG] Error allocating context, dying\n");
        exit(200);
    }
    
    oc->oformat = fmt;
    snprintf(oc->filename, sizeof(oc->filename), "%s", ctx->output_filename);
    oc->debug = 1;
    oc->start_time_realtime = ctx->input_context->start_time;
    oc->start_time = ctx->input_context->start_time;
    oc->duration = 0;
    oc->bit_rate = 0;

    for (int i = 0; i < ctx->input_context->nb_streams; i++) {
        input_stream = ctx->input_context->streams[i];
        if (input_stream->index == ctx->video_stream_index) {
            //copy stuff from input video index
            c = avcodec_find_encoder(CODEC_ID_H264);
            output_stream = avformat_new_stream(oc, c);
            video_stream = output_stream;
            cc = output_stream->codec;
            cc->width = input_stream->codec->width;
            cc->height = input_stream->codec->height;
#if 0       
            cc->width = viddef->nFrameWidth;
            cc->height = viddef->nFrameHeight;
#endif
            cc->codec_id = CODEC_ID_H264;
            cc->codec_type = AVMEDIA_TYPE_VIDEO;
            cc->bit_rate = ENCODED_BITRATE;
            cc->time_base = input_stream->codec->time_base;

            output_stream->avg_frame_rate = input_stream->avg_frame_rate;
            output_stream->r_frame_rate = input_stream->r_frame_rate;
            output_stream->start_time = AV_NOPTS_VALUE;

        } else if (input_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO) { 
            /* i care only about audio */
            c = avcodec_find_encoder(input_stream->codec->codec_id);
            output_stream = avformat_new_stream(oc, c);
            audio_stream = output_stream;
            avcodec_copy_context(output_stream->codec, input_stream->codec);
            /* Apparently fixes a crash on .mkvs with attachments: */
            av_dict_copy(&output_stream->metadata, input_stream->metadata, 0);
            /* Reset the codec tag so as not to cause problems with output format */
            output_stream->codec->codec_tag = 0;
        }
    }
    
    for (int i = 0; i < oc->nb_streams; i++) {
        if (oc->oformat->flags & AVFMT_GLOBALHEADER)
            oc->streams[i]->codec->flags
                |= CODEC_FLAG_GLOBAL_HEADER;
        if (oc->streams[i]->codec->sample_rate == 0)
            oc->streams[i]->codec->sample_rate = 48000; /* ish */
    }

    if (!(fmt->flags & AVFMT_NOFILE)) {
        fprintf(stderr, "[DEBUG] AVFMT_NOFILE set, allocating output container\n");
        if (avio_open(&oc->pb, ctx->output_filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "[DEBUG] error creating the output context\n");
            exit(1);
        }
    }
    
    return oc;
}


static
void 
avpacket_destruct(AVPacket *pkt) {
    if(pkt->data) {
        free(pkt->data);
    }
    pkt->data = NULL;
    pkt->destruct = av_destruct_packet;
}

static
void
write_audio_frame(AVFormatContext *oc, AVStream *st, struct transcoder_ctx_t *ctx) {
    AVPacket pkt = {0}; // data and size must be 0;
    struct packet_t *source_audio;
    av_init_packet(&pkt);

    if (!(source_audio = packet_queue_get_next_item_asynch(ctx->processed_audio_queue))) {
        return;
    }
    
    pkt.stream_index = st->index;
    pkt.size = source_audio->data_length;
    pkt.data = source_audio->data;
    pkt.pts = source_audio->PTS;
    pkt.destruct = avpacket_destruct;
    /* Write the compressed frame to the media file. */
    if (av_interleaved_write_frame(oc, &pkt) != 0) {
        fprintf(stderr, "[DEBUG] Error while writing audio frame\n");
        exit(1);
    }

    packet_queue_free_packet(source_audio, 0);
}

static 
void 
write_video_frame(AVFormatContext *oc, AVStream *st, struct transcoder_ctx_t *ctx)
{
    AVPacket pkt = { 0 }; // data and size must be 0;
    struct packet_t *source_video;
    av_init_packet(&pkt);


    //TODO Put encoded_video_queue into the main ctx
    if(!(source_video = packet_queue_get_next_item_asynch(&ctx->pipeline.encoded_video_queue))) { 
        return;
    }
    
    if(!(source_video->flags & OMX_BUFFERFLAG_ENDOFNAL)) {
        //buffer data because we do not have a full nal 
    } else { 
        //write rest of the data 
    }
    pkt.stream_index = st->index;
    pkt.size = source_video->data_length;
    pkt.data = source_video->data;
    pkt.pts = source_video->PTS;
    pkt.dts = AV_NOPTS_VALUE;
    pkt.destruct = avpacket_destruct;
    /* Write the compressed frame to the media file. */
    if (av_interleaved_write_frame(oc, &pkt) != 0) {
        fprintf(stderr, "[DEBUG] Error while writing audio frame\n");
        exit(1);
    }
    
    packet_queue_free_packet(source_video, 0);
}


void 
*writer_thread(void *thread_ctx) {

    struct transcoder_ctx_t *ctx = (struct transcoder_ctx_t *) thread_ctx;
    AVStream *video_stream = NULL, *audio_stream = NULL;
    AVFormatContext *output_context = init_output_context(ctx, video_stream, audio_stream);
    
#if 0
    FILE *out_file;

    out_file = fopen(ctx->output_filename, "wb");
    if (out_file == NULL) {
        printf("error creating output file. DYING \n");
        exit(1);
    }
#endif
    
    //write stream header if any
    avformat_write_header(output_context, NULL);
    
    //do not start doing anything until we get an encoded packet
    pthread_mutex_lock(&ctx->pipeline.video_encode.is_running_mutex);
    pthread_cond_wait(&ctx->pipeline.video_encode.is_running_cv, &ctx->pipeline.video_encode.is_running_mutex);
    
    while (!ctx->pipeline.video_encode.eos || !ctx->processed_audio_queue->queue_finished) {
        //FIXME a memory barrier is required here so that we don't race 
        //on above variables 
        
        //fill a buffer with video data 
        OERR(OMX_FillThisBuffer(ctx->pipeline.video_encode.h, omx_get_next_output_buffer(&ctx->pipeline.video_encode)));
        
        write_audio_frame(output_context, audio_stream, ctx); //write full audio frame 
        write_video_frame(output_context, video_stream, ctx); //write full video frame
        //encoded_video_queue is being filled by the previous command
        //FIXME no guarantee that we have a full frame per packet. 
        
        
#if 0
        struct packet_t *encoded_packet = packet_queue_get_next_item(&ctx->pipeline.encoded_video_queue);
        fwrite(encoded_packet->data, 1, encoded_packet->data_length, out_file);
        packet_queue_free_packet(encoded_packet, 1);
#endif
        
    }

    av_write_trailer(output_context);

    //free all the resources
    /* Free the streams. */
    for (int i = 0; i < output_context->nb_streams; i++) {
        av_freep(&output_context->streams[i]);
    }

    if (!(output_context->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_close(output_context->pb);

    /* free the stream */
    av_free(output_context);
#if 0
    fclose(out_file);
#endif
}
