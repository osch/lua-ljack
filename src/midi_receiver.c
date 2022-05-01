#include <jack/jack.h>
#include <jack/midiport.h>

#include "midi_receiver.h"

#define LJACK_CAPI_IMPLEMENT_GET_CAPI 1
#include "ljack_capi.h"

#define RECEIVER_CAPI_IMPLEMENT_GET_CAPI 1
#include "receiver_capi.h"

/* ============================================================================================ */

const char* const LJACK_MIDI_RECEIVER_CLASS_NAME = "ljack.midi_receiver";

static const char* LJACK_ERROR_INVALID_MIDI_RECEIVER = "invalid ljack.midi_receiver";

/* ============================================================================================ */

typedef struct LjackMidiReceiverUserData LjackMidiReceiverUserData;
typedef        LjackMidiReceiverUserData      MidiReceiverUserData;

struct LjackMidiReceiverUserData
{
    jack_client_t*     jackClient;
    jack_port_t*       midiInPort;

    bool               activated;
    
    const ljack_capi*  ljackCapi;
    ljack_capi_client* ljackCapiClient;
    
    const receiver_capi* receiverCapi;
    receiver_object*     receiver;
    receiver_writer*     receiverWriter;
};

/* ============================================================================================ */

static void setupMidiReceiverMeta(lua_State* L);

