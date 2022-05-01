#include <jack/jack.h>

#include "util.h"

#define RECEIVER_CAPI_IMPLEMENT_GET_CAPI 1
#include "receiver_capi.h"

#define LJACK_CAPI_IMPLEMENT_SET_CAPI 1
#include "ljack_capi_impl.h"

#include "client.h"
#include "client_intern.h"
#include "port.h"

typedef struct LjackPortUserData         PortUserData;
typedef struct LjackClientUserData       ClientUserData;
typedef struct LjackClientUserData       ClientUserData;

/* ============================================================================================ */

static const char* LJACK_ERROR_INVALID_CLIENT = "invalid jack client";

const char* const LJACK_CLIENT_CLASS_NAME = "ljack.client";

/* ============================================================================================ */


static void setupClientMeta(lua_State* L);

static int pushClientMeta(lua_State* L)
{
    if (luaL_newmetatable(L, LJACK_CLIENT_CLASS_NAME)) {
        setupClientMeta(L);
    }
    return 1;
}

static int LjackClient_name_size(lua_State* L)
{
    int s = jack_client_name_size(); // maximum number of characters in a JACK client name including the final NULL character
    lua_pushinteger(L, s - 1);
    return 1;
}

static int LjackClient_open(lua_State* L)
{
    int arg = 1;
    const char* clientName = luaL_checkstring(L, arg++);
    const receiver_capi* receiver_capi = NULL;
    receiver_object*     receiver      = NULL;
    if (!lua_isnoneornil(L, arg)) {
        int versErr = 0;
        receiver_capi = receiver_get_capi(L, arg, &versErr);
        if (receiver_capi) {
            receiver = receiver_capi->toReceiver(L, arg);
            ++arg;
        } else {
            if (versErr) {
                return luaL_argerror(L, arg, "receiver api version mismatch");
            }
        }
    }
    if (!receiver && !lua_isnoneornil(L, arg)) {
        return luaL_argerror(L, arg, "expected receiver object");
    }
    
    ClientUserData* udata = lua_newuserdata(L, sizeof(ClientUserData));
    memset(udata, 0, sizeof(ClientUserData));
    udata->weakTableRef   = LUA_REFNIL;
    udata->strongTableRef = LUA_REFNIL;
    async_mutex_init(&udata->processMutex);
    pushClientMeta(L);                                      /* -> udata, meta */
    lua_setmetatable(L, -2);                                /* -> udata */

    lua_newtable(L);                                        /* -> udata, weakTable */
    lua_newtable(L);                                        /* -> udata, weakTable, meta */
    lua_pushstring(L, "__mode");                            /* -> udata, weakTable, meta, key */
    lua_pushstring(L, "v");                                 /* -> udata, weakTable, meta, key, value */
    lua_rawset(L, -3);                                      /* -> udata, weakTable, meta */
    lua_setmetatable(L, -2);                                /* -> udata, weakTable */
    lua_pushvalue(L, -2);                                   /* -> udata, weakTable, udata */
    lua_rawsetp(L, -2, udata);                              /* -> udata, weakTable */
    udata->weakTableRef = luaL_ref(L, LUA_REGISTRYINDEX);   /* -> udata */
    lua_newtable(L);                                        /* -> udata, strongTable */
    udata->strongTableRef = luaL_ref(L, LUA_REGISTRYINDEX); /* -> udata */
    
    if (receiver) {
        udata->receiver_capi   = receiver_capi;
        udata->receiver_writer = receiver_capi->newWriter(1024, 2);
        if (!udata->receiver_writer) {
            return luaL_error(L, "error creating writer for receiver");
        }
        receiver_capi->retainReceiver(receiver);
        udata->receiver = receiver;
    }
    
    jack_status_t status = {0};
    udata->client = jack_client_open(clientName, JackNullOption, &status);
    if (udata->client) {
        ljack_client_intern_register_callbacks(udata);
    }
    if (!udata->client) {
        return luaL_error(L, "cannot open jack client");
    }
    return 1;
}

