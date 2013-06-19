/**
 * holds some util stuff for dealing with omx
 * 
 */

#include <stdio.h>
#include <string.h>

#include "omx.h"
#include "packet_queue.h"

OMX_TICKS pts_to_omx(uint64_t pts)
{
  OMX_TICKS ticks;
  ticks.nLowPart = pts;
  ticks.nHighPart = pts >> 32;
  return ticks;
};
/**
 * Populates the omx_cmd_t struct inside the component struct
 * 
 * @param component
 * @param Cmd
 * @param nParam
 * @param pCmdData
 * @return 
 */
OMX_ERRORTYPE
omx_send_command_and_wait0(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData) 
{
    pthread_mutex_lock(&component->cmd_queue_mutex);
    component->cmd.hComponent = component->h;
    component->cmd.Cmd = Cmd;
    component->cmd.nParam = nParam;
    component->cmd.pCmdData = pCmdData;
    pthread_mutex_unlock(&component->cmd_queue_mutex);

    OMX_SendCommand(component->h, Cmd, nParam, pCmdData);
}
/**
 * Blocks until the event handler signals the cond variable.
 * @param component
 * @param Cmd
 * @param nParam
 * @param pCmdData
 * @return 
 */
OMX_ERRORTYPE
omx_send_command_and_wait1(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData) 
{
    pthread_mutex_lock(&component->cmd_queue_mutex);
    while (component->cmd.hComponent) {
        /* pthread_cond_wait releases the mutex (which must be locked) and blocks on the condition variable */
        pthread_cond_wait(&component->cmd_queue_count_cv, &component->cmd_queue_mutex);
    }
    pthread_mutex_unlock(&component->cmd_queue_mutex);

}

OMX_ERRORTYPE 
omx_send_command_and_wait(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData)
{
  omx_send_command_and_wait0(component,Cmd,nParam,pCmdData);
  omx_send_command_and_wait1(component,Cmd,nParam,pCmdData);
}


/* The event handler is called from the OMX component thread */
static 
OMX_ERRORTYPE 
omx_event_handler(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_EVENTTYPE eEvent,
        OMX_IN OMX_U32 nData1,
        OMX_IN OMX_U32 nData2,
        OMX_IN OMX_PTR pEventData) 
{
    
    struct omx_component_t* component = (struct omx_component_t*) pAppData;

    switch (eEvent) {
        case OMX_EventError:
            fprintf(stderr, "[EVENT] %s %p has errored: %x\n", component->name, hComponent, (unsigned int) nData1);
            exit(1);
            break;

        case OMX_EventCmdComplete:
            fprintf(stderr, "[EVENT] %s %p has completed the last command (%x).\n", component->name, hComponent, nData1);

            pthread_mutex_lock(&component->cmd_queue_mutex);
            if ((nData1 == component->cmd.Cmd) &&
                    (nData2 == component->cmd.nParam)) {
                memset(&component->cmd, 0, sizeof (component->cmd));
                pthread_cond_signal(&component->cmd_queue_count_cv);
            }
            pthread_mutex_unlock(&component->cmd_queue_mutex);

            break;

        case OMX_EventPortSettingsChanged:
            fprintf(stderr, "[EVENT] %s %p port %d settings changed.\n", component->name, hComponent, (unsigned int) nData1);
            component->port_settings_changed = 1;
            break;

        case OMX_EventBufferFlag:
            if (nData2 & OMX_BUFFERFLAG_EOS) {
                fprintf(stderr, "[EVENT] Got an EOS event on %s %p (port %d, d2 %x)\n", component->name, hComponent, (unsigned int) nData1, (unsigned int) nData2);
                pthread_mutex_lock(&component->eos_mutex);
                component->eos = 1;
                pthread_cond_signal(&component->eos_cv);
                pthread_mutex_unlock(&component->eos_mutex);
            }

            break;

        case OMX_EventMark:
            fprintf(stderr,"[EVENT] OMX_EventMark\n");
            break;

        case OMX_EventParamOrConfigChanged:
            fprintf(stderr, "[EVENT] OMX_EventParamOrConfigChanged on component \"%s\" - d1=%x, d2=%x\n", component->name, (unsigned int) nData1, (unsigned int) nData2);
            component->config_changed = 1;
            break;

        default:
            fprintf(stderr, "[EVENT] Got an event of type %x on %s %p (d1: %x, d2 %x)\n", eEvent,
                    component->name, hComponent, (unsigned int) nData1, (unsigned int) nData2);
    }

    return OMX_ErrorNone;
}

