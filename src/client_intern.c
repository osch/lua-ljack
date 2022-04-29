#include <jack/jack.h>

#include "util.h"
#include "error.h"
#include "receiver_capi.h"

#include "client_intern.h"
#include "port.h"

typedef LjackPortUserData        PortUserData;
typedef LjackClientUserData      ClientUserData;


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
        fprintf(stderr, "Error while calling ljack client callback: %s\n", ehdata.buffer);
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

static int jackSampleRateCallback(jack_nframes_t nframes, void* arg)
{
    ClientUserData* udata = arg;

    if (udata->receiver) {
        addStringToWriter (udata, "SampleRate");
        addIntegerToWriter(udata, (lua_Integer)nframes);
        addMsgToReceiver  (udata);
    }
    return 0;
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
    atomic_inc        (&udata->shutdownReceived);
    async_mutex_notify(&udata->processMutex);
    async_mutex_unlock(&udata->processMutex);
}

/* ============================================================================================ */

typedef int ProcessCallback(jack_nframes_t nframes, void* processorData);

static int jackProcessCallback(jack_nframes_t nframes, void* arg)
{
    ClientUserData* udata = arg;
    
    LjackProcReg* list = udata->procRegList;

    if (udata->activatedProcRegList != list)
    {
        if (async_mutex_trylock(&udata->processMutex)) {
            udata->activatedProcRegList = list;
            async_mutex_notify(&udata->processMutex);
            async_mutex_unlock(&udata->processMutex);
        }
    }
    int rc = 0;
    if (list) {
        LjackProcReg* reg = list;
        while (true) 
        {
            ProcessCallback* processCallback = reg->processCallback;
            if (!processCallback) {
                break;
            }
            if (reg->activated) {
                if (processCallback(nframes, reg->processorData) != 0) {
                    rc += 1;
                }
            }
            ++reg;
        }
    }
    return rc;
}

/* ============================================================================================ */

void ljack_client_intern_activate_proc_list(ClientUserData* udata)
{
    if (udata->activated) {
        async_mutex_lock(&udata->processMutex);
        {
            while (   atomic_get(&udata->shutdownReceived) == 0
                   && udata->activatedProcRegList != udata->procRegList) 
            {
                async_mutex_wait(&udata->processMutex);
            }
            udata->activatedProcRegList = udata->procRegList;
        }
        async_mutex_unlock(&udata->processMutex);
    } else {
        udata->activatedProcRegList  = udata->procRegList;
    }
}

/* ============================================================================================ */

void ljack_client_intern_release_proc_reg(lua_State* L, LjackProcReg* reg)
{
    reg->processorData        = NULL;
    reg->processCallback      = NULL;
    reg->clientClosedCallback = NULL;
    reg->activated            = false;
    
    if (reg->portTableRef != LUA_REFNIL) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, reg->portTableRef);  /* -> portTable */
        for (int i = 1; i <= reg->portCount; ++i) {
            lua_rawgeti(L, -1, i);                             /* -> portTable, port */
            PortUserData* portUdata = lua_touserdata(L, -1);   /* -> portTable, port */
            if (portUdata) {
                portUdata->procUsageCounter -= 1;
            }
            lua_pop(L, 1);                                     /* -> portTable */
        }
        lua_pop(L, 1);                                         /* -> */
        luaL_unref(L, LUA_REGISTRYINDEX, reg->portTableRef);
        reg->portTableRef = LUA_REFNIL;
        reg->portCount = 0;
    }
}


/* ============================================================================================ */

void ljack_client_intern_register_callbacks(ClientUserData* udata)
{
    jack_set_graph_order_callback         (udata->client, jackGraphOrderCallback,             udata);
    jack_set_client_registration_callback (udata->client, jackClientRegistrationCallback,     udata);
    jack_set_port_connect_callback        (udata->client, jackPortConnectCallback,            udata);
    jack_set_port_registration_callback   (udata->client, jackPortRegistrationCallback,       udata);
    jack_set_port_rename_callback         (udata->client, jackPortRenameCallback,             udata);
    jack_set_sample_rate_callback         (udata->client, jackSampleRateCallback,             udata);
    jack_set_xrun_callback                (udata->client, jackXRunCallback,                   udata);
    jack_set_process_callback             (udata->client, jackProcessCallback,                udata);
    
    jack_on_info_shutdown                 (udata->client, jackInfoShutdownCallback,           udata);
}

/* ============================================================================================ */
