#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "omx.h"
#include "packet_queue.h"
#include "avformat.h"
#include "avcodec.h"
#include "encode.h"
#include "demux.h"
#include "mathematics.h"

#define _1mb 0x100000
#define SPS_PACKET 7
#define PPS_PACKET 8

struct mux_state_t { 
    uint8_t *sps; //for those special packets 
    uint32_t sps_size;
    uint8_t *pps;
    uint32_t pps_size;
    uint8_t *buf;
    uint32_t buf_offset;
    int64_t pts_offset; //taken from omxtx
    bool flush_framebuf;
    
};

static 
AVFormatContext *
init_output_context(const struct transcoder_ctx_t *ctx, AVStream **video_stream, AVStream **audio_stream) {
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
        output_stream = NULL;
        if (input_stream->index == ctx->video_stream_index) {
            //copy stuff from input video index
            c = avcodec_find_encoder(CODEC_ID_H264);
            output_stream = avformat_new_stream(oc, c);
            *video_stream = output_stream;
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
            *audio_stream = output_stream;
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
bool
extract_pps_ssp_packet(struct mux_state_t *mux_state, AVPacket *pkt) {
    bool packet_detected = false;
    
            if (pkt->data[0] == 0 
            && pkt->data[1] == 0 
            && pkt->data[2] == 0 
            && pkt->data[3] == 1) {
            
            fprintf(stderr, "[DEBUG] We got SSP/PPS packet\n");
            
            packet_detected = true;
            int packet_type = pkt->data[4] & 0x1f;
            if (packet_type == SPS_PACKET) {
                if (mux_state->sps) { free(mux_state->sps); }
                
                mux_state->sps = malloc(pkt->size);
                memcpy(mux_state->sps, pkt->data, pkt->size);
                mux_state->sps_size = pkt->size;
                fprintf(stderr, "[DEBUG] Wrote a new SPS, length %d\n", mux_state->sps_size);
                mux_state->flush_framebuf = false;
                
            } else if (packet_type == PPS_PACKET) {
                if(mux_state->pps) { free(mux_state->pps); }
                
                mux_state->pps = malloc(pkt->size);
                memcpy(mux_state->pps, pkt->data, pkt->size);
                mux_state->pps_size = pkt->size;
                fprintf(stderr, "[DEBUG] Wrote a new PPS, length %d\n", mux_state->sps_size);
                mux_state->flush_framebuf = false;
            }
            
        }
    
    return packet_detected;
}

static
void
init_stream_extra_data(AVFormatContext *oc, struct mux_state_t *mux_state, int video_index) {
    AVCodecContext *c;
    c = oc->streams[video_index]->codec;
    
    if (c->extradata) {
        av_free(c->extradata);
        c->extradata = NULL;
        c->extradata_size = 0;
    }
    if (mux_state->pps && mux_state->sps) {
        c->extradata_size = mux_state->pps_size + mux_state->sps_size;
        c->extradata = malloc(c->extradata_size);
        memcpy(c->extradata, mux_state->sps, mux_state->sps_size);
        memcpy(&c->extradata[mux_state->sps_size], mux_state->pps, mux_state->pps_size);
    }
}

static 
void 
write_video_frame(AVFormatContext *oc, AVStream *st, struct transcoder_ctx_t *ctx, struct mux_state_t *mux_state)
{
    AVPacket pkt = { 0 }; // data and size must be 0;
    struct packet_t *source_video;
    mux_state->flush_framebuf = (mux_state->pps && mux_state->sps);
    
    //TODO Put encoded_video_queue into the main ctx
    if(!(source_video = packet_queue_get_next_item_asynch(&ctx->pipeline.encoded_video_queue))) { 
        return;
    }

    if (!(source_video->flags & OMX_BUFFERFLAG_ENDOFNAL)) {
        //We don't have full NAL - buffer until we do
        fprintf(stderr, "[DEBUG] We got a partial NAL - buffering\n");
        if (!mux_state->buf) {
            mux_state->buf = malloc(_1mb);
        }

        memcpy(&mux_state->buf[mux_state->buf_offset], source_video->data, source_video->data_length);
        mux_state->buf_offset += source_video->data_length;
    } else {
        //we got full NAL - write it
        fprintf(stderr, "[DEBUG] We got the end of a NAL - writing\n");
        av_init_packet(&pkt);
        if (mux_state->buf_offset) {
            //if we have buffered data, write the
            //end of the NAL and set appropriate bufs

            //out of bounds check?
            memcpy(&mux_state->buf[mux_state->buf_offset], source_video->data, source_video->data_length);
            mux_state->buf_offset += source_video->data_length;
            pkt.data = mux_state->buf;
            pkt.size = mux_state->buf_offset;
            mux_state->buf_offset = 0;
            mux_state->buf = NULL;
        } else {
            fprintf(stderr, "[DEBUG] We got a full NAL - writing\n");
            //we get a full nal in current packet so write it directly.
            pkt.data = source_video->data;
            pkt.size = source_video->data_length;
        }

        pkt.stream_index = st->index;
        if (source_video->flags & OMX_BUFFERFLAG_SYNCFRAME) {
            pkt.flags |= AV_PKT_FLAG_KEY;
        }

        pkt.pts = av_rescale_q(source_video->PTS, ctx->omx_timebase, oc->streams[ctx->video_stream_index]->time_base);
        if(pkt.pts) { pkt.pts -= mux_state->pts_offset; }
        pkt.dts = AV_NOPTS_VALUE;
        pkt.destruct = avpacket_destruct;
        
        //check for SPS/PPS packets and deal with them
        if(extract_pps_ssp_packet(mux_state, &pkt)) {
            init_stream_extra_data(oc, mux_state, ctx->video_stream_index);
        }
    }
    
    
    
    /* Write the compressed frame to the media file. */
    if (mux_state->flush_framebuf) {
        if (av_interleaved_write_frame(oc, &pkt) != 0) {
            fprintf(stderr, "[DEBUG] Error while writing video frame\n");
            exit(1);
        } 
    }

    
    packet_queue_free_packet(source_video, 0);
}

void
*writer_thread(void *thread_ctx) {

    struct transcoder_ctx_t *ctx = (struct transcoder_ctx_t *) thread_ctx;
    AVStream *video_stream = NULL, *audio_stream = NULL;
    AVFormatContext *output_context = init_output_context(ctx, &video_stream, &audio_stream);
    struct mux_state_t mux_state = {0};

    //from omxtx
    mux_state.pts_offset = av_rescale_q(ctx->input_context->start_time, AV_TIME_BASE_Q, output_context->streams[ctx->video_stream_index]->time_base);

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
        //FIXME no guarantee that we have a full frame per packet?
        write_video_frame(output_context, video_stream, ctx, &mux_state); //write full video frame
        //encoded_video_queue is being filled by the previous command

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
