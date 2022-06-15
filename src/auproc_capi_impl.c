#include <jack/jack.h>
#include <jack/midiport.h>

#include "util.h"
#include "auproc_capi_impl.h"

#include "receiver_capi.h"

#include "port.h"
#include "procbuf.h"
#include "client_intern.h"

#include "main.h"

#define LJACK_SASSERT(condition) ((void)sizeof(char[1 - 2*!(condition)]));

/* ============================================================================================ */

typedef LjackPortUserData      PortUserData;
typedef LjackClientUserData    ClientUserData;
typedef LjackConnectorInfo     ConnectorInfo;
typedef LjackPortUserData      PortUserData;
typedef LjackProcBufUserData   ProcBufUserData;

/* ============================================================================================ */

typedef enum ljack_obj_type  ljack_obj_type;

enum ljack_obj_type
{
    LJACK_TNONE    = 0,
    LJACK_TCLIENT  = 1,
    LJACK_TPORT    = 2,
    LJACK_TPROCBUF = 4
};

/* ============================================================================================ */

static void staticAsserts()
{
    LJACK_SASSERT(sizeof(jack_default_audio_sample_t) == sizeof(float));
    LJACK_SASSERT(sizeof(jack_midi_data_t)            == sizeof(unsigned char));
    LJACK_SASSERT(sizeof(jack_nframes_t)              == sizeof(uint32_t));
    LJACK_SASSERT(sizeof(jack_midi_event_t)           == sizeof(auproc_midi_event));
    LJACK_SASSERT(sizeof(jack_time_t)                 == sizeof(uint64_t));
    LJACK_SASSERT(sizeof(lua_Integer)                 <= sizeof(uint64_t));
}

/* ============================================================================================ */

static jack_default_audio_sample_t* port_getAudioBuffer(auproc_connector* connector, jack_nframes_t nframes)
{
    PortUserData* udata = (PortUserData*) connector;
    return (jack_default_audio_sample_t*) jack_port_get_buffer(udata->port, nframes);
}

static jack_default_audio_sample_t* procbuf_getAudioBuffer(auproc_connector* connector, jack_nframes_t nframes)
{
    ProcBufUserData* udata = (ProcBufUserData*) connector;
    return (jack_default_audio_sample_t*) udata->ringBuffer->buf;
}

/* ============================================================================================ */

static auproc_midibuf* port_getMidiBuffer(auproc_connector* connector, jack_nframes_t nframes)
{
    PortUserData* udata = (PortUserData*) connector;
    return (auproc_midibuf*) jack_port_get_buffer(udata->port, nframes);
}

static auproc_midibuf* procbuf_getMidiBuffer(auproc_connector* connector, jack_nframes_t nframes)
{
    ProcBufUserData* udata = (ProcBufUserData*) connector;
    return (auproc_midibuf*) udata;
}

/* ============================================================================================ */

typedef float* (*audiometh_getAudioBuffer)(auproc_connector* connector, uint32_t nframes);

typedef auproc_midibuf* (*midimeth_getMidiBuffer)(auproc_connector* connector, uint32_t nframes);

typedef void (*midimeth_clearBuffer)(auproc_midibuf* midibuf);
    
typedef uint32_t (*midimeth_getEventCount)(auproc_midibuf* midibuf);
    
typedef int (*midimeth_getMidiEvent)(auproc_midi_event* event,
                                     auproc_midibuf*    midibuf,
                                     uint32_t               event_index);
    
typedef int (*midimeth_writeMidiEvent)(auproc_midibuf*     midibuf,
                                       jack_nframes_t          time,
                                       const jack_midi_data_t* data,
                                       size_t                  data_size);

typedef jack_midi_data_t* (*midimeth_reserveMidiEvent)(auproc_midibuf*     midibuf,
                                                       jack_nframes_t          time,
                                                       size_t                  data_size);

/* ============================================================================================ */

static const auproc_audiometh portAudioMethods =
{
    (audiometh_getAudioBuffer)   port_getAudioBuffer
};

/* ============================================================================================ */