static
OMX_ERRORTYPE
omx_empty_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) 
{

    struct omx_component_t* component = (struct omx_component_t*) pAppData;

    /*here we just signal that there is at
     least 1 non-empty input buffer 
     */
    if (component->buf_in_notempty == 0) {
        pthread_mutex_lock(&component->buf_in_mutex);
        component->buf_in_notempty = 1;
        pthread_cond_signal(&component->buf_in_notempty_cv);
        pthread_mutex_unlock(&component->buf_in_mutex);
    }
    return OMX_ErrorNone;
}

static
OMX_ERRORTYPE
omx_generic_fill_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {

    fprintf(stderr, "[omx_generic_fill_buffer_done]\n");
    return OMX_ErrorNone;
}

static
OMX_ERRORTYPE
omx_encoder_fill_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {

    struct omx_component_t* component = (struct omx_component_t*) pAppData;
    OMX_BUFFERHEADERTYPE *current;
    struct packet_t *encoded_packet;
    /* we  get the buffer with encoded data here 
     * and we have to queue it and then consume it from within another thread 
     */

    encoded_packet = malloc(sizeof (*encoded_packet));

    encoded_packet->data_length = pBuffer->nFilledLen;
    encoded_packet->data = malloc(pBuffer->nFilledLen);
    memcpy(encoded_packet->data, pBuffer->pBuffer, pBuffer->nFilledLen);
    packet_queue_add_item(&component->pipe->encoded_video_queue, encoded_packet);
    
    
    pBuffer->nFilledLen = 0; //prep buffer for return;
    
    pthread_mutex_lock(&component->buf_out_mutex);
    current = component->out_buffers;
    while (current && current->pAppPrivate)
        current = current->pAppPrivate;

    if (!current)
        component->out_buffers = pBuffer;
    else
        current->pAppPrivate = pBuffer;

    pBuffer->pAppPrivate = NULL;
    if (component->buf_out_notempty == 0) {
        component->buf_out_notempty = 1;
        pthread_cond_signal(&component->buf_out_notempty_cv);
    }

    pthread_mutex_unlock(&component->buf_out_mutex);

    return OMX_ErrorNone;
}



/**
 * Disable all ports for the current component
 * @param component
 */
static
void
omx_disable_all_ports(struct omx_component_t* component) 
{
    OMX_PORT_PARAM_TYPE ports;
    OMX_INDEXTYPE types[] = {OMX_IndexParamAudioInit, OMX_IndexParamVideoInit, OMX_IndexParamImageInit, OMX_IndexParamOtherInit};
    int i;

    OMX_INIT_STRUCTURE(ports);

    for (i = 0; i < 4; i++) {
        OMX_ERRORTYPE error = OMX_GetParameter(component->h, types[i], &ports);
        if (error == OMX_ErrorNone) {
            uint32_t j;
            for (j = 0; j < ports.nPorts; j++) {
                omx_send_command_and_wait(component, OMX_CommandPortDisable, ports.nStartPortNumber + j, NULL);
            }
        }
    }
}

OMX_ERRORTYPE 
omx_init_component(struct omx_pipeline_t* pipe, struct omx_component_t* component, char* compname)
{
  memset(component,0,sizeof(component));

  pthread_mutex_init(&component->cmd_queue_mutex, NULL);
  pthread_cond_init(&component->cmd_queue_count_cv,NULL);
  
  pthread_mutex_init(&component->buf_in_mutex, NULL);
  pthread_cond_init(&component->buf_in_notempty_cv,NULL);
  component->buf_in_notempty = 1;
  
  pthread_mutex_init(&component->buf_out_mutex, NULL);
  pthread_cond_init(&component->buf_out_notempty_cv,NULL);
  component->buf_out_notempty = 1;
  
  pthread_mutex_init(&component->eos_mutex,NULL);
  pthread_cond_init(&component->eos_cv,NULL);
  

  component->callbacks.EventHandler = omx_event_handler;
  component->callbacks.EmptyBufferDone = omx_empty_buffer_done;
  if(strcmp(compname, "OMX.broadcom.video_encode") == 0) {
      component->callbacks.FillBufferDone = omx_encoder_fill_buffer_done;
  } else { 
      component->callbacks.FillBufferDone = omx_generic_fill_buffer_done;
  }
  

  component->pipe = pipe;
  
  component->name = compname;

  /* Create OMX component */
  OERR(OMX_GetHandle(&component->h, compname, component, &component->callbacks));

  /* Disable all ports */
  omx_disable_all_ports(component);
  
  printf("[DEBUG] Initialised %s done\n", compname);

}

