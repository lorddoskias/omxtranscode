#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"
#include "packet_queue.h"
#include "video.h"
#include "omx.h"

static int video_decode_test(char *filename) {
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
    COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
    COMPONENT_T * list[5];
    TUNNEL_T tunnel[4];
    ILCLIENT_T *client;
    FILE *in;
    int status = 0;
    unsigned int data_len = 0;
    int packet_size = 80 << 10;

    memset(list, 0, sizeof (list));
    memset(tunnel, 0, sizeof (tunnel));

    if ((in = fopen(filename, "rb")) == NULL)
        return -2;

    if ((client = ilclient_init()) == NULL) {
        fclose(in);
        return -3;
    }

    if (OMX_Init() != OMX_ErrorNone) {
        ilclient_destroy(client);
        fclose(in);
        return -4;
    }

    // create video_decode
    if (ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
        status = -14;
    list[0] = video_decode;

    // create video_render
    if (status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
        status = -14;
    list[1] = video_render;

    // create clock
    if (status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
        status = -14;
    list[2] = clock;

    memset(&cstate, 0, sizeof (cstate));
    cstate.nSize = sizeof (cstate);
    cstate.nVersion.nVersion = OMX_VERSION;
    cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
    cstate.nWaitMask = 1;
    if (clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
        status = -13;

    // create video_scheduler
    if (status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
        status = -14;
    list[3] = video_scheduler;

    set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
    set_tunnel(tunnel + 1, video_scheduler, 11, video_render, 90);
    set_tunnel(tunnel + 2, clock, 80, video_scheduler, 12);

    // setup clock tunnel first
    if (status == 0 && ilclient_setup_tunnel(tunnel + 2, 0, 0) != 0)
        status = -15;
    else
        ilclient_change_component_state(clock, OMX_StateExecuting);

    if (status == 0)
        ilclient_change_component_state(video_decode, OMX_StateIdle);

    memset(&format, 0, sizeof (OMX_VIDEO_PARAM_PORTFORMATTYPE));
    format.nSize = sizeof (OMX_VIDEO_PARAM_PORTFORMATTYPE);
    format.nVersion.nVersion = OMX_VERSION;
    format.nPortIndex = 130;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;

    if (status == 0 &&
            OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
            ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0) {
        OMX_BUFFERHEADERTYPE *buf;
        int port_settings_changed = 0;
        int first_packet = 1;

        ilclient_change_component_state(video_decode, OMX_StateExecuting);

        while ((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL) {
            // feed data and wait until we get port settings changed
            unsigned char *dest = buf->pBuffer;

            data_len += fread(dest, 1, packet_size - data_len, in);

            if (port_settings_changed == 0 &&
                    ((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
                    (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                    ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0))) {
                port_settings_changed = 1;

                if (ilclient_setup_tunnel(tunnel, 0, 0) != 0) {
                    status = -7;
                    break;
                }

                ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

                // now setup tunnel to video_render
                if (ilclient_setup_tunnel(tunnel + 1, 0, 1000) != 0) {
                    status = -12;
                    break;
                }

                ilclient_change_component_state(video_render, OMX_StateExecuting);
            }
            if (!data_len)
                break;

            buf->nFilledLen = data_len;
            data_len = 0;

            buf->nOffset = 0;
            if (first_packet) {
                buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
                first_packet = 0;
            } else
                buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

            if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone) {
                status = -6;
                break;
            }
        }

        buf->nFilledLen = 0;
        buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

        if (OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
            status = -20;

        // wait for EOS from render
        ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
                ILCLIENT_BUFFER_FLAG_EOS, 10000);

        // need to flush the renderer to allow video_decode to disable its input port
        ilclient_flush_tunnels(tunnel, 0);

        ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
    }

    fclose(in);

    ilclient_disable_tunnel(tunnel);
    ilclient_disable_tunnel(tunnel + 1);
    ilclient_disable_tunnel(tunnel + 2);
    ilclient_teardown_tunnels(tunnel);

    ilclient_state_transition(list, OMX_StateIdle);
    ilclient_state_transition(list, OMX_StateLoaded);

    ilclient_cleanup_components(list);

    OMX_Deinit();

    ilclient_destroy(client);
    return status;
}

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
            buf = get_next_buffer(&decoder_ctx->pipeline.video_decode); /* This will block if there are no empty buffers */

            /* copy at most the length of the OMX buf*/
            int copy_length = OMX_MIN(current_packet->data_length, buf->nAllocLen);

            memcpy(buf->pBuffer, current_packet->data, copy_length);
            p += copy_length;
            bytes_left -= copy_length;

            buf->nFilledLen = copy_length;
            buf->nTimeStamp = pts_to_omx(current_packet->PTS);
            
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
    buf = get_next_buffer(&decoder_ctx->pipeline.video_decode);

    buf->nFilledLen = 0;
    buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

    OERR(OMX_EmptyThisBuffer(decoder_ctx->pipeline.video_decode.h, buf));

    omx_teardown_pipeline(&decoder_ctx->pipeline);
}
