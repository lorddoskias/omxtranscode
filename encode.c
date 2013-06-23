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
#include "video.h"

/* will be feeding stuff into the encoding pipeline */
void *
decode_thread(void *context) {

    OMX_BUFFERHEADERTYPE *input_buffer; // buffer taken from the OMX decoder 
    OMX_PARAM_PORTDEFINITIONTYPE encoder_config;
    OMX_PARAM_PORTDEFINITIONTYPE decoder_config;
    OMX_PARAM_PORTDEFINITIONTYPE deinterlacer_config;
    OMX_VIDEO_PARAM_PORTFORMATTYPE encoder_format_config; //used for the output of the encoder
    OMX_VIDEO_PARAM_BITRATETYPE encoder_bitrate_config; //used for the output of the encoder
    struct decode_ctx_t *ctx = (struct decode_ctx_t *) context;
    struct packet_t *current_packet;
    int bytes_left;
    uint8_t *p; // points to currently copied buffer 

    //    omx_setup_encoding_pipeline(&decoder_ctx->pipeline, OMX_VIDEO_CodingAVC);
    omx_setup_encoding_pipeline(&ctx->pipeline, OMX_VIDEO_CodingMPEG2);

    // main loop that will poll packets and render
    while (ctx->video_queue->queue_count != 0 || ctx->video_queue->queue_finished != 1) {
        current_packet = packet_queue_get_next_item(ctx->video_queue);
        p = current_packet->data;
        bytes_left = current_packet->data_length;

        while (bytes_left > 0) {

            fprintf(stderr, "OMX buffers: v: %02d/20, vcodec queue: %4d\r", omx_get_free_buffer_count(&ctx->pipeline.video_decode), ctx->video_queue->queue_count);
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
                ctx->pipeline.image_fx.port_settings_changed = 0;
                OMX_CONFIG_FRAMERATETYPE framerate_config;
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
                
                OMX_INIT_STRUCTURE(framerate_config);
                framerate_config.nPortIndex = 201;
                framerate_config.xEncodeFramerate = 50;
                OERR(OMX_SetConfig(ctx->pipeline.video_encode.h, OMX_IndexConfigVideoFramerate, &framerate_config));

                //configure encoder output bitrate
                OMX_INIT_STRUCTURE(encoder_bitrate_config);
                encoder_bitrate_config.nPortIndex = 201;
                encoder_bitrate_config.eControlRate = OMX_Video_ControlRateVariable; //var bitrate
                encoder_bitrate_config.nTargetBitrate = 2000000; //1 mbit 
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
                OMX_SendCommand(ctx->pipeline.image_fx.h, OMX_CommandPortEnable, 191, NULL);
                OMX_SendCommand(ctx->pipeline.video_encode.h, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait(&ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                
                fprintf(stderr, "finished configuring encoder\n");
            }

            if(ctx->pipeline.video_encode.port_settings_changed == 1) {
                ctx->pipeline.video_encode.port_settings_changed = 0;
                //signal the consumer thread it can start polling for data
                pthread_cond_signal(&ctx->is_running_cv);
            }
            
            OERR(OMX_EmptyThisBuffer(ctx->pipeline.video_decode.h, input_buffer));
        }

        packet_queue_free_item(current_packet);
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


void *writer_thread(void *thread_ctx) {

    struct decode_ctx_t *ctx = (struct decode_ctx_t *) thread_ctx;
    int written;
    FILE *out_file;

    out_file = fopen(ctx->output_filename, "wb");
    if (out_file == NULL) {
        printf("error creating output file. DYING \n");
        exit(1);
    }
    
    pthread_mutex_lock(&ctx->is_running_mutex);
    pthread_cond_wait(&ctx->is_running_cv, &ctx->is_running_mutex);
    
    while (!ctx->pipeline.video_encode.eos) {
        //fill a buffer with data 
        OERR(OMX_FillThisBuffer(ctx->pipeline.video_encode.h, omx_get_next_output_buffer(&ctx->pipeline.video_encode)));
        
        //encoded_video_queue is being filled by the previous command
        struct packet_t *encoded_packet = packet_queue_get_next_item(&ctx->pipeline.encoded_video_queue);
        written = fwrite(encoded_packet->data, 1, encoded_packet->data_length, out_file);
        packet_queue_free_item(encoded_packet);
    }

    fclose(out_file);
}