static const auproc_audiometh procBufAudioMethods =
{
    (audiometh_getAudioBuffer)   procbuf_getAudioBuffer
};

/* ============================================================================================ */
static const auproc_midimeth portMidiMethods =
{
    (midimeth_getMidiBuffer)     port_getMidiBuffer,
    (midimeth_clearBuffer)       jack_midi_clear_buffer,
    (midimeth_getEventCount)     jack_midi_get_event_count,
    (midimeth_getMidiEvent)      jack_midi_event_get,
    (midimeth_reserveMidiEvent)  jack_midi_event_reserve
};

/* ============================================================================================ */

static const auproc_midimeth procBufMidiMethods =
{
    (midimeth_getMidiBuffer)     procbuf_getMidiBuffer,
    (midimeth_clearBuffer)       ljack_procbuf_clear_midi_events,
    (midimeth_getEventCount)     ljack_procbuf_get_midi_event_count,
    (midimeth_getMidiEvent)      ljack_procbuf_get_midi_event,
    (midimeth_reserveMidiEvent)  ljack_procbuf_reserve_midi_event
};

/* ============================================================================================ */

static ljack_obj_type internGetObjectType(void* udata, size_t len)
{
    if (udata) {
        if (   len == sizeof(ClientUserData) 
            && ((ClientUserData*)udata)->className == LJACK_CLIENT_CLASS_NAME) 
        {
            return LJACK_TCLIENT;
        }
        if (   len == sizeof(PortUserData) 
            && ((PortUserData*)udata)->className == LJACK_PORT_CLASS_NAME) 
        {
            return LJACK_TPORT;
        }
        if (   len == sizeof(ProcBufUserData) 
            && ((ProcBufUserData*)udata)->className == LJACK_PROCBUF_CLASS_NAME) 
        {
            return LJACK_TPROCBUF;
        }
    }
    return LJACK_TNONE;
}

static auproc_obj_type getObjectType(lua_State* L, int index)
{
    void* udata = lua_touserdata(L, index);
    if (udata) {
        size_t len = lua_rawlen(L, index);
        switch (internGetObjectType(udata, len)) {
            case LJACK_TCLIENT:  return AUPROC_TENGINE;
            case LJACK_TPORT:    return AUPROC_TCONNECTOR;
            case LJACK_TPROCBUF: return AUPROC_TCONNECTOR;
        }
    }
    return AUPROC_TNONE;
}

/* ============================================================================================ */

static auproc_engine* getEngine(lua_State* L, int index, auproc_info* info)
{
    void*  udata = lua_touserdata(L, index);
    size_t len   = udata ? lua_rawlen(L, index) : 0;
    
    ljack_obj_type type = internGetObjectType(udata, len);

    ClientUserData* clientUdata = NULL;
    switch (type) {
        case LJACK_TCLIENT:   clientUdata = ((ClientUserData*) udata); break;
        case LJACK_TPORT:     clientUdata = ((PortUserData*)   udata)->clientUserData; break;
        case LJACK_TPROCBUF:  clientUdata = ((ProcBufUserData*)udata)->clientUserData; break;
    }
    if (clientUdata) {
        ljack_client_check_is_valid(L, clientUdata);
    }
    if (info) {
        memset(info, 0, sizeof(auproc_info));
        info->sampleRate = clientUdata->sampleRate;
    }
    return (auproc_engine*) clientUdata;
}

/* ============================================================================================ */

static int isEngineClosed(auproc_engine* engine)
{
    ClientUserData* udata = (ClientUserData*) engine;
    ljack_client_handle_shutdown(udata);
    return !udata || !udata->client;
}

/* ============================================================================================ */

static void checkEngineIsNotClosed(lua_State* L, auproc_engine* engine)
{
    ClientUserData* udata = (ClientUserData*) engine;
    ljack_client_check_is_valid(L, udata);
}

/* ============================================================================================ */

