#include <jack/jack.h>
#include <jack/midiport.h>

#include "midi_sender.h"

#define LJACK_CAPI_IMPLEMENT_GET_CAPI 1
#include "ljack_capi.h"

#define SENDER_CAPI_IMPLEMENT_GET_CAPI 1
#include "sender_capi.h"

/* ============================================================================================ */

const char* const LJACK_MIDI_SENDER_CLASS_NAME = "ljack.midi_sender";

static const char* LJACK_ERROR_INVALID_MIDI_SENDER = "invalid ljack.midi_sender";

/* ============================================================================================ */

typedef struct LjackMidiSenderUserData LjackMidiSenderUserData;
typedef        LjackMidiSenderUserData      MidiSenderUserData;

struct LjackMidiSenderUserData
{
    jack_client_t*     jackClient;
    jack_port_t*       midiOutPort;

    bool               activated;
    
    const ljack_capi*  ljackCapi;
    ljack_capi_client* ljackCapiClient;
    
    const sender_capi* senderCapi;
    sender_object*     sender;
    sender_reader*     senderReader;
    
    bool               hasNextEvent;
    jack_nframes_t     nextEventFrame;
};

/* ============================================================================================ */

static void setupMidiSenderMeta(lua_State* L);

static int pushMidiSenderMeta(lua_State* L)
{
    if (luaL_newmetatable(L, LJACK_MIDI_SENDER_CLASS_NAME)) {
        setupMidiSenderMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static MidiSenderUserData* checkMidiSenderUdata(lua_State* L, int arg)
{
    MidiSenderUserData* udata = luaL_checkudata(L, arg, LJACK_MIDI_SENDER_CLASS_NAME);
    if (!udata->jackClient) {
        luaL_error(L, LJACK_ERROR_INVALID_MIDI_SENDER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(jack_nframes_t nframes, void* processorData)
{
    MidiSenderUserData* udata = (MidiSenderUserData*) processorData;
    
    jack_client_t* client   = udata->jackClient;
    void*          port_buf = jack_port_get_buffer(udata->midiOutPort, nframes);
    
    jack_midi_clear_buffer(port_buf);
    
    const sender_capi* senderCapi = udata->senderCapi;
    sender_object*     sender     = udata->sender;
    sender_reader*     reader     = udata->senderReader;

    jack_nframes_t f0 = jack_last_frame_time(client);

nextEvent:
    if (!udata->hasNextEvent) {
        int rc = senderCapi->nextMessageFromSender(sender, reader,
                                                   true /* nonblock */, 0 /* timeout */,
                                                   NULL /* errorHandler */, NULL /* errorHandlerData */);
        if (rc == 0) {
            sender_capi_value  senderValue;
            senderCapi->nextValueFromReader(reader, &senderValue);
            if (senderValue.type != SENDER_CAPI_TYPE_NONE) {
                bool hasT = false;
                jack_time_t t;
                if (senderValue.type == SENDER_CAPI_TYPE_INTEGER) {
                    hasT = true;
                    t = senderValue.intVal;
                } else if (senderValue.type == SENDER_CAPI_TYPE_NUMBER) {
                    hasT = true;
                    t = senderValue.numVal;
                }
                if (hasT) {
                    jack_time_t t0 = jack_frames_to_time(udata->jackClient, f0);
                    if (t < t0) {
                        t = t0;
                    }
                    udata->hasNextEvent = true;
                    udata->nextEventFrame = jack_time_to_frames(udata->jackClient, t);
                } else {
                    senderCapi->clearReader(reader);
                }
            }
        }
    }

    if (udata->hasNextEvent) {
        jack_nframes_t f  = udata->nextEventFrame;
        if (f < f0) {
            f = f0;
        }
        if (f >= f0 && f < f0 + nframes) {
            sender_capi_value  senderValue;
            senderCapi->nextValueFromReader(reader, &senderValue);
            if (senderValue.type == SENDER_CAPI_TYPE_STRING) {
                jack_midi_event_write(port_buf, f-f0, (const unsigned char*) senderValue.strVal.ptr, 
                                                                             senderValue.strVal.len);
            }
            senderCapi->clearReader(reader);
            udata->hasNextEvent = false;
            goto nextEvent;
        }
    }
    return 0;
}

static void clientClosed(lua_State* L, void* processorData)
{
    MidiSenderUserData* udata = (MidiSenderUserData*) processorData;
 
    udata->jackClient      = NULL;
    udata->activated       = false;
    udata->ljackCapi       = NULL;
    udata->ljackCapiClient = NULL;
}

/* ============================================================================================ */

static int LjackMidiSender_new(lua_State* L)
{
    const int portArg = 1;
    const int sndrArg = 2;
    MidiSenderUserData* udata = lua_newuserdata(L, sizeof(MidiSenderUserData));
    memset(udata, 0, sizeof(MidiSenderUserData));
    pushMidiSenderMeta(L);                                /* -> udata, meta */
    lua_setmetatable(L, -2);                                /* -> udata */
    int versionError = 0;
    const ljack_capi* capi = ljack_get_capi(L, portArg, &versionError);
    ljack_capi_client* capiClient = NULL;
    if (capi) {
        capiClient = capi->getLjackClient(L, portArg);
    }
    if (!capi || !capiClient) {
        if (versionError) {
            return luaL_argerror(L, portArg, "ljack version mismatch");
        } else {
            return luaL_argerror(L, portArg, "expected ljack.port");
        }
    }
    
    int errReason = 0;
    const sender_capi* senderCapi = sender_get_capi(L, sndrArg, &errReason);
    if (!senderCapi) {
        if (errReason == 1) {
            return luaL_argerror(L, sndrArg, "sender capi version mismatch");
        } else {
            return luaL_argerror(L, sndrArg, "expected object with sender capi");
        }
    }
    sender_object* sender = senderCapi->toSender(L, sndrArg);
    if (!sender) {
        return luaL_argerror(L, sndrArg, "expected object with sender capi");
    }
    udata->senderCapi = senderCapi;
    udata->sender     = sender;
    senderCapi->retainSender(sender);
    
    udata->senderReader = senderCapi->newReader(16 * 1024, 1);
    if (!udata->senderReader) {
        return luaL_error(L, "out of memory");
    }
    
    ljack_capi_reg_port portReg = {LJACK_CAPI_PORT_OUT, LJACK_CAPI_PORT_MIDI, NULL};
    ljack_capi_reg_err portError = {0};
    jack_client_t* jackClient = capi->registerProcessor(L, portArg, 1, capiClient, udata, processCallback, clientClosed, &portReg, &portError);
    if (!jackClient)
    {
        if (portError.portError == LJACK_CAPI_PORT_ERR_PORT_INVALID) {
            return luaL_argerror(L, portArg, "invalid ljack.port");
        }
        else if (portError.portError == LJACK_CAPI_PORT_ERR_CLIENT_MISMATCH
              || portError.portError == LJACK_CAPI_PORT_ERR_PORT_NOT_MINE) 
        {
            return luaL_argerror(L, portArg, "port belongs to other client");
        }
        else if (portError.portError != LJACK_CAPI_PORT_NO_ERROR) {
            return luaL_argerror(L, portArg, "expected MIDI IN port");
        }
        else {
            return luaL_error(L, "cannot register processor");
        }
    }
 
    udata->jackClient      = jackClient;
    udata->activated       = false;
    udata->ljackCapi       = capi;
    udata->ljackCapiClient = capiClient;
    udata->midiOutPort     = portReg.jackPort;
    return 1;
}

/* ============================================================================================ */

static int LjackMidiSender_release(lua_State* L)
{
    MidiSenderUserData* udata = luaL_checkudata(L, 1, LJACK_MIDI_SENDER_CLASS_NAME);
    if (udata->jackClient) {
        udata->ljackCapi->unregisterProcessor(L, udata->ljackCapiClient, udata);

        udata->jackClient      = NULL;
        udata->activated       = false;
        udata->ljackCapi       = NULL;
        udata->ljackCapiClient = NULL;
        
        if (udata->sender) {
            if (udata->senderReader) {
                udata->senderCapi->freeReader(udata->senderReader);
                udata->senderReader = NULL;
            }
            udata->senderCapi->releaseSender(udata->sender);
            udata->sender     = NULL;
            udata->senderCapi = NULL;
        }
    }
    return 0;
}

/* ============================================================================================ */

static int LjackMidiSender_toString(lua_State* L)
{
    MidiSenderUserData* udata = luaL_checkudata(L, 1, LJACK_MIDI_SENDER_CLASS_NAME);

    if (udata->jackClient) {
        lua_pushfstring(L, "%s: %p", LJACK_MIDI_SENDER_CLASS_NAME, udata);
    } else {
        lua_pushfstring(L, "%s: invalid", LJACK_MIDI_SENDER_CLASS_NAME);
    }
    return 1;
}

/* ============================================================================================ */

static int LjackMidiSender_activate(lua_State* L)
{
    MidiSenderUserData* udata = checkMidiSenderUdata(L, 1);
    if (!udata->activated) {    
        udata->ljackCapi->activateProcessor(L, udata->ljackCapiClient, udata);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int LjackMidiSender_deactivate(lua_State* L)
{
    MidiSenderUserData* udata = checkMidiSenderUdata(L, 1);
    if (udata->activated) {                                           
        udata->ljackCapi->deactivateProcessor(L, udata->ljackCapiClient, udata);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg LjackMidiSenderMethods[] = 
{
    { "activate",    LjackMidiSender_activate },
    { "deactivate",  LjackMidiSender_deactivate },
    { "close",       LjackMidiSender_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg LjackMidiSenderMetaMethods[] = 
{
    { "__tostring", LjackMidiSender_toString },
    { "__gc",       LjackMidiSender_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_midi_sender", LjackMidiSender_new },
    { NULL,                NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupMidiSenderMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, LJACK_MIDI_SENDER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, LjackMidiSenderMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, MidiSenderClass */
    luaL_setfuncs(L, LjackMidiSenderMethods, 0);         /* -> meta, MidiSenderClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int ljack_midi_sender_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, LJACK_MIDI_SENDER_CLASS_NAME)) {
        setupMidiSenderMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