/* ============================================================================================ */

static void internalClientClose(lua_State* L, ClientUserData* udata)
{
    if (udata->client) {
        if (udata->procRegList) {
            for (int i = 0; i < udata->procRegCount; ++i) {
                LjackProcReg* reg = udata->procRegList + i;
                if (reg->clientClosedCallback) {
                    reg->clientClosedCallback(L, reg->processorData);
                }
                ljack_client_intern_release_proc_reg(L, reg);
            }
            free(udata->procRegList);
            udata->procRegList = NULL;
            udata->activatedProcRegList = NULL;
            udata->procRegCount = 0;
        }
        jack_client_close(udata->client);
        udata->client = NULL;
        udata->activated = NULL;
        {
            PortUserData* p = udata->firstPortUserData;
            while (p) {
                p->client = NULL;
                p->port   = NULL;
                p = p->nextPortUserData;
            }
        }
    }
}

void ljack_handle_client_shutdown(lua_State* L, ClientUserData* udata)
{
    if (udata) {
        internalClientClose(L, udata);
    }
}


static int LjackClient_release(lua_State* L)
{
    ClientUserData* udata = luaL_checkudata(L, 1, LJACK_CLIENT_CLASS_NAME);
    if (!udata->closed) {
        if (udata->weakTableRef != LUA_REFNIL) {
            luaL_unref(L, LUA_REGISTRYINDEX, udata->weakTableRef);
            udata->weakTableRef = LUA_REFNIL;
        }
        if (udata->strongTableRef != LUA_REFNIL) {
            luaL_unref(L, LUA_REGISTRYINDEX, udata->strongTableRef);
            udata->strongTableRef = LUA_REFNIL;
        }
        if (udata->client) {
            internalClientClose(L, udata);
        }
        while (udata->firstPortUserData) {
            ljack_port_release(L, udata->firstPortUserData);
        }
        if (udata->receiver_writer) {
            udata->receiver_capi->freeWriter(udata->receiver_writer);
            udata->receiver_writer = NULL;
        }
        if (udata->receiver) {
            udata->receiver_capi->releaseReceiver(udata->receiver);
            udata->receiver = NULL;
        }
        async_mutex_destruct(&udata->processMutex);
        udata->closed = true;
    }
    return 0;
}

/* ============================================================================================ */

int ljack_client_push_client_object(lua_State* L, ClientUserData* udata)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, udata->weakTableRef);      /* -> weakTable */
    lua_rawgetp(L, -1, udata);                                   /* -> weakTable, udata */
    return 1;
}

/* ============================================================================================ */

int ljack_client_check_shutdown(lua_State* L, ClientUserData* udata)
{
    if (atomic_get(&udata->shutdownReceived)) {
        ljack_handle_client_shutdown(L, udata);
        return luaL_error(L, "error: jack client shutdown");
    } else {
        return 0;
    }
}

/* ============================================================================================ */

static ClientUserData* checkClientUdata(lua_State* L, int arg)
{
    ClientUserData* udata = luaL_checkudata(L, arg, LJACK_CLIENT_CLASS_NAME);
    if (!udata->client) {
        luaL_error(L, LJACK_ERROR_INVALID_CLIENT);
        return NULL;
    }
    ljack_client_check_shutdown(L, udata);
    return udata;
}

/* ============================================================================================ */

static int LjackClient_activate(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);

    if (jack_activate(udata->client) != 0) {
        internalClientClose(L, udata);
        return luaL_error(L, "error: cannot activate client");
    }
    udata->activated = true;
    return 0;
}

/* ============================================================================================ */

static int LjackClient_deactivate(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);

    if (jack_deactivate(udata->client) != 0) {
        internalClientClose(L, udata);
        return luaL_error(L, "error: cannot deactivate client");
    }
    udata->activated = false;
    return 0;
}

/* ============================================================================================ */