static auproc_con_type getConnectorType(lua_State* L, int index)
{
    PortUserData*    portUdata    = NULL;
    ProcBufUserData* procBufUdata = NULL;
    ljack_client_intern_get_connector(L, index, &portUdata, &procBufUdata);
    if (portUdata) {
        if (portUdata->isAudio) return AUPROC_AUDIO;
        if (portUdata->isMidi)  return AUPROC_MIDI;
    }
    else if (procBufUdata) {
        if (procBufUdata->isAudio) return AUPROC_AUDIO;
        if (procBufUdata->isMidi)  return AUPROC_MIDI;
    }
    return 0;
}


/* ============================================================================================ */

static auproc_direction getPossibleDirections(lua_State* L, int index)
{
    PortUserData*    portUdata    = NULL;
    ProcBufUserData* procBufUdata = NULL;
    ljack_client_intern_get_connector(L, index, &portUdata, &procBufUdata);
    auproc_direction rslt = AUPROC_NONE;
    if (portUdata) {
        if (portUdata->isInput)                                       rslt |= AUPROC_IN;
        if (portUdata->isOutput && portUdata->procUsageCounter == 0)  rslt |= AUPROC_OUT;
    }
    else if (procBufUdata) {
        if (procBufUdata->outUsageCounter == 0) rslt = AUPROC_OUT;
        else                                    rslt = AUPROC_IN;
    }
    return rslt;
}

/* ============================================================================================ */

static auproc_reg_err_type checkPortReg(ClientUserData*   clientUdata,
                                        PortUserData*     udata,
                                        auproc_con_reg*   conReg)
{
    if (!udata || !udata->client) {
        return AUPROC_REG_ERR_CONNCTOR_INVALID;
    }
    if (   udata->clientUserData != clientUdata
        || !jack_port_is_mine(udata->client, udata->port))
    {
        return AUPROC_REG_ERR_ENGINE_MISMATCH;
    }
    if (   (conReg->conDirection == AUPROC_IN  && !udata->isInput)
        || (conReg->conDirection == AUPROC_OUT && !udata->isOutput)
        || (conReg->conDirection == AUPROC_OUT &&  udata->isOutput && udata->procUsageCounter > 0)) 
    {
        return AUPROC_REG_ERR_WRONG_DIRECTION;
    }
    
    if (conReg->conType == AUPROC_AUDIO && udata->isAudio) {
        return AUPROC_CAPI_REG_NO_ERROR;
    }
    else if (conReg->conType == AUPROC_MIDI && udata->isMidi) {
        return AUPROC_CAPI_REG_NO_ERROR;
    }
    else {
        return AUPROC_REG_ERR_WRONG_CONNECTOR_TYPE;
    }
}

/* ============================================================================================ */

static auproc_reg_err_type checkProcBufReg(ClientUserData*  clientUdata,
                                           ProcBufUserData* udata,
                                           auproc_con_reg*  conReg)
{
    if (!udata || !udata->jackClient) {
        return AUPROC_REG_ERR_CONNCTOR_INVALID;
    }
    if (udata->clientUserData != clientUdata) {
        return AUPROC_REG_ERR_ENGINE_MISMATCH;
    }
    if ( (conReg->conDirection == AUPROC_IN  && udata->outUsageCounter == 0)
      || (conReg->conDirection == AUPROC_OUT && udata->outUsageCounter != 0))
    {
        return AUPROC_REG_ERR_WRONG_DIRECTION;
    }
    
    if (conReg->conType == AUPROC_AUDIO && udata->isAudio) {
        return AUPROC_CAPI_REG_NO_ERROR;
    }
    else if (conReg->conType == AUPROC_MIDI && udata->isMidi) {
        return AUPROC_CAPI_REG_NO_ERROR;
    }
    else {
        return AUPROC_REG_ERR_WRONG_CONNECTOR_TYPE;
    }
}



/* ============================================================================================ */

