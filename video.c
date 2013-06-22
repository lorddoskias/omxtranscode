/**
 * 
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packet_queue.h"
#include "video.h"
#include "omx.h"

#if 0
void *
video_thread(void *ctx) {

    OMX_BUFFERHEADERTYPE *buf; /* buffer taken from the OMX decoder */
    struct decode_ctx_t *decoder_ctx = (struct decode_ctx_t *) ctx;
    struct packet_t *current_packet;
    int bytes_left;
    uint8_t *p; /* points to currently copied buffer */

    omx_setup_pipeline(&decoder_ctx->pipeline, OMX_VIDEO_CodingAVC);
    
    /* main loop that will poll packets and render*/
    while (decoder_ctx->video_queue->queue_count != 0 || decoder_ctx->video_queue->queue_finished != 1) {
        current_packet = packet_queue_get_next_item(decoder_ctx->video_queue);
        p = current_packet->data;
        bytes_left = current_packet->data_length;

        while (bytes_left > 0) {

            fprintf(stderr, "OMX buffers: v: %02d/20, vcodec queue: %4d\r", omx_get_free_buffer_count(&decoder_ctx->pipeline.video_decode), decoder_ctx->video_queue->queue_count);
            buf = omx_get_next_input_buffer(&decoder_ctx->pipeline.video_decode); /* This will block if there are no empty buffers */

            /* copy at most the length of the OMX buf*/
            int copy_length = OMX_MIN(bytes_left, buf->nAllocLen);

            memcpy(buf->pBuffer, p, copy_length);
            p += copy_length;
            bytes_left -= copy_length;

            buf->nFilledLen = copy_length;
//            buf->nTimeStamp = pts_to_omx(current_packet->PTS);
            
            if (decoder_ctx->first_packet) {
                buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
                decoder_ctx->first_packet = 0;
            } else {
                //taken from ilclient
                buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
            }

            if (decoder_ctx->pipeline.video_decode.port_settings_changed == 1) {
                decoder_ctx->pipeline.video_decode.port_settings_changed = 0;

                fprintf(stderr, "video_decode port_settings_changed = 1\n");
                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.video_decode.h, 131, decoder_ctx->pipeline.video_scheduler.h, 10));
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_decode, OMX_CommandPortEnable, 131, NULL);

                omx_send_command_and_wait(&decoder_ctx->pipeline.video_scheduler, OMX_CommandPortEnable, 10, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_scheduler, OMX_CommandStateSet, OMX_StateExecuting, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_render, OMX_CommandStateSet, OMX_StateIdle, NULL);
            }

            if (decoder_ctx->pipeline.video_scheduler.port_settings_changed == 1) {
                decoder_ctx->pipeline.video_scheduler.port_settings_changed = 0;
                fprintf(stderr, "video_scheduler port_settings_changed = 1\n");

                OERR(OMX_SetupTunnel(decoder_ctx->pipeline.video_scheduler.h, 11, decoder_ctx->pipeline.video_render.h, 90));
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_scheduler, OMX_CommandPortEnable, 11, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_render, OMX_CommandPortEnable, 90, NULL);
                omx_send_command_and_wait(&decoder_ctx->pipeline.video_render, OMX_CommandStateSet, OMX_StateExecuting, NULL);
            }

            OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, buf));
        }

        packet_queue_free_item(current_packet);
        current_packet = NULL;
    }

    printf("Finishing stream \n");
    /* Indicate end of video stream */
    buf = omx_get_next_input_buffer(&decoder_ctx->pipeline.video_decode);

    buf->nFilledLen = 0;
    buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

    OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, buf));

    omx_teardown_pipeline(&decoder_ctx->pipeline);
}
#endif