static int LjackClient_toString(lua_State* L)
{
    ClientUserData* udata = luaL_checkudata(L, 1, LJACK_CLIENT_CLASS_NAME);
    if (udata->client) {
        ljack_util_quote_string(L, jack_get_client_name(udata->client));  /* -> quoted */
        lua_pushfstring(L, "%s: %p (name=%s)", LJACK_CLIENT_CLASS_NAME, 
                                               udata->client, 
                                               lua_tostring(L, -1));      /* -> quoted, rslt */
    } else {
        lua_pushfstring(L, "%s: invalid", LJACK_CLIENT_CLASS_NAME);       /* -> rslt */
    }
    return 1;
}

/* ============================================================================================ */

static int LjackClient_get_client_name(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);
    const char* name = jack_get_client_name(udata->client);
    lua_pushstring(L, name);
    return 1;
}

/* ============================================================================================ */

static int LjackClient_port_name(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    lua_Integer port = luaL_checkinteger(L, arg++);
    
    jack_port_t* p = jack_port_by_id(udata->client, port);
    if (!p) {
        return luaL_error(L, "invalid jack port id %d", port);
    }
    lua_pushstring(L, jack_port_name(p));
    return 1;
}

/* ============================================================================================ */

static int LjackClient_port_short_name(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    lua_Integer port = luaL_checkinteger(L, arg++);
    
    jack_port_t* p = jack_port_by_id(udata->client, port);
    if (!p) {
        return luaL_error(L, "invalid jack port id %d", port);
    }
    lua_pushstring(L, jack_port_short_name(p));
    return 1;
}

/* ============================================================================================ */

static const char* const portTypes[] =
{
    "MIDI",
    "AUDIO",
    NULL
};

static const char* const portDirections[] =
{
    "IN",
    "OUT",
    NULL
};

/* ============================================================================================ */

static void connectPortUserData(lua_State* L, ClientUserData* udata, PortUserData* portUserData)
{                                                                /* -> portUserData */
    portUserData->nextPortUserData = udata->firstPortUserData;
    if (udata->firstPortUserData) {
        udata->firstPortUserData->prevNextPortUserData = &portUserData->nextPortUserData;
    }
    udata->firstPortUserData = portUserData;

    portUserData->clientUserData = udata;
    portUserData->prevNextPortUserData = &udata->firstPortUserData;
    portUserData->shutdownReceived = &udata->shutdownReceived;

    lua_rawgeti(L, LUA_REGISTRYINDEX, udata->weakTableRef);      /* -> portUserData, weakTable */
    lua_pushvalue(L, -2);                                        /* -> portUserData, weakTable, portUserData */
    lua_rawsetp(L, -2, portUserData->port);                      /* -> portUserData, weakTable */
    lua_pop(L, 1);                                               /* -> portUserData */
}

/* ============================================================================================ */

static int LjackClient_port_register(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    const char*        name = luaL_checkstring(L, arg++);
    LjackPortType      type = luaL_checkoption(L, arg++, NULL, portTypes);
    LjackPortDirection dir  = luaL_checkoption(L, arg++, NULL, portDirections);
    
    PortUserData* portUserData = ljack_port_register(L, udata->client, name, type, dir); /* -> portUserData */
    if (!portUserData->port) {
        return luaL_error(L, "cannot register port");
    }                                                            /* -> portUserData */
    connectPortUserData(L, udata, portUserData);                 /* -> portUserData */
    
    return 1;                                                    /* -> portUserData */
}
/* ============================================================================================ */

static int LjackClient_port_by_name(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    const char* name = luaL_checkstring(L, arg++);
    if (!name[0]) {
        lua_pushnil(L);
        return 1;
    }
    jack_port_t* port = jack_port_by_name(udata->client, name);
    if (!port) {
        lua_pushnil(L);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, udata->weakTableRef);      /* -> weakTable */
    if (lua_rawgetp(L, -1, port) == LUA_TNIL) {                  /* -> weakTable, portUserData */
        lua_pop(L, 1);                                           /* -> weakTable */
        PortUserData* portUserData 
                    = ljack_port_create(L, udata->client, port); /* -> weakTable, portUserData */
        
        connectPortUserData(L, udata, portUserData);             /* -> weakTable, portUserData */
    }                                                            /* -> weakTable, portUserData */
    return 1;
}