static auproc_processor* registerProcessor(lua_State* L, 
                                           int firstConnectorIndex, int connectorCount,
                                           auproc_engine* engine, 
                                           const char* processorName,
                                           void* processorData,
                                           int  (*processCallback)(jack_nframes_t nframes, void* processorData),
                                           int  (*bufferSizeCallback)(jack_nframes_t nframes, void* processorData),
                                           void (*engineClosedCallback)(void* processorData),
                                           void (*engineReleasedCallback)(void* processorData),
                                           auproc_con_reg* conRegList,
                                           auproc_con_reg_err*  regError)
{
    ClientUserData* clientUdata = (ClientUserData*) engine;
    
    if (!processorName || !processorData || !processCallback) {
        if (regError) {
            regError->errorType = AUPROC_REG_ERR_CALL_INVALID;
            regError->conIndex = -1;
        }
        return NULL;
    }
    
    for (int i = 0; i < connectorCount; ++i) {
        PortUserData*    portUdata    = NULL;
        ProcBufUserData* procBufUdata = NULL;
        ljack_client_intern_get_connector(L, firstConnectorIndex + i, &portUdata, &procBufUdata);
        if (portUdata == NULL && procBufUdata == NULL) {
            if (regError) {
                regError->errorType = AUPROC_REG_ERR_ARG_INVALID;
                regError->conIndex = i;
            }
            return NULL;
        }
        auproc_con_reg*     conReg = conRegList + i;
        auproc_reg_err_type err;
        if (   (   conReg->conDirection == AUPROC_IN 
                || conReg->conDirection == AUPROC_OUT)

            && (   conReg->conType == AUPROC_AUDIO
                || conReg->conType == AUPROC_MIDI))
        {
            if (portUdata) {
                err = checkPortReg(clientUdata, portUdata, conReg);
            }
            else if (procBufUdata) {
                err = checkProcBufReg(clientUdata, procBufUdata, conReg);
            }
            else {
                err = AUPROC_REG_ERR_ARG_INVALID;
            }
        } else {
            err = AUPROC_REG_ERR_CALL_INVALID;
        }
        if (err != AUPROC_CAPI_REG_NO_ERROR) {
            if (regError) {
                regError->errorType = err;
                regError->conIndex = i;
            }
            return NULL;
        }
    }
    
    lua_newtable(L);                                     /* -> connectorTable */
    for (int i = 0; i < connectorCount; ++i) {
        lua_pushvalue(L, firstConnectorIndex + i);       /* -> connectorTable, connector */
        lua_rawseti(L, -2, i + 1);                       /* -> connectorTable */
    }
    int connectorTableRef = luaL_ref(L, LUA_REGISTRYINDEX);   /* -> */
    
    LjackProcReg** oldList = clientUdata->procRegList;
    int            n       = clientUdata->procRegCount;
    
    LjackProcReg*   newReg    = calloc(1, sizeof(LjackProcReg));
    int             newLength = n + 1;
    LjackProcReg**  newList   = calloc(newLength + 1, sizeof(LjackProcReg*));
    ConnectorInfo*  conInfos  = calloc(connectorCount, sizeof(ConnectorInfo));
    char*           procName  = malloc(strlen(processorName) + 1);
    if (!newReg || !newList || !conInfos || !procName) {
        if (newReg)   free(newReg);
        if (newList)  free(newList);
        if (conInfos) free(conInfos);
        if (procName) free(procName);
        luaL_unref(L, LUA_REGISTRYINDEX, connectorTableRef);
        luaL_error(L, "out of memory");
        return NULL;
    }
    if (oldList) {
        memcpy(newList, oldList, sizeof(LjackProcReg*) * n);
    }
    newList[newLength-1] = newReg;
    newList[newLength]   = NULL;
    strcpy(procName, processorName);

    newReg->processorName          = procName;
    newReg->processorData          = processorData;
    newReg->processCallback        = processCallback;
    newReg->bufferSizeCallback     = bufferSizeCallback;
    newReg->engineClosedCallback   = engineClosedCallback;
    newReg->engineReleasedCallback = engineReleasedCallback;
    newReg->connectorTableRef      = connectorTableRef;
    newReg->connectorCount         = connectorCount;
    newReg->connectorInfos         = conInfos;
      
    async_mutex_lock(&clientUdata->processMutex);
    {
        int rc = 0;
        if (bufferSizeCallback) {
            rc = bufferSizeCallback(clientUdata->bufferSize, processorData);
        }
        if (rc != 0) {
            async_mutex_unlock(&clientUdata->processMutex);
            free(newReg);
            free(newList);
            free(conInfos);
            free(procName);
            luaL_unref(L, LUA_REGISTRYINDEX, connectorTableRef);
            luaL_error(L, "error %d from bufferSizeCallback for processor '%s'", processorName);
            return NULL;
        }
        newReg->bufferSize = clientUdata->bufferSize;

        clientUdata->procRegList  = newList;
        clientUdata->procRegCount = newLength;
        ljack_client_intern_activate_proc_list_LOCKED(clientUdata, newList);
    }
    async_mutex_unlock(&clientUdata->processMutex);
    
    if (oldList) {
        free(oldList);
    }
    
    for (int i = 0; i < connectorCount; ++i) {
        PortUserData*    portUdata    = NULL;
        ProcBufUserData* procBufUdata = NULL;
        ljack_client_intern_get_connector(L, firstConnectorIndex + i, &portUdata, &procBufUdata);
        if (portUdata) {
            conInfos[i].isPort = true;
            portUdata->procUsageCounter += 1;
            if (conRegList[i].conDirection == AUPROC_IN) {
                conInfos[i].isInput  = true;
            } else {
                conInfos[i].isOutput = true;
            }
            conInfos[i].portUdata = portUdata;
            conRegList[i].connector = (auproc_connector*)portUdata;
            if (portUdata->isAudio) {
                conRegList[i].audioMethods = &portAudioMethods;
                conRegList[i].midiMethods  = NULL;
            }
            else if (portUdata->isMidi) {
                conRegList[i].audioMethods = NULL;
                conRegList[i].midiMethods  = &portMidiMethods;
            }
        } 
        else if (procBufUdata) {
            conInfos[i].isProcBuf = true;
            procBufUdata->procUsageCounter += 1;
            if (conRegList[i].conDirection == AUPROC_IN) {
                procBufUdata->inpUsageCounter += 1;
                conInfos[i].isInput = true;
            } else {
                conInfos[i].isOutput = true;
                procBufUdata->outUsageCounter += 1;
            }
            conInfos[i].procBufUdata = procBufUdata;
            conRegList[i].connector = (auproc_connector*)procBufUdata;
            if (procBufUdata->isAudio) {
                conRegList[i].audioMethods = &procBufAudioMethods;
                conRegList[i].midiMethods  = NULL;
            }
            else if (procBufUdata->isMidi) {
                conRegList[i].audioMethods = NULL;
                conRegList[i].midiMethods  = &procBufMidiMethods;
            }
        }
    }
    return (auproc_processor*)newReg;
}

