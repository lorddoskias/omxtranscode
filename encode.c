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
#include <stdio.h>
#include <unistd.h>

#include "omx.h"
#include "packet_queue.h"
#include "avformat.h"
#include "avcodec.h"
#include "demux.h"

#define ENCODED_BITRATE 2000000 //2 megabits

/* will be feeding stuff into the encoding pipeline */
void *
decode_thread(void *context) {

    OMX_BUFFERHEADERTYPE *input_buffer; // buffer taken from the OMX decoder 
    OMX_PARAM_PORTDEFINITIONTYPE encoder_config;
    OMX_PARAM_PORTDEFINITIONTYPE decoder_config;
    OMX_PARAM_PORTDEFINITIONTYPE deinterlacer_config;
    OMX_VIDEO_PARAM_PORTFORMATTYPE encoder_format_config; //used for the output of the encoder
    OMX_VIDEO_PARAM_BITRATETYPE encoder_bitrate_config; //used for the output of the encoder
    struct transcoder_ctx_t *ctx = (struct transcoder_ctx_t *) context;
    struct packet_t *current_packet;
    int bytes_left;
    uint8_t *p; // points to currently copied buffer 

    //    omx_setup_encoding_pipeline(&decoder_ctx->pipeline, OMX_VIDEO_CodingAVC);
    omx_setup_encoding_pipeline(&ctx->pipeline, OMX_VIDEO_CodingMPEG2);

    // main loop that will poll packets and render
    while (ctx->input_video_queue->queue_count != 0 || ctx->input_video_queue->queue_finished != 1) {
        //TODO a memory barrier is going to be needed so that we don't race
        current_packet = packet_queue_get_next_item(ctx->input_video_queue);
        p = current_packet->data;
        bytes_left = current_packet->data_length;

        while (bytes_left > 0) {

            fprintf(stderr, "OMX buffers: v: %02d/20, vcodec queue: %4d\r", omx_get_free_buffer_count(&ctx->pipeline.video_decode), ctx->input_video_queue->queue_count);
            input_buffer = omx_get_next_input_buffer(&ctx->pipeline.video_decode); // This will block if there are no empty buffers

            // copy at most the length of the OMX buf
            int copy_length = OMX_MIN(bytes_left, input_buffer->nAllocLen);

            memcpy(input_buffer->pBuffer, p, copy_length);
            p += copy_length;
            bytes_left -= copy_length;

            input_buffer->nFilledLen = copy_length;
            input_buffer->nTimeStamp = pts_to_omx(current_packet->PTS);
            
            if (ctx->first_packet) {
                input_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
                ctx->first_packet = 0;
            } else {
                //taken from ilclient
                input_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
            }
            
            if(bytes_left == 0) {
                input_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
            }

            //configure the resizer after the decoder has got output
            if (ctx->pipeline.video_decode.port_settings_changed == 1) {
                ctx->pipeline.video_decode.port_settings_changed = 0;

                int x = 640;
                int y = 480;

                // rounding the dimensions up to the next multiple of 16.
                x += 0x0f;
                x &= ~0x0f;
                y += 0x0f;
                y &= ~0x0f;
                //the above code is used if resizer is used
                
                //get information about the decoded video
                /*OMX_INIT_STRUCTURE(decoder_config);
                decoder_config.nPortIndex = 131;
                OERR(OMX_GetParameter(ctx->pipeline.video_decode.h, OMX_IndexParamPortDefinition, &decoder_config));
                
                decoder_config.nPortIndex = 190;
                OERR(OMX_GetParameter(ctx->pipeline.image_fx.h, OMX_IndexParamPortDefinition, &decoder_config));
                decoder_config.nPortIndex = 191;
                OERR(OMX_GetParameter(ctx->pipeline.image_fx.h, OMX_IndexParamPortDefinition, &decoder_config));
                */
                OERR(OMX_SetupTunnel(ctx->pipeline.video_decode.h, 131, ctx->pipeline.image_fx.h, 190));
                omx_send_command_and_wait(&ctx->pipeline.video_decode, OMX_CommandPortEnable, 131, NULL);
                omx_send_command_and_wait(&ctx->pipeline.image_fx, OMX_CommandPortEnable, 190, NULL);
                omx_send_command_and_wait(&ctx->pipeline.image_fx, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                
                fprintf(stderr, "configuring deinterlacer done\n");
            }
            
            if(ctx->pipeline.image_fx.port_settings_changed == 1) {
                OMX_ERRORTYPE error;
                ctx->pipeline.image_fx.port_settings_changed = 0;
                 //get info from deinterlacer output
               /* OMX_INIT_STRUCTURE(deinterlacer_config);
                deinterlacer_config.nPortIndex = 191;
                OERR(OMX_GetParameter(ctx->pipeline.image_fx.h, OMX_IndexParamPortDefinition, &deinterlacer_config));
                
                //get default from encoder input
                OMX_INIT_STRUCTURE(encoder_config);
                encoder_config.nPortIndex = 200;
                OERR(OMX_GetParameter(ctx->pipeline.video_encode.h, OMX_IndexParamPortDefinition, &encoder_config));
                //modify it with deinterlacer 
                encoder_config.format.video.nFrameHeight = deinterlacer_config.format.image.nFrameHeight;
                encoder_config.format.video.nFrameWidth = deinterlacer_config.format.image.nFrameWidth;
                encoder_config.format.video.eCompressionFormat = deinterlacer_config.format.image.eCompressionFormat;
                encoder_config.format.video.eColorFormat = deinterlacer_config.format.image.eColorFormat;
                encoder_config.format.video.nSliceHeight = deinterlacer_config.format.image.nSliceHeight;
                encoder_config.format.video.nStride = deinterlacer_config.format.image.nStride;
                //and feed it 
                OERR(OMX_SetParameter(ctx->pipeline.video_encode.h, OMX_IndexParamPortDefinition, &encoder_config));
                */
                //configure encoder output format
                OMX_INIT_STRUCTURE(encoder_format_config);
                encoder_format_config.nPortIndex = 201; //encoder output port
                encoder_format_config.eCompressionFormat = OMX_VIDEO_CodingAVC;
                OERR(OMX_SetParameter(ctx->pipeline.video_encode.h, OMX_IndexParamVideoPortFormat, &encoder_format_config));
                
                //configure encoder output bitrate
                OMX_INIT_STRUCTURE(encoder_bitrate_config);
                encoder_bitrate_config.nPortIndex = 201;
                encoder_bitrate_config.eControlRate = OMX_Video_ControlRateVariable; //var bitrate
                encoder_bitrate_config.nTargetBitrate = ENCODED_BITRATE; //1 mbit 
                OERR(OMX_SetParameter(ctx->pipeline.video_encode.h, OMX_IndexParamVideoBitrate, &encoder_bitrate_config));
                
                //setup tunnel from decoder to encoder
                OERR(OMX_SetupTunnel(ctx->pipeline.image_fx.h, 191, ctx->pipeline.video_encode.h, 200));
                
                //set encoder to idle after we have finished configuring it
                omx_send_command_and_wait0(&ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateIdle, NULL);
                
                //allocate buffers for output of the encoder
                //send the enable command, which won't complete until the bufs are alloc'ed
                omx_send_command_and_wait0(&ctx->pipeline.video_encode, OMX_CommandPortEnable, 201, NULL);
                omx_alloc_buffers(&ctx->pipeline.video_encode, 201); //allocate output buffers
                //block until the port is fully enabled
                omx_send_command_and_wait1(&ctx->pipeline.video_encode, OMX_CommandPortEnable, 201, NULL);

                omx_send_command_and_wait1(&ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateIdle, NULL);
                
                //enable the two ports
                omx_send_command_and_wait0(&ctx->pipeline.image_fx, OMX_CommandPortEnable, 191, NULL);
                omx_send_command_and_wait0(&ctx->pipeline.video_encode, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait1(&ctx->pipeline.video_encode, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait1(&ctx->pipeline.image_fx, OMX_CommandPortEnable, 191, NULL);
                
                omx_send_command_and_wait(&ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                fprintf(stderr, "finished configuring encoder\n");
            }

            if(ctx->pipeline.video_encode.port_settings_changed == 1) {
                fprintf(stderr, "encoder enabled\n");
                ctx->pipeline.video_encode.port_settings_changed = 0;
                //signal the consumer thread it can start polling for data
                pthread_cond_signal(&ctx->is_running_cv);
            }
            
            OERR(OMX_EmptyThisBuffer(ctx->pipeline.video_decode.h, input_buffer));
        }

        packet_queue_free_packet(current_packet, 1);
        current_packet = NULL;
    }

    printf("Finishing stream \n");
    /* Indicate end of video stream */
    input_buffer = omx_get_next_input_buffer(&ctx->pipeline.video_decode);

    input_buffer->nFilledLen = 0;
    input_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

    OERR(OMX_EmptyThisBuffer(ctx->pipeline.video_decode.h, input_buffer));

    // omx_teardown_pipeline(&decoder_ctx->pipeline);
}

//STUF ABOUT THE WRITER THREAD BELOW THIS LINE

static AVFormatContext *makeoutputcontext(AVFormatContext *ic,
        const char *oname, int idx, const OMX_PARAM_PORTDEFINITIONTYPE *prt) {
    AVFormatContext *oc;
    AVOutputFormat *fmt;
    int i;
    AVStream *iflow, *oflow;
    AVCodec *c;
    AVCodecContext *cc;
    const OMX_VIDEO_PORTDEFINITIONTYPE *viddef;
    int streamindex = 0;

    viddef = &prt->format.video;

    fmt = av_guess_format(NULL, oname, NULL);
    if (!fmt) {
        fprintf(stderr, "Can't guess format for %s; defaulting to "
                "MPEG\n",
                oname);
        fmt = av_guess_format(NULL, "MPEG", NULL);
    }
    if (!fmt) {
        fprintf(stderr, "Failed even that.  Bye bye.\n");
        exit(1);
    }

    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Failed to alloc outputcontext\n");
        exit(1);
    }
    oc->oformat = fmt;
    snprintf(oc->filename, sizeof (oc->filename), "%s", oname);
    oc->debug = 1;
    oc->start_time_realtime = ic->start_time;
    oc->start_time = ic->start_time;
    oc->duration = 0;
    oc->bit_rate = 0;

    for (i = 0; i < ic->nb_streams; i++) {
        iflow = ic->streams[i];
        if (i == idx) { /* My new H.264 stream. */
            c = avcodec_find_encoder(CODEC_ID_H264);
            oflow = avformat_new_stream(oc, c);
            cc = oflow->codec;
            cc->width = viddef->nFrameWidth;
            cc->height = viddef->nFrameHeight;
            cc->codec_id = CODEC_ID_H264;
            cc->codec_type = AVMEDIA_TYPE_VIDEO;
            cc->bit_rate = ENCODED_BITRATE;
            cc->time_base = iflow->codec->time_base;

            oflow->avg_frame_rate = iflow->avg_frame_rate;
            oflow->r_frame_rate = iflow->r_frame_rate;
            oflow->start_time = AV_NOPTS_VALUE;

        } else { /* Something pre-existing. */
            c = avcodec_find_encoder(iflow->codec->codec_id);
            oflow = avformat_new_stream(oc, c);
            avcodec_copy_context(oflow->codec, iflow->codec);
            /* Apparently fixes a crash on .mkvs with attachments: */
            av_dict_copy(&oflow->metadata, iflow->metadata, 0);
            /* Reset the codec tag so as not to cause problems with output format */
            oflow->codec->codec_tag = 0;
        }
    }
    for (i = 0; i < oc->nb_streams; i++) {
        if (oc->oformat->flags & AVFMT_GLOBALHEADER)
            oc->streams[i]->codec->flags
                |= CODEC_FLAG_GLOBAL_HEADER;
        if (oc->streams[i]->codec->sample_rate == 0)
            oc->streams[i]->codec->sample_rate = 48000; /* ish */
    }

    return oc;
}


/* Add a audio output stream. */
static
AVStream *
add_audio_stream(AVFormatContext *oc, struct transcoder_ctx_t *ctx) {
    AVCodecContext *c;
    AVStream *st;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_type = AVMEDIA_TYPE_AUDIO;
    c->codec_id = ctx->audio_codec.codec_id;
    c->sample_rate = ctx->audio_codec.sample_rate; 
    c->bit_rate = ctx->audio_codec.bit_rate; 
    c->channels = ctx->audio_codec.channels;
    c->channel_layout = ctx->audio_codec.channels_layout;
    c->sample_fmt = ctx->audio_codec.sample_fmt;
    
    return st;
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

/* Add a video output stream. */
static
AVStream *
add_video_stream(AVFormatContext *oc) {
    AVCodecContext *c;
    AVStream *st;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id =  CODEC_ID_H264;
    c->codec_type = AVMEDIA_TYPE_VIDEO;        
    /* Put sample parameters. */
    c->bit_rate = ENCODED_BITRATE;
    /* Resolution must be a multiple of two. */
    c->width = 720;
    c->height = 576;
    c->time_base.den = 50; //should be 50 due to de-interlacing
    c->time_base.num = 1;
    c->pix_fmt = PIX_FMT_YUV420P;
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
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
    AVFormatContext *output_context;
    AVOutputFormat *fmt;
    AVStream *video_stream = NULL, *audio_stream = NULL;

    //choose a container
    fmt = av_guess_format("mpegts", NULL, NULL);
    if (!fmt) {
        fprintf(stderr, "[DEBUG] Error guessing format, dying\n");
        exit(199);
    }
    
    output_context = avformat_alloc_context();
    if(!output_context) {
        fprintf(stderr, "[DEBUG] Error allocating context, dying\n");
        exit(200);
    }
    
    output_context->oformat = fmt;
    snprintf(output_context->filename, sizeof(output_context->filename), "%s", ctx->output_filename);
    

    
#if 0
    FILE *out_file;

    out_file = fopen(ctx->output_filename, "wb");
    if (out_file == NULL) {
        printf("error creating output file. DYING \n");
        exit(1);
    }
#endif
    if(fmt->video_codec != CODEC_ID_NONE) {
        video_stream = add_video_stream(output_context);
    }
    
    if(fmt->audio_codec != CODEC_ID_NONE) {
        audio_stream = add_audio_stream(output_context, ctx);
    }
    
    //allocate the output file if the container requires it
    if (!(fmt->flags & AVFMT_NOFILE)) {
        fprintf(stderr, "[DEBUG] AVFMT_NOFILE set, allocating output container\n");
        if (avio_open(&output_context->pb, ctx->output_filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "[DEBUG] error creating the output context\n");
            exit(1);
        }
    }
    
    //write stream header if any
    avformat_write_header(output_context, NULL);
    
    pthread_mutex_lock(&ctx->is_running_mutex);
    pthread_cond_wait(&ctx->is_running_cv, &ctx->is_running_mutex);
    
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

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_close(output_context->pb);

    /* free the stream */
    av_free(output_context);
#if 0
    fclose(out_file);
#endif
}