/* ============================================================================================ */

static int LjackClient_port_by_id(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    lua_Integer id = luaL_checkinteger(L, arg++);
    jack_port_t* port = jack_port_by_id(udata->client, (jack_port_id_t)id);

//    const char* portName = port ? jack_port_name(port) : NULL;
//    if (portName && portName[0]) {
//        port = jack_port_by_name(udata->client, portName); // assure port is still active
//    } else {
//        port = NULL;
//    }
    if (!port) {
        lua_pushnil(L);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, udata->weakTableRef);      /* -> weakTable */
    if (lua_rawgetp(L, -1, port) == LUA_TNIL) {                  /* -> weakTable, portUserData */
        lua_pop(L, 1);                                           /* -> weakTable */
        PortUserData* portUserData 
                    = ljack_port_create(L, udata->client, port); /* -> weakTable, portUserData */
        
        connectPortUserData(L, udata, portUserData);             /* -> weakTable, portUserData */
    }                                                            /* -> weakTable, portUserData */
    return 1;
}

/* ============================================================================================ */

static int LjackClient_get_ports(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    const char*        name = NULL;
    int                type = -1;
    int                dir  = -1;
    if (!lua_isnone(L, arg)) {
        if (lua_isnil(L, arg)) {
            ++arg;
        } else {
            name = luaL_checkstring(L, arg++);
        }
    }
    if (!lua_isnone(L, arg)) {
        if (lua_isnil(L, arg)) {
            ++arg;
        } else {
            type = luaL_checkoption(L, arg++, NULL, portTypes);
        }
    }
    if (!lua_isnone(L, arg)) {
        if (lua_isnil(L, arg)) {
            ++arg;
        } else {
            dir = luaL_checkoption(L, arg++, NULL, portDirections);
        }
    }
    const char*   typeName = NULL;
    unsigned long flags    = 0;
    switch (type) {
        case MIDI:  typeName = JACK_DEFAULT_MIDI_TYPE;  break;
        case AUDIO: typeName = JACK_DEFAULT_AUDIO_TYPE; break;
    }
    switch (dir) {
        case IN:  flags = JackPortIsInput;  break;
        case OUT: flags = JackPortIsOutput; break;
    }

    luaL_checkstack(L, 10, NULL);
    lua_pushcfunction(L, ljack_util_push_string_list); /* -> func ----  may cause mem error in lua 5.1 */

    const char** ports = jack_get_ports(udata->client, name, typeName, flags);
    if (!ports) {
        lua_newtable(L);                 /* -> func, table */
        return 1;
    }
    lua_pushlightuserdata(L, ports);    /* -> func, ports */
    int rc = lua_pcall(L, 1, 1, 0);     /* -> rslt */
    jack_free(ports);
    if (rc != LUA_OK) {
        return lua_error(L);
    }
    return 1;
}

/* ============================================================================================ */


static int LjackClient_connect(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    const char* p1 = ljack_port_name_from_arg(L, arg++);
    const char* p2 = ljack_port_name_from_arg(L, arg++);
    int rc = jack_connect(udata->client, p1, p2);
    lua_pushboolean(L, (rc == 0 || rc == EEXIST));
    return 1;
}

/* ============================================================================================ */

static int LjackClient_disconnect(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    const char* p1 = ljack_port_name_from_arg(L, arg++);
    const char* p2 = ljack_port_name_from_arg(L, arg++);
    int rc = jack_disconnect(udata->client, p1, p2);
    lua_pushboolean(L, (rc == 0));
    return 1;
}

/* ============================================================================================ */