static int pushMidiReceiverMeta(lua_State* L)
{
    if (luaL_newmetatable(L, LJACK_MIDI_RECEIVER_CLASS_NAME)) {
        setupMidiReceiverMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static MidiReceiverUserData* checkMidiReceiverUdata(lua_State* L, int arg)
{
    MidiReceiverUserData* udata = luaL_checkudata(L, arg, LJACK_MIDI_RECEIVER_CLASS_NAME);
    if (!udata->jackClient) {
        luaL_error(L, LJACK_ERROR_INVALID_MIDI_RECEIVER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(jack_nframes_t nframes, void* processorData)
{
    MidiReceiverUserData* udata = (MidiReceiverUserData*) processorData;
    
    jack_client_t* client   = udata->jackClient;
    void*          port_buf = jack_port_get_buffer(udata->midiInPort, nframes);
    
    jack_midi_event_t in_event;
    jack_nframes_t event_index = 0;
    jack_nframes_t event_count = jack_midi_get_event_count(port_buf);
    
    const receiver_capi* receiverCapi = udata->receiverCapi;
    receiver_object*     receiver     = udata->receiver;
    receiver_writer*     writer       = udata->receiverWriter;

    jack_nframes_t t0 = jack_last_frame_time(client);
    if (receiver) {
        for (int i = 0; i < event_count; ++i) {
            jack_midi_event_get(&in_event, port_buf, i);
            size_t s = in_event.size;
            if (s > 0) {
                receiverCapi->addIntegerToWriter(writer, jack_frames_to_time(client, t0 + in_event.time));
                receiverCapi->addStringToWriter (writer, in_event.buffer, in_event.size);
                receiverCapi->msgToReceiver(receiver, writer, false /* clear */, false /* nonblock */, 
                                            NULL /* error handler */, NULL /* error handler data */);
            }
        }
    }
    
    return 0;
}

static void clientClosed(lua_State* L, void* processorData)
{
    MidiReceiverUserData* udata = (MidiReceiverUserData*) processorData;
 
    udata->jackClient      = NULL;
    udata->activated       = false;
    udata->ljackCapi       = NULL;
    udata->ljackCapiClient = NULL;
}

/* ============================================================================================ */

static int LjackMidiReceiver_new(lua_State* L)
{
    const int portArg = 1;
    const int recvArg = 2;
    MidiReceiverUserData* udata = lua_newuserdata(L, sizeof(MidiReceiverUserData));
    memset(udata, 0, sizeof(MidiReceiverUserData));
    pushMidiReceiverMeta(L);                                /* -> udata, meta */
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
    const receiver_capi* receiverCapi = receiver_get_capi(L, recvArg, &errReason);
    if (!receiverCapi) {
        if (errReason == 1) {
            return luaL_argerror(L, recvArg, "receiver capi version mismatch");
        } else {
            return luaL_argerror(L, recvArg, "expected object with receiver capi");
        }
    }
    receiver_object* receiver = receiverCapi->toReceiver(L, recvArg);
    if (!receiver) {
        return luaL_argerror(L, recvArg, "expected object with receiver capi");
    }
    udata->receiverCapi = receiverCapi;
    udata->receiver     = receiver;
    receiverCapi->retainReceiver(receiver);
    
    udata->receiverWriter = receiverCapi->newWriter(16 * 1024, 1);
    if (!udata->receiverWriter) {
        return luaL_error(L, "out of memory");
    }
    
    ljack_capi_reg_port portReg = {LJACK_CAPI_PORT_IN, LJACK_CAPI_PORT_MIDI, NULL};
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
    udata->midiInPort      = portReg.jackPort;
    return 1;
}

/* ============================================================================================ */

static int LjackMidiReceiver_release(lua_State* L)
{
    MidiReceiverUserData* udata = luaL_checkudata(L, 1, LJACK_MIDI_RECEIVER_CLASS_NAME);
    if (udata->jackClient) {
        udata->ljackCapi->unregisterProcessor(L, udata->ljackCapiClient, udata);

        udata->jackClient      = NULL;
        udata->activated       = false;
        udata->ljackCapi       = NULL;
        udata->ljackCapiClient = NULL;
        
        if (udata->receiver) {
            if (udata->receiverWriter) {
                udata->receiverCapi->freeWriter(udata->receiverWriter);
                udata->receiverWriter = NULL;
            }
            udata->receiverCapi->releaseReceiver(udata->receiver);
            udata->receiver     = NULL;
            udata->receiverCapi = NULL;
        }
    }
    return 0;
}

/* ============================================================================================ */

static int LjackMidiReceiver_toString(lua_State* L)
{
    MidiReceiverUserData* udata = luaL_checkudata(L, 1, LJACK_MIDI_RECEIVER_CLASS_NAME);

    if (udata->jackClient) {
        lua_pushfstring(L, "%s: %p", LJACK_MIDI_RECEIVER_CLASS_NAME, udata);
    } else {
        lua_pushfstring(L, "%s: invalid", LJACK_MIDI_RECEIVER_CLASS_NAME);
    }
    return 1;
}

/* ============================================================================================ */

static int LjackMidiReceiver_activate(lua_State* L)
{
    MidiReceiverUserData* udata = checkMidiReceiverUdata(L, 1);
    if (!udata->activated) {    
        udata->ljackCapi->activateProcessor(L, udata->ljackCapiClient, udata);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int LjackMidiReceiver_deactivate(lua_State* L)
{
    MidiReceiverUserData* udata = checkMidiReceiverUdata(L, 1);
    if (udata->activated) {                                           
        udata->ljackCapi->deactivateProcessor(L, udata->ljackCapiClient, udata);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg LjackMidiReceiverMethods[] = 
{
    { "activate",    LjackMidiReceiver_activate },
    { "deactivate",  LjackMidiReceiver_deactivate },
    { "close",       LjackMidiReceiver_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg LjackMidiReceiverMetaMethods[] = 
{
    { "__tostring", LjackMidiReceiver_toString },
    { "__gc",       LjackMidiReceiver_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_midi_receiver", LjackMidiReceiver_new },
    { NULL,                NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupMidiReceiverMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, LJACK_MIDI_RECEIVER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, LjackMidiReceiverMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, MidiReceiverClass */
    luaL_setfuncs(L, LjackMidiReceiverMethods, 0);         /* -> meta, MidiReceiverClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int ljack_midi_receiver_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, LJACK_MIDI_RECEIVER_CLASS_NAME)) {
        setupMidiReceiverMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