/* ============================================================================================ */

static void unregisterProcessor(lua_State* L,
                                auproc_engine* engine,
                                auproc_processor* processor)
{
    ClientUserData* clientUdata = (ClientUserData*) engine;
    LjackProcReg*   reg         = (LjackProcReg*)   processor;

    int            n       = clientUdata->procRegCount;
    LjackProcReg** oldList = clientUdata->procRegList;

    int index = -1;
    if (oldList) {
        for (int i = 0; i < n; ++i) {
            if (oldList[i] == reg) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        luaL_error(L, "processor is not registered");
        return;
    }

    for (int i = 0; i < reg->connectorCount; ++i) {
        LjackConnectorInfo* info = reg->connectorInfos + i;
        if (info->isProcBuf && info->isOutput) {
            if (info->procBufUdata->inpUsageCounter > 0) {
                luaL_error(L, "process buffer %p data is used by another registered processor", info->procBufUdata);
                return;
            }
        }
    }

    /* --------------------------------------------------------------------- */
    
    LjackProcReg** newList = malloc(sizeof(LjackProcReg*) * (n - 1 + 1));
    
    if (!newList) {
        luaL_error(L, "out of memory");
        return;
    }
    if (index > 0) {
        memcpy(newList, oldList, sizeof(LjackProcReg*) * index);
    }
    if (index + 1 <= n) {
        memcpy(newList + index, oldList + index + 1, sizeof(LjackProcReg*) * ((n + 1) - (index + 1)));
    }
    
    async_mutex_lock(&clientUdata->processMutex);
    {
        clientUdata->procRegList  = newList;
        clientUdata->procRegCount = n - 1;
        
        ljack_client_intern_activate_proc_list_LOCKED(clientUdata, newList);
    }
    async_mutex_unlock(&clientUdata->processMutex);
    
    ljack_client_intern_release_proc_reg(L, reg);

    free(oldList);
}

/* ============================================================================================ */

static void activateProcessor(lua_State* L,
                              auproc_engine* engine,
                              auproc_processor* processor)
{
    ClientUserData* clientUdata = (ClientUserData*) engine;
    LjackProcReg*   reg         = (LjackProcReg*)   processor;

    if (!reg->activated) 
    {
        for (int i = 0; i < reg->connectorCount; ++i) {
            LjackConnectorInfo* info = reg->connectorInfos + i;
            if (info->isProcBuf && info->isInput) {
                if (info->procBufUdata->outActiveCounter < 1) {
                    luaL_error(L, "process buffer %p has no activated processor for providing data", info->procBufUdata);
                    return;
                }
            }
        }
        for (int i = 0; i < reg->connectorCount; ++i) {
            LjackConnectorInfo* info = reg->connectorInfos + i;
            if (info->isProcBuf) {
                if (info->isInput) {
                    info->procBufUdata->inpActiveCounter += 1;
                } else {
                    info->procBufUdata->outActiveCounter += 1;
                }
            }
        }
        reg->activated = true;
    }
}

/* ============================================================================================ */

static void deactivateProcessor(lua_State* L,
                                auproc_engine* engine,
                                auproc_processor* processor)
{
    ClientUserData* clientUdata = (ClientUserData*) engine;
    LjackProcReg*   reg         = (LjackProcReg*)   processor;

    if (reg->activated)
    {
        for (int i = 0; i < reg->connectorCount; ++i) {
            LjackConnectorInfo* info = reg->connectorInfos + i;
            if (info->isProcBuf && info->isOutput) {
                if (info->procBufUdata->inpActiveCounter > 0) {
                    luaL_error(L, "process buffer %p data is used by another activated processor", info->procBufUdata);
                    return;
                }
            }
        }
        for (int i = 0; i < reg->connectorCount; ++i) {
            LjackConnectorInfo* info = reg->connectorInfos + i;
            if (info->isProcBuf) {
                if (info->isInput) {
                    info->procBufUdata->inpActiveCounter -= 1;
                } else {
                    info->procBufUdata->outActiveCounter -= 1;
                }
            }
        }
        reg->activated = false;
    }
}

/* ============================================================================================ */

static uint32_t getProcessBeginFrameTime(auproc_engine* engine)
{
    ClientUserData* clientUdata = (ClientUserData*) engine;

    return jack_last_frame_time(clientUdata->client);
}

/* ============================================================================================ */

static void logError(auproc_engine* engine, const char* fmt, ...)
{
    bool finished;
    do {
        va_list args;
        va_start(args, fmt);
            finished = ljack_log_errorV(fmt, args);
        va_end(args);
    } while (!finished);
}

/* ============================================================================================ */

static void logInfo(auproc_engine* engine, const char* fmt, ...)
{
    bool finished;
    do {
        va_list args;
        va_start(args, fmt);
            finished = ljack_log_infoV(fmt, args);
        va_end(args);
    } while (!finished);
}

/* ============================================================================================ */


const auproc_capi auproc_capi_impl = 
{
    AUPROC_CAPI_VERSION_MAJOR,
    AUPROC_CAPI_VERSION_MINOR,
    AUPROC_CAPI_VERSION_PATCH,
    
    NULL, /* next_capi */

    getObjectType,
    getEngine,
    isEngineClosed,
    checkEngineIsNotClosed,
    getConnectorType,
    getPossibleDirections,
    registerProcessor,
    unregisterProcessor,
    activateProcessor,
    deactivateProcessor,
    getProcessBeginFrameTime,
    "client", /* engine_category_name */    
    logError,
    logInfo,
};

/* ============================================================================================ */
