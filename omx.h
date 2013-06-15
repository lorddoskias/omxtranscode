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

        OMX_BUFFERHEADERTYPE *buffers;
        int port_settings_changed;
        int config_changed;

        pthread_mutex_t buf_mutex;
        int buf_notempty;
        pthread_cond_t buf_notempty_cv;

        pthread_mutex_t eos_mutex;
        int eos;
        pthread_cond_t eos_cv;
    };


#ifdef	__cplusplus
}
#endif

#endif	/* OMX_H */

