/**
 * @param argc
 * @param argv
 * @return 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "include/bcm_host.h"
#include "include/IL/OMX_Core.h"
#include "packet_queue.h"
#include "demux.h"

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


int main(int argc, char **argv) {


    pthread_t demux_tid = 0;
    int status;
    struct packet_queue_t *decoder; 
    struct av_demux_t *demux;
    
    demux = malloc(sizeof(struct av_demux_t));
    decoder = malloc(sizeof(struct packet_queue_t));
    
    //copy the input file name
    demux->input_filename = malloc(strlen(argv[1]) + 1);
    memcpy(demux->input_filename, argv[1], strlen(argv[1]) + 1);
    
     //copy the output file name
    demux->output_filename = malloc(strlen(argv[2]) + 1);
    memcpy(demux->output_filename, argv[2], strlen(argv[2]) + 1);
    
    demux->queue = decoder;
    
    status = pthread_create(&demux_tid, NULL, demux_thread, demux);
    if(status) {
        printf("Error creating thread : %d\n", status);
    }
    
    pthread_exit(NULL);
     
}


