#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

#include "util.h"
#include "error.h"
#include "receiver_capi.h"

#include "client_intern.h"
#include "port.h"
#include "procbuf.h"
#include "main.h"

typedef LjackPortUserData      PortUserData;
typedef LjackClientUserData    ClientUserData;
typedef LjackProcBufUserData   ProcBufUserData;


/* ============================================================================================ */

static void addBooleanToWriter(ClientUserData* udata, int b)
{
    udata->receiver_capi->addBooleanToWriter(udata->receiver_writer, b);
}

static void addIntegerToWriter(ClientUserData* udata, lua_Integer i)
{
    udata->receiver_capi->addIntegerToWriter(udata->receiver_writer, i);
}

static void addStringToWriter(ClientUserData* udata, const char* str)
{
    udata->receiver_capi->addStringToWriter(udata->receiver_writer, str, strlen(str));
}

static void handleReceiverError(void* ehdata, const char* msg, size_t msglen)
{
    ljack_handle_error((error_handler_data*)ehdata, msg, msglen);
}

static void addMsgToReceiver(ClientUserData* udata)
{
    error_handler_data ehdata = {0};
    udata->receiver_capi->msgToReceiver(udata->receiver, udata->receiver_writer, 
                                        false, false, handleReceiverError, &ehdata);
    if (ehdata.buffer) {
        ljack_log_error("LJACK: Error while calling client callback.");
        ljack_log_error(ehdata.buffer);
        free(ehdata.buffer);
    }
}

/* ============================================================================================ */

static int jackGraphOrderCallback(void* arg)
{
    ClientUserData* udata = arg;
    if (udata->receiver) {
        addStringToWriter(udata, "GraphOrder");
        addMsgToReceiver (udata);
    }
    return 0;
}

static void jackClientRegistrationCallback(const char* name, int registered, void* arg)
{
    ClientUserData* udata = arg;
    if (udata->receiver) {
        addStringToWriter (udata, "ClientRegistration");
        addStringToWriter (udata, name);
        addBooleanToWriter(udata, registered);
        addMsgToReceiver  (udata);
    }
}

static void jackPortConnectCallback(jack_port_id_t a, jack_port_id_t b, int connected, void* arg)
{
    ClientUserData* udata = arg;
    if (udata->receiver) {
        addStringToWriter (udata, "PortConnect");
        addIntegerToWriter(udata, (lua_Integer)a);
        addIntegerToWriter(udata, (lua_Integer)b);
        addBooleanToWriter(udata, connected);
        addMsgToReceiver  (udata);
    }
}

static void jackPortRegistrationCallback(jack_port_id_t port, int registered, void* arg)
{
    ClientUserData* udata = arg;
    if (udata->receiver) {
        addStringToWriter (udata, "PortRegistration");
        addIntegerToWriter(udata, (lua_Integer)port);
        addBooleanToWriter(udata, registered);
        addMsgToReceiver  (udata);
    }
}

static void jackPortRenameCallback(jack_port_id_t port, const char* old_name, const char* new_name, void* arg)
{
    ClientUserData* udata = arg;

    if (udata->receiver) {
        addStringToWriter (udata, "PortRename");
        addIntegerToWriter(udata, (lua_Integer)port);
        addStringToWriter (udata, old_name);
        addStringToWriter (udata, new_name);
        addMsgToReceiver  (udata);
    }
}

static int jackXRunCallback(void* arg)
{
    ClientUserData* udata = arg;

    if (udata->receiver) {
        addStringToWriter (udata, "XRun");
        addMsgToReceiver  (udata);
    }
    return 0;
}

static void jackInfoShutdownCallback(jack_status_t code, const char* reason, void* arg)
{
    ClientUserData* udata = arg;

    if (udata->receiver) {
        addStringToWriter (udata, "Shutdown");
        addStringToWriter (udata, reason);
        addMsgToReceiver  (udata);
    }
    
    async_mutex_lock  (&udata->processMutex);
    udata->shutdownReceived = true;
    async_mutex_notify(&udata->processMutex);
    async_mutex_unlock(&udata->processMutex);
}


static void adjustProcessorBufferSizes_LOCKED(ClientUserData* udata, LjackProcReg** procRegList, jack_nframes_t nframes)
{
    if (procRegList) {
        int i = 0;
        while (true) 
        {
            LjackProcReg* reg = procRegList[i];
            if (!reg) {
                break;
            }
            if (reg->bufferSize != nframes) {
                if (reg->bufferSizeCallback) {
                    int rc = reg->bufferSizeCallback(nframes, reg->processorData);
                    if (rc != 0) {
                        if (!udata->severeProcessingError) {
                            ljack_log_error("LJACK: client invalidated because buffer size callback for processor '%s' gives error %d.", reg->processorName, rc);
                            udata->severeProcessingError = true;
                            udata->shutdownReceived = true;
                            async_mutex_notify(&udata->processMutex);
                            if (udata->receiver) {
                                addStringToWriter (udata, "ProcessingError");
                                addStringToWriter (udata, "client invalidated because buffer size callback gives error");
                                addStringToWriter (udata, reg->processorName);
                                addIntegerToWriter(udata, rc);
                                addMsgToReceiver  (udata);
                            }
                        }
                    }
                }
                reg->bufferSize = nframes;
            }
            ++i;
        }
    }
}