/* Based on allocbufs from omxtx.
   Buffers are connected as a one-way linked list using pAppPrivate as the pointer to the next element */
void
omx_alloc_buffers(struct omx_component_t *component, int port) 
{
    int i;
    OMX_BUFFERHEADERTYPE *list = NULL, **end = &list;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    OMX_INIT_STRUCTURE(portdef);
    portdef.nPortIndex = port;

    OERR(OMX_GetParameter(component->h, OMX_IndexParamPortDefinition, &portdef));

    for (i = 0; i < portdef.nBufferCountActual; i++) {
        OMX_U8 *buf;

        buf = vcos_malloc_aligned(portdef.nBufferSize, portdef.nBufferAlignment, "buffer");

        OERR(OMX_UseBuffer(component->h, end, port, NULL, portdef.nBufferSize, buf));

        end = (OMX_BUFFERHEADERTYPE **) &((*end)->pAppPrivate);
    }

    if (portdef.eDir == OMX_DirInput) { 
        component->in_buffers = list;
    } else { 
        component->out_buffers = list;
    }
    
    
    printf("[DEBUG] Allocating buffers for %s done\n", component->name);
}

/* Return the next free buffer, or NULL if none are free */
OMX_BUFFERHEADERTYPE *
omx_get_next_input_buffer(struct omx_component_t* component) {
    OMX_BUFFERHEADERTYPE *ret;

retry:
    pthread_mutex_lock(&component->buf_in_mutex);

    ret = component->in_buffers;
    while (ret && ret->nFilledLen > 0)
        ret = ret->pAppPrivate;

    if (!ret)
        component->buf_in_notempty = 0;

    if (ret) {
        pthread_mutex_unlock(&component->buf_in_mutex);
        return ret;
    }

    while (component->buf_in_notempty == 0)
        pthread_cond_wait(&component->buf_in_notempty_cv, &component->buf_in_mutex);

    pthread_mutex_unlock(&component->buf_in_mutex);

    goto retry;

    /* We never get here, but keep GCC happy */
    return NULL;
}

/* Return the next full buffer*/
OMX_BUFFERHEADERTYPE *
omx_get_next_output_buffer(struct omx_component_t* component) {

    OMX_BUFFERHEADERTYPE *ret = NULL, *prev = NULL;

    pthread_mutex_lock(&component->buf_out_mutex);
    while (component->buf_out_notempty == 0) {
        pthread_cond_wait(&component->buf_out_notempty_cv, &component->buf_out_mutex);
    }

    do {
        ret = component->out_buffers;
        //go to the end of the list
        while (ret && ret->pAppPrivate != NULL) {
            prev = ret;
            ret = ret->pAppPrivate;
        }

        if (ret) {
            //if there is only 1 element in the list
            if (prev == NULL)
                component->out_buffers = ret->pAppPrivate;
            else
                prev->pAppPrivate = ret->pAppPrivate;

            ret->pAppPrivate = NULL;
        } else { 
            component->buf_out_notempty = 0;
        }

        pthread_mutex_unlock(&component->buf_out_mutex);

    } while (!ret);

    return ret;
}

void
omx_free_buffers(struct omx_component_t *component, int port) {
    OMX_BUFFERHEADERTYPE *buf, *prev;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;

    OMX_INIT_STRUCTURE(portdef);
    portdef.nPortIndex = port;

    OERR(OMX_GetParameter(component->h, OMX_IndexParamPortDefinition, &portdef));

    if (portdef.eDir == OMX_DirInput) {
        buf = component->in_buffers;
    } else {
        buf = component->out_buffers;
    }

    while (buf) {
        prev = buf->pAppPrivate;
        OERR(OMX_FreeBuffer(component->h, port, buf)); /* This also calls free() */
        buf = prev;
    }
}

