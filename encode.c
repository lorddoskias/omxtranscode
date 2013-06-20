#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "omx.h"
#include "packet_queue.h"
#include "video.h"

/* will be feeding stuff into the encoding pipeline */
void *
decode_thread(void *ctx) {

    OMX_BUFFERHEADERTYPE *input_buffer; // buffer taken from the OMX decoder 
    OMX_BUFFERHEADERTYPE *output_buffer; // buffer taken from the OMX encoder 
    OMX_PARAM_PORTDEFINITIONTYPE resizer_config;
    OMX_PARAM_PORTDEFINITIONTYPE generic_config;
    OMX_VIDEO_PARAM_PORTFORMATTYPE encoder_config; //used for the output of the encoder
    OMX_VIDEO_PARAM_BITRATETYPE bitrate; //used for the output of the encoder
    struct decode_ctx_t *decoder_ctx = (struct decode_ctx_t *) ctx;
    struct packet_t *current_packet;
    int bytes_left;
    uint8_t *p; // points to currently copied buffer 

    // set common stuff 
    OMX_INIT_STRUCTURE(generic_config);
    OMX_INIT_STRUCTURE(resizer_config);
    OMX_INIT_STRUCTURE(bitrate);
    OMX_INIT_STRUCTURE(encoder_config);
    
    omx_setup_encoding_pipeline(&decoder_ctx->pipeline, OMX_VIDEO_CodingAVC);
    
    // main loop that will poll packets and render
    while (decoder_ctx->video_queue->queue_count != 0 || decoder_ctx->video_queue->queue_finished != 1) {
        current_packet = packet_queue_get_next_item(decoder_ctx->video_queue);
        p = current_packet->data;
        bytes_left = current_packet->data_length;

        while (bytes_left > 0) {

            fprintf(stderr, "OMX buffers: v: %02d/20, vcodec queue: %4d\r", omx_get_free_buffer_count(&decoder_ctx->pipeline.video_decode), decoder_ctx->video_queue->queue_count);
            input_buffer = omx_get_next_input_buffer(&decoder_ctx->pipeline.video_decode); // This will block if there are no empty buffers

            // copy at most the length of the OMX buf
            int copy_length = OMX_MIN(bytes_left, input_buffer->nAllocLen);

            memcpy(input_buffer->pBuffer, p, copy_length);
            p += copy_length;
            bytes_left -= copy_length;

            input_buffer->nFilledLen = copy_length;
            
            if (decoder_ctx->first_packet) {
                input_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;
                decoder_ctx->first_packet = 0;
            } else {
                //taken from ilclient
                input_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
            }

            //configure the resizer after the decoder has got output
            if (decoder_ctx->pipeline.video_decode.port_settings_changed == 1) {
                decoder_ctx->pipeline.video_decode.port_settings_changed = 0;
                
                int x = 640;
                int y = 480;
                
                // rounding the dimensions up to the next multiple of 16.
                x += 0x0f;
                x &= ~0x0f;
                y += 0x0f;
                y &= ~0x0f;
                
                //get information about the decoded video
                generic_config.nPortIndex = 131;
                OERR(OMX_GetParameter(decoder_ctx->pipeline.video_decode.h, OMX_IndexParamPortDefinition, &generic_config));
                
                //configure input of resizer
                resizer_config.nPortIndex = 60;
                OERR(OMX_GetParameter(decoder_ctx->pipeline.resize.h, OMX_IndexParamPortDefinition, &resizer_config));
                
                resizer_config.format.image.nFrameWidth = generic_config.format.video.nFrameWidth;
                resizer_config.format.image.nFrameHeight = generic_config.format.video.nFrameHeight;
                resizer_config.format.image.nStride = generic_config.format.video.nStride;
                resizer_config.format.image.nSliceHeight = generic_config.format.video.nSliceHeight;
                resizer_config.format.image.bFlagErrorConcealment = generic_config.format.video.bFlagErrorConcealment;
                resizer_config.format.image.eCompressionFormat = generic_config.format.video.eCompressionFormat;
                resizer_config.format.image.eColorFormat = generic_config.format.video.eColorFormat;
                resizer_config.format.image.pNativeWindow = generic_config.format.video.pNativeWindow;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.resize.h, OMX_IndexParamPortDefinition, &resizer_config));
                
                //configure output of resizer 
                resizer_config.nPortIndex = 61; //output port 
                resizer_config.format.image.nStride = 0;
                resizer_config.format.image.nSliceHeight = 0;
                resizer_config.format.image.nFrameWidth = 640;
                resizer_config.format.image.nFrameHeight = 480;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.resize.h, OMX_IndexParamPortDefinition, &resizer_config));
                
                //tunnel from decoder to resizer 
                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.video_decode.h, 131, decoder_ctx->pipeline.resize.h, 60));
                
                //request stateidle, which will be set only after we have enabled the ports
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandStateSet, OMX_StateIdle, NULL);
                
                //enable decoder -> resizer ports 
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_decode, OMX_CommandPortEnable, 131, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandPortEnable, 60, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                
                fprintf(stderr,"finished resizer config\n");
                
            }
            
            //configure the encoder after the resizer has got output
            if(decoder_ctx->pipeline.resize.port_settings_changed == 1) {
                decoder_ctx->pipeline.resize.port_settings_changed = 0;
                
                fprintf(stderr, "resize port_settings_changed = 1\n");
                
                resizer_config.nPortIndex = 61; //resizer output port 
                OERR(OMX_GetParameter(decoder_ctx->pipeline.resize.h, OMX_IndexParamPortDefinition, &resizer_config));
                
                //setup the encoder input from resizer's output
                generic_config.nPortIndex = 200; //encoder input port
                generic_config.format.video.nFrameWidth = resizer_config.format.image.nFrameWidth;
                generic_config.format.video.nFrameHeight = resizer_config.format.image.nFrameHeight;
                generic_config.format.video.nStride = resizer_config.format.image.nStride;
                generic_config.format.video.nSliceHeight = resizer_config.format.image.nSliceHeight;
                generic_config.format.video.bFlagErrorConcealment = resizer_config.format.image.bFlagErrorConcealment;
                generic_config.format.video.eCompressionFormat = resizer_config.format.image.eCompressionFormat;
                generic_config.format.video.eColorFormat = resizer_config.format.image.eColorFormat;
                generic_config.format.video.pNativeWindow = resizer_config.format.image.pNativeWindow;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamPortDefinition, &generic_config));
                
                
                //configure encoder output format
                encoder_config.nPortIndex = 201; //encoder output port
                encoder_config.eCompressionFormat = OMX_VIDEO_CodingAVC;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamVideoPortFormat, &encoder_config));
                
                //configure encoder output bitrate
                bitrate.nPortIndex = 201;
                bitrate.eControlRate = OMX_Video_ControlRateVariable; //var bitrate
                bitrate.nTargetBitrate = 1000000; //1 mbit 
               	OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamVideoBitrate, &bitrate));
                
                //set encoder to idle after we have finished configuring it
                omx_send_command_and_wait0(&decoder_ctx->pipeline.video_encode,OMX_CommandStateSet, OMX_StateIdle, NULL);
                
                 //tunnel from resizer to encoder
                //this has to be done before the encoder's output port is enabled
                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.resize.h, 61, decoder_ctx->pipeline.video_encode.h, 200));

                //allocate buffers for output of the encoder
                //send the enable command, which won't complete until the bufs are alloc'ed
                omx_send_command_and_wait0(&decoder_ctx->pipeline.video_encode, OMX_CommandPortEnable, 201, NULL);
                omx_alloc_buffers(&decoder_ctx->pipeline.video_encode, 201); //allocate output buffers
                //block until the port is fully enabled
                omx_send_command_and_wait1(&decoder_ctx->pipeline.video_encode, OMX_CommandPortEnable, 201, NULL);
                
                omx_send_command_and_wait1(&decoder_ctx->pipeline.video_encode,OMX_CommandStateSet, OMX_StateIdle, NULL);
                
                
                //enable resizer -> encoder ports
               /* omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandPortEnable, 61, NULL); */
                //these have to be asynch 
                OMX_SendCommand(decoder_ctx->pipeline.resize.h, OMX_CommandPortEnable, 61, NULL);
                OMX_SendCommand(decoder_ctx->pipeline.video_encode.h, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateExecuting, NULL);
            
                fprintf(stderr,"encoder config finished\n");
            }

            OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, input_buffer));
            
        }

        packet_queue_free_item(current_packet);
        current_packet = NULL;
    }

    printf("Finishing stream \n");
    /* Indicate end of video stream */
    input_buffer = omx_get_next_input_buffer(&decoder_ctx->pipeline.video_decode);

    input_buffer->nFilledLen = 0;
    input_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

    OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, input_buffer));
    
    decoder_ctx->video_queue->queue_finished = 1;
   // omx_teardown_pipeline(&decoder_ctx->pipeline);
}

void *consumer_thread(void *thread_ctx) {
    struct decode_ctx_t *ctx = (struct decode_ctx_t *) thread_ctx;

    while (!ctx->video_queue->queue_finished || ctx->pipeline.encoded_video_queue.queue_count > 0) {
        if (ctx->pipeline.video_encode.port_settings_changed == 1) {
            OERR(OMX_FillThisBuffer(ctx->pipeline.video_encode.h, omx_get_next_output_buffer(&ctx->pipeline.video_encode)));
        }
    }
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


    while (!ctx->video_queue->queue_finished || ctx->pipeline.encoded_video_queue.queue_count > 0) {
        struct packet_t *encoded_packet = packet_queue_get_next_item(&ctx->pipeline.encoded_video_queue);
        written = fwrite(encoded_packet->data, 1, encoded_packet->data_length, out_file);
        packet_queue_free_item(encoded_packet);
    }

    fclose(out_file);
}