static int jackBufferSizeCallback(jack_nframes_t nframes, void* arg)
{
    ClientUserData* udata = arg;

    async_mutex_lock(&udata->processMutex);
    {
        if (udata->confirmedProcRegList != udata->activeProcRegList) {
            udata->confirmedProcRegList = udata->activeProcRegList;
            async_mutex_notify(&udata->processMutex);
        }

        if (udata->bufferSize != nframes) {
            udata->bufferSize = nframes;
            udata->audioBufferSize = jack_port_type_get_buffer_size(udata->client, JACK_DEFAULT_AUDIO_TYPE);
            udata->midiBufferSize  = jack_port_type_get_buffer_size(udata->client, JACK_DEFAULT_MIDI_TYPE);
            ProcBufUserData*  procBufUdata = udata->firstProcBufUserData;
            while (procBufUdata) {
                if (procBufUdata->ringBuffer) {
                    jack_ringbuffer_free(procBufUdata->ringBuffer);
                    size_t size = procBufUdata->isAudio ? udata->audioBufferSize 
                                                        : udata->midiBufferSize;
                    procBufUdata->ringBuffer = jack_ringbuffer_create(size);
                    ljack_procbuf_clear_midi_events(procBufUdata);
                    if (procBufUdata->ringBuffer) {
                        jack_ringbuffer_mlock(procBufUdata->ringBuffer);
                    } else {
                        if (!udata->severeProcessingError) {
                            ljack_log_error("LJACK: client invalidated because buffer allocation failed for process buffer '%s'.", procBufUdata->procBufName);
                            udata->severeProcessingError = true;
                            udata->shutdownReceived = true;
                            if (udata->receiver) {
                                addStringToWriter (udata, "ProcessingError");
                                addStringToWriter (udata, "client invalidated because buffer allocation failed for process buffer");
                                addStringToWriter (udata, procBufUdata->procBufName);
                                addMsgToReceiver  (udata);
                            }
                        }
                    }
                }
                procBufUdata = procBufUdata->nextProcBufUserData;
            }
            adjustProcessorBufferSizes_LOCKED(udata, udata->procRegList, nframes);
        }
    }
    async_mutex_unlock(&udata->processMutex);

    if (udata->receiver) {
        addStringToWriter (udata, "BufferSize");
        addIntegerToWriter(udata, (lua_Integer)nframes);
        addMsgToReceiver  (udata);
    }
    return 0;
}

/* ============================================================================================ */

typedef int ProcessCallback(jack_nframes_t nframes, void* processorData);

static int jackProcessCallback(jack_nframes_t nframes, void* arg)
{
    ClientUserData* udata = arg;
    
    LjackProcReg** list = udata->activeProcRegList;

    if (udata->confirmedProcRegList != list)
    {
        if (async_mutex_trylock(&udata->processMutex)) {
            udata->confirmedProcRegList = list;
            async_mutex_notify(&udata->processMutex);
            async_mutex_unlock(&udata->processMutex);
        }
    }
    if (!udata->shutdownReceived)
    {
        if (list) {
            int i = 0;
            while (true) 
            {
                LjackProcReg* reg = list[i];
                if (!reg) {
                    break;
                }
                ProcessCallback* processCallback = reg->processCallback;
                if (reg->activated) {
                    reg->outBuffersCleared = false;
                    int rc = processCallback(nframes, reg->processorData);
                    if (rc != 0) {
                        async_mutex_lock(&udata->processMutex);
                        {
                            ljack_log_error("LJACK: client invalidated because processor '%s' returned processing error %d.", reg->processorName, rc);
                            udata->severeProcessingError = true;
                            udata->shutdownReceived = true;
                            async_mutex_notify(&udata->processMutex);

                            if (udata->receiver) {
                                addStringToWriter (udata, "ProcessingError");
                                addStringToWriter (udata, "client invalidated because processor returned processing error");
                                addStringToWriter (udata, reg->processorName);
                                addIntegerToWriter(udata, rc);
                                addMsgToReceiver  (udata);
                            }
                        }
                        async_mutex_unlock(&udata->processMutex);
                        return rc;
                    }
                } else if (!reg->outBuffersCleared) {
                    for (int i = 0, n = reg->connectorCount; i < n; ++i) {
                        LjackConnectorInfo* info = reg->connectorInfos + i;
                        if (info->isOutput) {
                            if (info->isPort) {
                                if (info->portUdata->isAudio) {
                                    jack_default_audio_sample_t* b = (jack_default_audio_sample_t*)jack_port_get_buffer(info->portUdata->port, nframes);
                                    memset(b, 0, nframes * sizeof(jack_default_audio_sample_t));
                                } else if (info->portUdata->isMidi) {
                                    jack_midi_clear_buffer(jack_port_get_buffer(info->portUdata->port, nframes));
                                }
                            } else if (info->isProcBuf) {
                                if (info->procBufUdata->isAudio) {
                                    jack_default_audio_sample_t* b = (jack_default_audio_sample_t*)info->procBufUdata->ringBuffer->buf;
                                    memset(b, 0, nframes * sizeof(jack_default_audio_sample_t));
                                }
                                else if (info->procBufUdata->isMidi) {
                                    ljack_procbuf_clear_midi_events(info->procBufUdata);
                                }
                            }
                        }
                    }
                    reg->outBuffersCleared = true;
                }
                ++i;
            }
        }
    }
    return 0;
}

