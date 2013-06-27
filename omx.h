/* 
 * File:   omx.h
 * Author: lorddoskias
 *
 * Created on June 15, 2013, 11:07 AM
 */

#ifndef OMX_H
#define	OMX_H

#include "include/interface/vcos/vcos.h"
#include "include/IL/OMX_Broadcom.h"
#include "packet_queue.h"

#ifdef	__cplusplus
extern "C" {
#endif


#define OERR(cmd)       do {                                            \
                                    OMX_ERRORTYPE oerr = cmd;               \
                                    if (oerr != OMX_ErrorNone) {            \
                                            fprintf(stderr, #cmd " failed on line %d: %x\n",  __LINE__, oerr);        \
                                            exit(1);                        \
                                    }                                       \
                            } while (0)   

#define OMX_INIT_STRUCTURE(a) \
        memset(&(a), 0, sizeof(a)); \
        (a).nSize = sizeof(a); \
        (a).nVersion.nVersion = OMX_VERSION

#define OMX_MIN(a,b) (((a) < (b)) ? (a) : (b))
    
    
    /* used for identifying commands to block on */
    struct omx_cmd_t {
        OMX_HANDLETYPE *hComponent;
        OMX_COMMANDTYPE Cmd;
        OMX_U32 nParam;
        OMX_PTR pCmdData;
    };

    struct omx_component_t {
        OMX_HANDLETYPE h;
        OMX_CALLBACKTYPE callbacks;

        char* name;
        
        /* Variables for handling asynchronous commands */
        struct omx_cmd_t cmd;
        pthread_mutex_t cmd_queue_mutex;
        pthread_cond_t cmd_queue_count_cv;

        /* Pointer to parent pipeline */
        struct omx_pipeline_t* pipe;
        
        int port_settings_changed;
        int config_changed;

        //input buffers
        pthread_mutex_t buf_in_mutex;
        OMX_BUFFERHEADERTYPE *in_buffers;
        int buf_in_notempty;
        pthread_cond_t buf_in_notempty_cv;

        //output buffers
        pthread_mutex_t buf_out_mutex;
        OMX_BUFFERHEADERTYPE *out_buffers;
        int buf_out_notempty;
        pthread_cond_t buf_out_notempty_cv;
        
        pthread_mutex_t eos_mutex;
        int eos;
        pthread_cond_t eos_cv;
        
        pthread_mutex_t is_running_mutex;
        pthread_cond_t is_running_cv;
    };

    struct omx_pipeline_t {
        struct omx_component_t video_decode;
        struct omx_component_t image_fx;
        struct omx_component_t video_encode;
        
        //holds processed packets from the encoder 
        struct packet_queue_t encoded_video_queue;
    };

OMX_ERRORTYPE omx_init_component(struct omx_pipeline_t* pipe, struct omx_component_t* component, char* compname);
OMX_ERRORTYPE omx_setup_pipeline(struct omx_pipeline_t* pipe, OMX_VIDEO_CODINGTYPE video_codec);
OMX_ERRORTYPE omx_setup_encoding_pipeline(struct omx_pipeline_t* pipe, OMX_VIDEO_CODINGTYPE video_codec);
OMX_BUFFERHEADERTYPE *omx_get_next_input_buffer(struct omx_component_t* component);
OMX_BUFFERHEADERTYPE *omx_get_next_output_buffer(struct omx_component_t* component);
OMX_ERRORTYPE omx_send_command_and_wait(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData);
OMX_ERRORTYPE omx_send_command_and_wait0(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData); 
OMX_ERRORTYPE omx_send_command_and_wait1(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData);
void omx_alloc_buffers(struct omx_component_t *component, int port); 
int omx_get_free_buffer_count(struct omx_component_t* component);
void omx_teardown_pipeline(struct omx_pipeline_t* pipe);
OMX_TICKS pts_to_omx(uint64_t pts);
uint64_t omx_to_pts(OMX_TICKS ticks);

#ifdef	__cplusplus
}
#endif

#endif	/* OMX_H */