int 
omx_get_free_buffer_count(struct omx_component_t* component)
{
  int n = 0;
  OMX_BUFFERHEADERTYPE *buf = component->in_buffers;

  pthread_mutex_lock(&component->buf_in_mutex);
  while (buf) {
    if (buf->nFilledLen == 0) n++;
    buf = buf->pAppPrivate;
  }
  pthread_mutex_unlock(&component->buf_in_mutex);

  return n;
}

static
OMX_ERRORTYPE 
omx_flush_tunnel(struct omx_component_t* source, int source_port, struct omx_component_t* sink, int sink_port)
{
  omx_send_command_and_wait(source,OMX_CommandFlush,source_port,NULL);
  omx_send_command_and_wait(sink,OMX_CommandFlush,sink_port,NULL);
}


OMX_ERRORTYPE 
omx_setup_pipeline(struct omx_pipeline_t* pipe, OMX_VIDEO_CODINGTYPE video_codec)
{
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;

  OMX_CONFIG_BOOLEANTYPE configBoolTrue;
  OMX_INIT_STRUCTURE(configBoolTrue);
  configBoolTrue.bEnabled = OMX_TRUE;

  omx_init_component(pipe, &pipe->video_decode, "OMX.broadcom.video_decode");
  omx_init_component(pipe, &pipe->video_render, "OMX.broadcom.video_render");
  omx_init_component(pipe, &pipe->clock, "OMX.broadcom.clock");

  OMX_INIT_STRUCTURE(cstate);
  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = OMX_CLOCKPORT0;
  OERR(OMX_SetParameter(pipe->clock.h, OMX_IndexConfigTimeClockState, &cstate));

  omx_init_component(pipe, &pipe->video_scheduler, "OMX.broadcom.video_scheduler");

  /* Setup clock tunnels first */
  // source component must at least be idle, not loaded
  omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateIdle, NULL);

  OERR(OMX_SetupTunnel(pipe->clock.h, 80, pipe->video_scheduler.h, 12));

  OERR(OMX_SendCommand(pipe->clock.h, OMX_CommandPortEnable, 80, NULL));
  OERR(OMX_SendCommand(pipe->video_scheduler.h, OMX_CommandPortEnable, 12, NULL));

  omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateIdle, NULL);

  omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateExecuting, NULL);

  /* Configure video_decoder */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateIdle, NULL);

  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130;
  format.eCompressionFormat = video_codec;

  OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamVideoPortFormat, &format));

   /* Enable error concealment for H264 only - without this, HD channels don't work reliably */
 /* if (video_codec == OMX_VIDEO_CodingAVC) {
     OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ec;
     OMX_INIT_STRUCTURE(ec);
     ec.bStartWithValidFrame = OMX_FALSE;
     OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ec));
  }
*/
  /* Enable video decoder input port */
  omx_send_command_and_wait0(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Allocate input buffers */
  omx_alloc_buffers(&pipe->video_decode, 130);
  
  /* Wait for input port to be enabled */
  omx_send_command_and_wait1(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Change video_decode to OMX_StateExecuting */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateExecuting, NULL);

  /* Enable passing of buffer marks */
  //OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamPassBufferMarks, &configBoolTrue));
  //OERR(OMX_SetParameter(pipe->video_render.h, OMX_IndexParamPassBufferMarks, &configBoolTrue));

  printf("[DEBUG] pipeline init done\n");
  return OMX_ErrorNone;
}


