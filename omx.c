/**
 * holds some util stuff for dealing with omx
 * 
 */


#include "omx.h"


OMX_ERRORTYPE 
omx_init_component(struct omx_component_t* component, char* compname)
{
  memset(component,0,sizeof(component));

  pthread_mutex_init(&component->cmd_queue_mutex, NULL);
  pthread_cond_init(&component->cmd_queue_count_cv,NULL);
  component->buf_notempty = 1;
  pthread_cond_init(&component->buf_notempty_cv,NULL);
  pthread_cond_init(&component->eos_cv,NULL);
  pthread_mutex_init(&component->eos_mutex,NULL);

  component->callbacks.EventHandler = omx_event_handler;
  component->callbacks.EmptyBufferDone = omx_empty_buffer_done;
  component->callbacks.FillBufferDone = omx_fill_buffer_done;

  component->name = compname;

  /* Create OMX component */
  OERR(OMX_GetHandle(&component->h, compname, component, &component->callbacks));

  /* Disable all ports */
  omx_disable_all_ports(component);

}

/* The event handler is called from the OMX component thread */
static 
OMX_ERRORTYPE 
omx_event_handler(OMX_IN OMX_HANDLETYPE hComponent,
        OMX_IN OMX_PTR pAppData,
        OMX_IN OMX_EVENTTYPE eEvent,
        OMX_IN OMX_U32 nData1,
        OMX_IN OMX_U32 nData2,
        OMX_IN OMX_PTR pEventData) {
    
    struct omx_component_t* component = (struct omx_component_t*) pAppData;

    switch (eEvent) {
        case OMX_EventError:
            fprintf(stderr, "[EVENT] %s %p has errored: %x\n", component->name, hComponent, (unsigned int) nData1);
            exit(1);
            break;

        case OMX_EventCmdComplete:
            fprintf(stderr, "[EVENT] %s %p has completed the last command (%x).\n", component->name, hComponent, nData1);

            //fprintf(stderr,"[EVENT] Waiting for lock\n");
            pthread_mutex_lock(&component->cmd_queue_mutex);
            //fprintf(stderr,"[EVENT] Got lock\n");
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
        OMX_IN OMX_BUFFERHEADERTYPE* pBuffer) {

    struct omx_component_t* component = (struct omx_component_t*) pAppData;

    if (component->buf_notempty == 0) {
        pthread_mutex_lock(&component->buf_mutex);
        component->buf_notempty = 1;
        pthread_cond_signal(&component->buf_notempty_cv);
        pthread_mutex_unlock(&component->buf_mutex);
    }
    return OMX_ErrorNone;
}


static 
OMX_ERRORTYPE 
omx_fill_buffer_done(OMX_IN OMX_HANDLETYPE hComponent,
                                          OMX_IN OMX_PTR pAppData,
                                          OMX_IN OMX_BUFFERHEADERTYPE* pBuffer)
{
  fprintf(stderr,"[omx_fill_buffer_done]\n");
  return OMX_ErrorNone;
}
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
omx_send_command_and_wait0(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData) {
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
omx_send_command_and_wait1(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData) {
    pthread_mutex_lock(&component->cmd_queue_mutex);
    while (component->cmd.hComponent) {
        /* pthread_cond_wait releases the mutex (which must be locked) and blocks on the condition variable */
        pthread_cond_wait(&component->cmd_queue_count_cv, &component->cmd_queue_mutex);
    }
    pthread_mutex_unlock(&component->cmd_queue_mutex);

}

OMX_ERRORTYPE omx_send_command_and_wait(struct omx_component_t* component, OMX_COMMANDTYPE Cmd, OMX_U32 nParam, OMX_PTR pCmdData)
{
  omx_send_command_and_wait0(component,Cmd,nParam,pCmdData);
  omx_send_command_and_wait1(component,Cmd,nParam,pCmdData);
}


static
void
omx_disable_all_ports(struct omx_component_t* component) {
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