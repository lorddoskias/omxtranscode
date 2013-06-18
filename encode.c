#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "omx.h"
#include "packet_queue.h"
#include "video.h"

/* will be feeding stuff into the encoding pipeline */
void *
encode_thread(void *ctx) {

    OMX_BUFFERHEADERTYPE *input_buffer; // buffer taken from the OMX decoder 
    OMX_BUFFERHEADERTYPE *output_buffer; // buffer taken from the OMX encoder 
    OMX_PARAM_PORTDEFINITIONTYPE resizer_config;
    OMX_PARAM_PORTDEFINITIONTYPE generic_config;
    OMX_VIDEO_PARAM_PORTFORMATTYPE encoder_config; //used for the output of the encoder
    OMX_VIDEO_PARAM_BITRATETYPE bitrate; //used for the output of the encoder
    struct decode_ctx_t *decoder_ctx = (struct decode_ctx_t *) ctx;
    struct packet_t *current_packet;
    int bytes_left;
    FILE *out_file;
    size_t written;
    uint8_t *p; // points to currently copied buffer 

    // set common stuff 
    OMX_INIT_STRUCTURE(resizer_config);
    OMX_INIT_STRUCTURE(bitrate);
    OMX_INIT_STRUCTURE(encoder_config);
    
    out_file = fopen("encoder.h264", "w+");
    if(out_file = NULL) {
        printf("error creating output file. DYING \n");
        exit(1);
    }
    
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

            if (decoder_ctx->pipeline.video_decode.port_settings_changed == 1) {
                fprintf(stderr, "video_decode port_settings_changed = 1\n");
                
                int x = 640;
                int y = 480;
                
                // rounding the dimensions up to the next multiple of 16.
                x += 0x0f;
                x &= ~0x0f;
                y += 0x0f;
                y &= ~0x0f;
                decoder_ctx->pipeline.video_decode.port_settings_changed = 0;
                
                
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
                
                
                //maybe i should invoke the resizer here ??!?!?!
                usleep(40); //this might be waiting for the resizer to be configured
                            //test it with a separate IF block after resizer port changed state
                
                OERR(OMX_GetParameter(decoder_ctx->pipeline.resize.h, OMX_IndexParamPortDefinition, &resizer_config));
                
                //setup the encoder input
                generic_config.nPortIndex = 200; //encoder input port
                generic_config.format.video.nFrameWidth = resizer_config.format.video.nFrameWidth;
                generic_config.format.video.nFrameHeight = resizer_config.format.video.nFrameHeight;
                generic_config.format.video.nStride = resizer_config.format.video.nStride;
                generic_config.format.video.nSliceHeight = resizer_config.format.video.nSliceHeight;
                generic_config.format.video.bFlagErrorConcealment = resizer_config.format.video.bFlagErrorConcealment;
                generic_config.format.video.eCompressionFormat = resizer_config.format.video.eCompressionFormat;
                generic_config.format.video.eColorFormat = resizer_config.format.video.eColorFormat;
                generic_config.format.video.pNativeWindow = resizer_config.format.video.pNativeWindow;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamPortDefinition, &generic_config));
                
                
                
                //tunnel from decoder to resizer 
                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.video_decode.h, 131, decoder_ctx->pipeline.resize.h, 60));
                //tunnel from resizer to encoder
                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.resize.h, 61, decoder_ctx->pipeline.video_encode.h, 200));
            
                //configure encoder output format
                encoder_config.nPortIndex = 201; //encoder output port
                encoder_config.eCompressionFormat = OMX_VIDEO_CodingAVC;
                OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamVideoPortFormat, &encoder_config));
                
                //configure encoder output bitrate
                bitrate.nPortIndex = 201; //encoder output
                bitrate.eControlRate = OMX_Video_ControlRateVariable; //var bitrate
                bitrate.nTargetBitrate = 1000000; //1 mbit 
               	OERR(OMX_SetParameter(decoder_ctx->pipeline.video_encode.h, OMX_IndexParamVideoBitrate, &bitrate));
                
                //allocate buffers for already enabled port
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode, OMX_CommandPortEnable, 201, NULL);
                omx_alloc_buffers(&decoder_ctx->pipeline.video_encode, 201); //allocate output buffers
                
                //enable decoder -> resizer -> encoder ports 
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_decode, OMX_CommandPortEnable, 131, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandPortEnable, 60, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandPortEnable, 61,NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode, OMX_CommandPortEnable, 200, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandStateSet, OMX_StateIdle, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode,OMX_CommandStateSet, OMX_StateIdle, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.resize, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_encode, OMX_CommandStateSet, OMX_StateExecuting, NULL);
            }

            OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, input_buffer));
            
            output_buffer = omx_get_next_input_buffer(&decoder_ctx->pipeline.video_encode);
            OERR(OMX_FillThisBuffer(decoder_ctx->pipeline.video_decode.h, output_buffer));


            if (output_buffer != NULL) {

                written = fwrite(output_buffer->pBuffer, 1, output_buffer->nFilledLen, output_buffer);
                if (written != output_buffer->nFilledLen) {
                    printf("fwrite: Error emptying buffer. Written %d bytes out of %d!\n", written, output_buffer->nFilledLen);
                }
                output_buffer->nFilledLen = 0;
            } else {
                printf("Not getting it :(\n");
            }
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

   // omx_teardown_pipeline(&decoder_ctx->pipeline);
}