void 
omx_teardown_pipeline(struct omx_pipeline_t* pipe)
{
   OMX_BUFFERHEADERTYPE *buf;
   int i=1;

   printf("[DEBUG] beginning pipeline teardown\n");
   /* NOTE: Three events are sent after the previous command:

[EVENT] Got an event of type 4 on video_decode 0x426a10 (d1: 83, d2 1)
[EVENT] Got an event of type 4 on video_scheduler 0x430d10 (d1: b, d2 1)
[EVENT] Got an event of type 4 on video_render 0x430b30 (d1: 5a, d2 1) 5a = port (90) 1 = OMX_BUFFERFLAG_EOS
*/

   fprintf(stderr,"[vcodec] omx_teardown pipeline 2b\n");
   /* Flush entrance to pipeline */
   omx_send_command_and_wait(&pipe->video_decode,OMX_CommandFlush,130,NULL);

   /* Flush all tunnels */
   fprintf(stderr,"[vcodec] omx_teardown pipeline 3\n");
   omx_flush_tunnel(&pipe->video_decode, 131, &pipe->video_scheduler, 10);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 4\n");
   omx_flush_tunnel(&pipe->video_scheduler, 11, &pipe->video_render, 90);
   omx_flush_tunnel(&pipe->clock, 80, &pipe->video_scheduler, 12);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 5\n");

   /* Scheduler -> render tunnel */
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 11, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandPortDisable, 90, NULL);

   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 10, NULL);

   fprintf(stderr,"[vcodec] omx_teardown pipeline 11\n");

   /* Disable video_decode input port and buffers */
   //dumpport(pipe->video_decode.h,130);
   omx_send_command_and_wait0(&pipe->video_decode, OMX_CommandPortDisable, 130, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 6\n");
   omx_free_buffers(&pipe->video_decode, 130);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 7\n");
   //dumpport(pipe->video_decode.h,130);
   omx_send_command_and_wait1(&pipe->video_decode, OMX_CommandPortDisable, 130, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 8\n");

   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandPortDisable, 131, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 10\n");


   /* NOTE: The clock disable doesn't complete until after the video scheduler port is
disabled (but it completes before the video scheduler port disabling completes). */
   OERR(OMX_SendCommand(pipe->clock.h, OMX_CommandPortDisable, 80, NULL));
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 12, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 12\n");

   /* Teardown tunnels */
   OERR(OMX_SetupTunnel(pipe->video_decode.h, 131, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 10, NULL, 0));
   fprintf(stderr,"[vcodec] omx_teardown pipeline 13\n");

   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 11, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_render.h, 90, NULL, 0));

   OERR(OMX_SetupTunnel(pipe->clock.h, 80, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 12, NULL, 0));

   

   fprintf(stderr,"[vcodec] omx_teardown pipeline 14\n");
   /* Transition all components to Idle */
   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateIdle, NULL);

   fprintf(stderr,"[vcodec] omx_teardown pipeline 15\n");

   /* Transition all components to Loaded */
   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateLoaded, NULL);


   fprintf(stderr,"[vcodec] omx_teardown pipeline 16\n");
   /* Finally free the component handles */
   OERR(OMX_FreeHandle(pipe->video_decode.h));
   OERR(OMX_FreeHandle(pipe->video_scheduler.h));
   OERR(OMX_FreeHandle(pipe->video_render.h));
   OERR(OMX_FreeHandle(pipe->clock.h));
   fprintf(stderr,"[vcodec] omx_teardown pipeline 17\n");

}


OMX_ERRORTYPE 
omx_setup_encoding_pipeline(struct omx_pipeline_t* pipe, OMX_VIDEO_CODINGTYPE video_codec)
{
  OMX_VIDEO_PARAM_PORTFORMATTYPE format;

  packet_queue_init(&pipe->encoded_video_queue);
  
  omx_init_component(pipe, &pipe->video_decode, "OMX.broadcom.video_decode");
  omx_init_component(pipe, &pipe->resize, "OMX.broadcom.resize");
  omx_init_component(pipe, &pipe->video_encode, "OMX.broadcom.video_encode");
  
  /* Configure video_decoder */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateIdle, NULL);

  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130;
  format.eCompressionFormat = video_codec;

  OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamVideoPortFormat, &format));

   /* Enable error concealment for H264 only - without this, HD channels don't work reliably */
 /* if (video_codec == OMX_VIDEO_CodingAVC) {
     OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ec;
     OMX_INIT_STRUCTURE(ec);
     ec.bStartWithValidFrame = OMX_FALSE;
     OERR(OMX_SetParameter(pipe->video_decode.h, OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ec));
  }
*/
  /* Enable video decoder input port */
  omx_send_command_and_wait0(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Allocate input buffers */
  omx_alloc_buffers(&pipe->video_decode, 130);
  
  /* Wait for input port to be enabled */
  omx_send_command_and_wait1(&pipe->video_decode, OMX_CommandPortEnable, 130, NULL);

  /* Change video_decode to OMX_StateExecuting */
  omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateExecuting, NULL);

  /* the rest of the config will be done  in the main event loop
   when the decoder has received port chagned event*/

  
  printf("[DEBUG] pipeline init done\n");
  return OMX_ErrorNone;
}

