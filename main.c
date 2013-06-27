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
#include <pthread.h>

#include "avformat.h"
#include "bcm_host.h"
#include "include/IL/OMX_Core.h"
#include "packet_queue.h"
#include "demux.h"
#include "omx.h"
#include "encode.h"

/* Data types */


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
mpeg2_codec_enabled(void) {
    char response[512];

    vc_gencmd_init();

    vc_gencmd(response, sizeof (response), "codec_enabled MPG2");

    if (strcmp(response, "MPG2=enabled") == 0) {
        return 1;
    } else {
        return 0;
    }
}

struct av_demux_t *
init_demux(const char *input_file) {
    struct av_demux_t *demux_ctx;

    demux_ctx = malloc(sizeof (*demux_ctx));

    //copy the input file name
    demux_ctx->input_filename = malloc(strlen(input_file) + 1);
    memcpy(demux_ctx->input_filename, input_file, strlen(input_file) + 1);

    demux_ctx->video_queue = malloc(sizeof (struct packet_queue_t));
    packet_queue_init(demux_ctx->video_queue);

    demux_ctx->audio_queue = malloc(sizeof (struct packet_queue_t));
    packet_queue_init(demux_ctx->audio_queue);


    if (avformat_open_input(&demux_ctx->input_context, demux_ctx->input_filename, NULL, NULL) < 0) {
        printf("Error opening input file: %s\n", demux_ctx->input_filename);
        abort();
    }

    if (avformat_find_stream_info(demux_ctx->input_context, NULL) < 0) {
        printf("error finding streams\n");
        abort();
    }

    return demux_ctx;
}

struct decode_ctx_t *
init_decode(struct av_demux_t *demux_ctx, const char *output_file) {
    struct decode_ctx_t *decoder_ctx;

    decoder_ctx = malloc(sizeof (*decoder_ctx));

    decoder_ctx->input_video_queue = demux_ctx->video_queue;
    decoder_ctx->first_packet = 1;
    
    decoder_ctx->processed_audio_queue = demux_ctx->audio_queue;
    decoder_ctx->audio_codec = &demux_ctx->audio_codec;
    
    //copy the output file name
    decoder_ctx->output_filename = malloc(strlen(output_file) + 1);
    memcpy(decoder_ctx->output_filename, output_file, strlen(output_file) + 1);
    
    pthread_mutex_init(&decoder_ctx->is_running_mutex, NULL);
    pthread_cond_init(&decoder_ctx->is_running_cv, NULL);

    return decoder_ctx;
}


int main(int argc, char **argv) {

    pthread_t demux_tid = 0;
    pthread_t writer_tid = 0;
    pthread_t encoder_tid = 0;
    
    pthread_attr_t attr;
    int status;
    struct av_demux_t *demux_ctx;
    struct decode_ctx_t *decoder_ctx;
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    //global initialization of resources
    //these must be here to prevent sporadic failures
    bcm_host_init();
    OERR(OMX_Init());
    av_register_all();
    avformat_network_init();
    
    demux_ctx = init_demux(argv[1]);
    decoder_ctx = init_decode(demux_ctx, argv[2]);

    // start the thread that will pump packets in the queue 
    status = pthread_create(&demux_tid, &attr, demux_thread, demux_ctx);
    if(status) {
        fprintf(stderr,"Error creating demux thread : %d\n", status);
    }
    
    status = pthread_create(&encoder_tid, &attr, decode_thread, decoder_ctx);
    if (status) {
        fprintf(stderr,"Error creating decoder thread : %d\n", status);
    }
     
    status = pthread_create(&writer_tid, &attr, writer_thread, decoder_ctx);
    if (status) {
        fprintf(stderr,"Error creating file writer thread : %d\n", status);
    }

    // block until the demux and decoder are finished finished
    pthread_join(demux_tid, NULL);
    pthread_join(encoder_tid, NULL);
    pthread_join(writer_tid, NULL);
    printf("the other threads have terminating, i'm dying as well\n");
    // do any cleanup
    OERR(OMX_Deinit());
    avformat_close_input(&demux_ctx->input_context);
    free(demux_ctx->input_filename);
    free(demux_ctx->video_queue);
    free(demux_ctx->audio_queue);
    free(decoder_ctx->output_filename);
    free(demux_ctx); 
    pthread_attr_destroy(&attr);
}



/*
int main(int argc, char **argv) {
    
    struct omx_component_t render;
    OMX_IMAGE_PARAM_PORTFORMATTYPE render_format;
    
    OMX_INIT_STRUCTURE(render_format);
    render_format.nPortIndex = 60;
    bcm_host_init();
    OERR(OMX_Init());
    omx_init_component(NULL, &render, "OMX.broadcom.resize");
    
   OERR(OMX_GetParameter(render.h, OMX_IndexParamImagePortFormat, &render_format));
   
   printf("supported color format: %x\n", render_format.eColorFormat);
   printf("supported compression format: %d\n", render_format.eCompressionFormat);
   
   OERR(OMX_Deinit());
    
}
*/