/* ============================================================================================ */

void ljack_client_intern_activate_proc_list_LOCKED(ClientUserData* udata, 
                                                   LjackProcReg**  newList)
{
    udata->activeProcRegList = newList;
    
    if (udata->activated) {
        while (   atomic_get(&udata->shutdownReceived) == 0
               && udata->confirmedProcRegList != newList) 
        {
            async_mutex_wait(&udata->processMutex);
        }
    }
    udata->confirmedProcRegList = newList;
}

/* ============================================================================================ */

void ljack_client_intern_get_connector(lua_State* L, int arg, 
                                       PortUserData** portUdata, 
                                       ProcBufUserData** procBufUdata)
{
    void* udata = lua_touserdata(L, arg);
    if (udata) {
        size_t len = lua_rawlen(L, arg);
        if (   len == sizeof(PortUserData) 
            && ((PortUserData*)udata)->className == LJACK_PORT_CLASS_NAME) 
        {
            *portUdata = udata;
        }
        else if (   len == sizeof(ProcBufUserData) 
                 && ((ProcBufUserData*)udata)->className == LJACK_PROCBUF_CLASS_NAME) 
        {
            *procBufUdata = udata;
        }
    }
}

/* ============================================================================================ */

void ljack_client_intern_release_proc_reg(lua_State* L, LjackProcReg* reg)
{
    reg->processorData        = NULL;
    reg->processCallback      = NULL;
    reg->engineClosedCallback = NULL;
    
    bool wasActivated = reg->activated;
    if (wasActivated) {
        reg->activated = false;
    }
    if (reg->connectorTableRef != LUA_REFNIL) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg->connectorTableRef);  /* -> connectorTable */
        for (int i = 1; i <= reg->connectorCount; ++i) {
            lua_rawgeti(L, -1, i);                             /* -> connectorTable, connector */
            PortUserData*    portUdata    = NULL;
            ProcBufUserData* procBufUdata = NULL;
            ljack_client_intern_get_connector(L, -1, &portUdata, &procBufUdata);
            if (portUdata) {
                portUdata->procUsageCounter -= 1;
            }
            else if (procBufUdata) {
                procBufUdata->procUsageCounter -= 1;
                if (reg->connectorInfos) {
                    if (reg->connectorInfos[i-1].isInput) {
                        procBufUdata->inpUsageCounter -= 1;
                        if (wasActivated) {
                            procBufUdata->inpActiveCounter -= 1;
                        }
                    } else {
                        procBufUdata->outUsageCounter -= 1;
                        if (wasActivated) {
                            procBufUdata->outActiveCounter -= 1;
                        }
                    }
                }
            }
            lua_pop(L, 1);                                     /* -> connectorTable */
        }
        lua_pop(L, 1);                                         /* -> */
        luaL_unref(L, LUA_REGISTRYINDEX, reg->connectorTableRef);
        reg->connectorTableRef = LUA_REFNIL;
        reg->connectorCount = 0;
    }
    if (reg->connectorInfos) {
        free(reg->connectorInfos);
        reg->connectorInfos = NULL;
    }
    if (reg->processorName) {
        free(reg->processorName);
        reg->processorName = NULL;
    }
    free(reg);
}


/* ============================================================================================ */

void ljack_client_intern_register_callbacks(ClientUserData* udata)
{
    jack_set_graph_order_callback         (udata->client, jackGraphOrderCallback,             udata);
    jack_set_client_registration_callback (udata->client, jackClientRegistrationCallback,     udata);
    jack_set_port_connect_callback        (udata->client, jackPortConnectCallback,            udata);
    jack_set_port_registration_callback   (udata->client, jackPortRegistrationCallback,       udata);
    jack_set_port_rename_callback         (udata->client, jackPortRenameCallback,             udata);
    jack_set_buffer_size_callback         (udata->client, jackBufferSizeCallback,             udata);
    jack_set_xrun_callback                (udata->client, jackXRunCallback,                   udata);
    jack_set_process_callback             (udata->client, jackProcessCallback,                udata);
    
    jack_on_info_shutdown                 (udata->client, jackInfoShutdownCallback,           udata);
}

/* ============================================================================================ */