void 
omx_teardown_encoding_pipeline(struct omx_pipeline_t* pipe)
{
   OMX_BUFFERHEADERTYPE *buf;
   int i=1;

   printf("[DEBUG] beginning pipeline teardown\n");
   /* NOTE: Three events are sent after the previous command:

[EVENT] Got an event of type 4 on video_decode 0x426a10 (d1: 83, d2 1)
[EVENT] Got an event of type 4 on video_scheduler 0x430d10 (d1: b, d2 1)
[EVENT] Got an event of type 4 on video_render 0x430b30 (d1: 5a, d2 1) 5a = port (90) 1 = OMX_BUFFERFLAG_EOS
*/

   fprintf(stderr,"[vcodec] omx_teardown pipeline 2b\n");
   /* Flush entrance to pipeline */
   omx_send_command_and_wait(&pipe->video_decode,OMX_CommandFlush,130,NULL);

   /* Flush all tunnels */
   fprintf(stderr,"[vcodec] omx_teardown pipeline 3\n");
   omx_flush_tunnel(&pipe->video_decode, 131, &pipe->video_scheduler, 10);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 4\n");
   omx_flush_tunnel(&pipe->video_scheduler, 11, &pipe->video_render, 90);
   omx_flush_tunnel(&pipe->clock, 80, &pipe->video_scheduler, 12);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 5\n");

   /* Scheduler -> render tunnel */
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 11, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandPortDisable, 90, NULL);

   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 10, NULL);

   fprintf(stderr,"[vcodec] omx_teardown pipeline 11\n");

   /* Disable video_decode input port and buffers */
   //dumpport(pipe->video_decode.h,130);
   omx_send_command_and_wait0(&pipe->video_decode, OMX_CommandPortDisable, 130, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 6\n");
   omx_free_buffers(&pipe->video_decode, 130);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 7\n");
   //dumpport(pipe->video_decode.h,130);
   omx_send_command_and_wait1(&pipe->video_decode, OMX_CommandPortDisable, 130, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 8\n");

   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandPortDisable, 131, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 10\n");


   /* NOTE: The clock disable doesn't complete until after the video scheduler port is
disabled (but it completes before the video scheduler port disabling completes). */
   OERR(OMX_SendCommand(pipe->clock.h, OMX_CommandPortDisable, 80, NULL));
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandPortDisable, 12, NULL);
   fprintf(stderr,"[vcodec] omx_teardown pipeline 12\n");

   /* Teardown tunnels */
   OERR(OMX_SetupTunnel(pipe->video_decode.h, 131, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 10, NULL, 0));
   fprintf(stderr,"[vcodec] omx_teardown pipeline 13\n");

   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 11, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_render.h, 90, NULL, 0));

   OERR(OMX_SetupTunnel(pipe->clock.h, 80, NULL, 0));
   OERR(OMX_SetupTunnel(pipe->video_scheduler.h, 12, NULL, 0));

   

   fprintf(stderr,"[vcodec] omx_teardown pipeline 14\n");
   /* Transition all components to Idle */
   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandStateSet, OMX_StateIdle, NULL);
   omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateIdle, NULL);

   fprintf(stderr,"[vcodec] omx_teardown pipeline 15\n");

   /* Transition all components to Loaded */
   omx_send_command_and_wait(&pipe->video_decode, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->video_scheduler, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->video_render, OMX_CommandStateSet, OMX_StateLoaded, NULL);
   omx_send_command_and_wait(&pipe->clock, OMX_CommandStateSet, OMX_StateLoaded, NULL);


   fprintf(stderr,"[vcodec] omx_teardown pipeline 16\n");
   /* Finally free the component handles */
   OERR(OMX_FreeHandle(pipe->video_decode.h));
   OERR(OMX_FreeHandle(pipe->video_scheduler.h));
   OERR(OMX_FreeHandle(pipe->video_render.h));
   OERR(OMX_FreeHandle(pipe->clock.h));
   fprintf(stderr,"[vcodec] omx_teardown pipeline 17\n");

}