static int LjackClient_is_connected(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    jack_port_t* p1 = ljack_port_ptr_from_arg(L, udata->client, arg++);
    const char*  p2 = ljack_port_name_from_arg(L, arg++);

    bool rslt = p1 && p2 && jack_port_connected_to(p1, p2);
    lua_pushboolean(L, rslt);
    return 1;
}

/* ============================================================================================ */

static int LjackClient_get_connections(lua_State* L)
{
    int arg = 1;
    ClientUserData* udata = checkClientUdata(L, arg++);
    jack_port_t* p = ljack_port_ptr_from_arg(L, udata->client, arg++);

    luaL_checkstack(L, 10, NULL);
    lua_pushcfunction(L, ljack_util_push_string_list); /* -> func ----  may cause mem error in lua 5.1 */

    const char** ports = jack_port_get_all_connections(udata->client, p);
    if (!ports) {
        lua_newtable(L);                  /* -> func, table */
        return 1;
    }
    lua_pushlightuserdata(L, ports);      /* -> func, ports */
    int rc = lua_pcall(L, 1, 1, 0);       /* -> rslt */
    jack_free(ports);
    if (rc != LUA_OK) {
        return lua_error(L);
    }
    return 1;
}

/* ============================================================================================ */

static int LjackClient_get_time(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1); // Crash in jack_get_time if no client was created
    lua_pushinteger(L, jack_get_time());
    return 1;
}

/* ============================================================================================ */

static int LjackClient_get_sample_rate(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);
    lua_pushinteger(L, jack_get_sample_rate(udata->client));
    return 1;
}
/* ============================================================================================ */

static int LjackClient_get_buffer_size(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);
    lua_pushinteger(L, jack_get_buffer_size(udata->client));
    return 1;
}

/* ============================================================================================ */

static int LjackClient_cpu_load(lua_State* L)
{
    ClientUserData* udata = checkClientUdata(L, 1);
    lua_pushnumber(L, jack_cpu_load(udata->client));
    return 1;
}

/* ============================================================================================ */

static const luaL_Reg LjackClientMethods[] = 
{
    { "close",               LjackClient_release         },
    { "activate",            LjackClient_activate        },
    { "deactivate",          LjackClient_deactivate      },
    { "get_client_name",     LjackClient_get_client_name },
    { "port_name",           LjackClient_port_name       },
    { "port_short_name",     LjackClient_port_short_name },
    { "port_register",       LjackClient_port_register   },
    { "port_by_name",        LjackClient_port_by_name    },
    { "port_by_id",          LjackClient_port_by_id      },
    { "get_ports",           LjackClient_get_ports       },
    { "connect",             LjackClient_connect         },
    { "is_connected",        LjackClient_is_connected    },
    { "get_connections",     LjackClient_get_connections },
    { "get_time",            LjackClient_get_time        },
    { "get_sample_rate",     LjackClient_get_sample_rate },
    { "get_buffer_size",     LjackClient_get_buffer_size },
    { "cpu_load",            LjackClient_cpu_load        },

    { NULL,         NULL } /* sentinel */
};

static const luaL_Reg LjackClientMetaMethods[] = 
{
    { "__tostring", LjackClient_toString },
    { "__gc",       LjackClient_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "client_name_size", LjackClient_name_size },
    { "client_open",      LjackClient_open  },
    { NULL,        NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupClientMeta(lua_State* L)
{                                                      /* -> meta */
    lua_pushstring(L, LJACK_CLIENT_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                /* -> meta */

    luaL_setfuncs(L, LjackClientMetaMethods, 0);       /* -> meta */
    
    lua_newtable(L);                                   /* -> meta, ClientClass */
    luaL_setfuncs(L, LjackClientMethods, 0);           /* -> meta, ClientClass */
    lua_setfield (L, -2, "__index");                   /* -> meta */
    ljack_set_capi(L, -1, &ljack_capi_impl);
}


/* ============================================================================================ */

int ljack_client_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, LJACK_CLIENT_CLASS_NAME)) {
        setupClientMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */
