#include <jack/jack.h>

#include "util.h"

#define LJACK_CAPI_IMPLEMENT_SET_CAPI 1
#include "ljack_capi_impl.h"

#include "port.h"

/* ============================================================================================ */

static const char* LJACK_ERROR_INVALID_PORT = "invalid jack port";

const char* const LJACK_PORT_CLASS_NAME = "ljack.port";

typedef struct LjackPortUserData   PortUserData;
typedef struct LjackClientUserData ClientUserData;

typedef LjackPortType       PortType;
typedef LjackPortDirection  PortDirection;

/* ============================================================================================ */


static void setupPortMeta(lua_State* L);

static int pushPortMeta(lua_State* L)
{
    if (luaL_newmetatable(L, LJACK_PORT_CLASS_NAME)) {
        setupPortMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static int LjackPort_name_size(lua_State* L)
{
    int s = jack_port_name_size(); // the maximum number of characters in a full JACK port name including the final NULL character.
    lua_pushinteger(L, s - 1);
    return 1;
}


/* ============================================================================================ */

PortUserData* ljack_port_register(lua_State* L, jack_client_t* client, const char* name, PortType type, PortDirection dir)
{
    PortUserData* udata = lua_newuserdata(L, sizeof(PortUserData));
    memset(udata, 0, sizeof(PortUserData));
    pushPortMeta(L);         /* -> udata, meta */
    lua_setmetatable(L, -2); /* -> udata */
    const char* typeName = NULL;
    unsigned long flags = 0;
    switch (type) {
        case MIDI:  typeName = JACK_DEFAULT_MIDI_TYPE;  break;
        case AUDIO: typeName = JACK_DEFAULT_AUDIO_TYPE; break;
    }
    switch (dir) {
        case IN:  flags = JackPortIsInput;  break;
        case OUT: flags = JackPortIsOutput; break;
    }
    udata->client = client;
    udata->port   = jack_port_register(client, name, typeName, flags, 0);
    return udata;
}

/* ============================================================================================ */

static int getPortFlags(PortUserData* udata) 
{
    if (!udata->hasPortFlags) {
        udata->portFlags = jack_port_flags(udata->port);
        udata->hasPortFlags = true;
    }
    return udata->portFlags;
}

/* ============================================================================================ */

static void getType(PortUserData* udata) 
{
    const char* type = jack_port_type(udata->port);
    udata->hasType = true;
    udata->isAudio = (strcmp(type, JACK_DEFAULT_AUDIO_TYPE) == 0);
    udata->isMidi  = (strcmp(type, JACK_DEFAULT_MIDI_TYPE)  == 0);
}

/* ============================================================================================ */

static int isAudio(PortUserData* udata) 
{
    if (!udata->hasType) {
        getType(udata);
    }
    return udata->isAudio;
}

/* ============================================================================================ */

static int isMidi(PortUserData* udata) 
{
    if (!udata->hasType) {
        getType(udata);
    }
    return udata->isMidi;
}

/* ============================================================================================ */

PortUserData* ljack_port_create(lua_State* L, jack_client_t* client, jack_port_t* port)
{
    PortUserData* udata = lua_newuserdata(L, sizeof(PortUserData));
    memset(udata, 0, sizeof(PortUserData));
    pushPortMeta(L);         /* -> udata, meta */
    lua_setmetatable(L, -2); /* -> udata */
    udata->client = client;
    udata->port   = port;
    return udata;
}

/* ============================================================================================ */

void ljack_port_release(lua_State* L, PortUserData* udata)
{
    if (udata->client && udata->port) {
        if (!atomic_get(udata->shutdownReceived) && jack_port_is_mine(udata->client, udata->port)) {
            jack_port_unregister(udata->client, udata->port);
        }
        udata->port = NULL;
    }
    if (udata->prevNextPortUserData) {
        *udata->prevNextPortUserData = udata->nextPortUserData;
        if (udata->nextPortUserData) {
            udata->nextPortUserData->prevNextPortUserData = udata->prevNextPortUserData;
        }
    }
    udata->client = NULL;
}

static int LjackPort_release(lua_State* L)
{
    PortUserData* udata = luaL_checkudata(L, 1, LJACK_PORT_CLASS_NAME);
    ljack_port_release(L, udata);
    return 0;
}

/* ============================================================================================ */

static PortUserData* checkPortUdata(lua_State* L, int arg)
{
    PortUserData* udata = luaL_checkudata(L, arg, LJACK_PORT_CLASS_NAME);
    if (!udata->client) {
        luaL_error(L, LJACK_ERROR_INVALID_PORT);
        return NULL;
    }
    if (atomic_get(udata->shutdownReceived)) {
        ljack_handle_client_shutdown(L, udata->clientUserData);
        luaL_error(L, "error: jack client shutdown");
        return NULL;
    }
    return udata;
}


/* ============================================================================================ */

ljack_capi_port_err ljack_port_check_port_req(ClientUserData*      clientUdata,
                                              PortUserData*        udata,
                                              ljack_capi_reg_port* portReg)
{
    if (!udata || !udata->client) {
        return LJACK_CAPI_PORT_ERR_PORT_INVALID;
    }
    if (udata->clientUserData != clientUdata) {
        return LJACK_CAPI_PORT_ERR_CLIENT_MISMATCH;
    }
    if (!jack_port_is_mine(udata->client, udata->port)) {
        return LJACK_CAPI_PORT_ERR_PORT_NOT_MINE;
    }
    if (portReg->portDirection == LJACK_CAPI_PORT_IN && !(getPortFlags(udata) & JackPortIsInput)) {
        return LJACK_CAPI_PORT_ERR_IN_EXPECTED;
    }
    if (portReg->portDirection == LJACK_CAPI_PORT_OUT && !(getPortFlags(udata) & JackPortIsOutput)) {
        return LJACK_CAPI_PORT_ERR_OUT_EXPECTED;
    }
    if (portReg->portType == LJACK_CAPI_PORT_AUDIO && !isAudio(udata)) {
        return LJACK_CAPI_PORT_ERR_AUDIO_EXPECTED;
    }
    if (portReg->portType == LJACK_CAPI_PORT_MIDI && !isMidi(udata)) {
        return LJACK_CAPI_PORT_ERR_MIDI_EXPECTED;
    }
    return LJACK_CAPI_PORT_NO_ERROR;
}

/* ============================================================================================ */

bool ljack_is_port_udata(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA) {
        return false;
    }
    bool rslt = false;
    if (lua_getmetatable(L, index)) {                     /* -> meta1 */
        if (luaL_getmetatable(L, LJACK_PORT_CLASS_NAME)
                != LUA_TNIL)                              /* -> meta1, meta2 */
        {
            if (lua_rawequal(L, -1, -2)) {
                rslt = true;
            }
        }                                                 /* -> meta1, meta2 */
        lua_pop(L, 2);                                    /* -> */
    }                                                     /* -> */
    return rslt;
}

/* ============================================================================================ */

static int LjackPort_unregister(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    if (!jack_port_is_mine(udata->client, udata->port)) {
        return luaL_error(L, "not owning this port");
    }
    if (udata->procUsageCounter > 0) {
        return luaL_error(L, "port is used by processor");
    }
    int rc = jack_port_unregister(udata->client, udata->port);
    if (rc != 0) {
        return luaL_error(L, "cannot unregister port");
    }
    udata->port   = NULL;
    udata->client = NULL;

    ljack_port_release(L, udata);
    return 0;
}

/* ============================================================================================ */

static int LjackPort_toString(lua_State* L)
{
    PortUserData* udata = luaL_checkudata(L, 1, LJACK_PORT_CLASS_NAME);
    jack_port_t* port = udata->port;
    if (port) {
        ljack_util_quote_string(L, jack_port_name(port));                 /* -> quoted */
        lua_pushfstring(L, "%s: %p (name=%s)", LJACK_PORT_CLASS_NAME, 
                                               port, 
                                               lua_tostring(L, -1));      /* -> quoted, rslt */
    } else {
        lua_pushfstring(L, "%s: invalid", LJACK_PORT_CLASS_NAME);         /* -> rslt */
    }
    return 1;
}

/* ============================================================================================ */

static int LjackPort_name(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushstring(L, jack_port_name(udata->port));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_short_name(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushstring(L, jack_port_short_name(udata->port));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_client_prefix(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    const char* fullName  = jack_port_name(udata->port);
    const char* shortName = jack_port_short_name(udata->port);
    if (!fullName || !shortName) {
        return 0;
    }
    size_t flen = strlen(fullName);
    size_t slen = strlen(shortName);
    if (slen > flen) {
        return 0;
    }
    const char* ending = fullName + flen - slen;
    if (memcmp(ending, shortName, slen) != 0) {
        return 0;
    }
    lua_pushlstring(L, fullName, flen - slen);
    return 1;
}

/* ============================================================================================ */

static int LjackPort_get_client(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    return ljack_client_push_client_object(L, udata->clientUserData);
}

/* ============================================================================================ */

static int LjackPort_is_mine(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushboolean(L, jack_port_is_mine(udata->client, udata->port));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_is_input(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushboolean(L, getPortFlags(udata) & JackPortIsInput);
    return 1;
}

/* ============================================================================================ */

static int LjackPort_is_output(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushboolean(L, getPortFlags(udata) & JackPortIsOutput);
    return 1;
}

/* ============================================================================================ */

static int LjackPort_is_midi(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushboolean(L, isMidi(udata));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_is_audio(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);
    lua_pushboolean(L, isAudio(udata));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_get_connections(lua_State* L)
{
    PortUserData* udata = checkPortUdata(L, 1);

    luaL_checkstack(L, 10, NULL);
    lua_pushcfunction(L, ljack_util_push_string_list); /* -> func ----  may cause mem error in lua 5.1 */

    const char** ports = jack_port_get_all_connections(udata->client, udata->port);
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

const char* ljack_port_name_from_arg(lua_State* L, int arg)
{
    if (lua_type(L, arg) == LUA_TSTRING) {
        return lua_tostring(L, arg);
    } else {
        if (ljack_is_port_udata( L, arg)) {
            PortUserData* portUdata = lua_touserdata(L, arg);
            return jack_port_name(portUdata->port);
        } else {
            luaL_argerror(L, arg, "string or port object expected");
            return NULL;
        }
    }
}

/* ============================================================================================ */

jack_port_t* ljack_port_ptr_from_arg(lua_State* L, jack_client_t* client, int arg)
{
    if (lua_type(L, arg) == LUA_TSTRING) {
        const char* name = lua_tostring(L, arg);
        return jack_port_by_name(client, name);
    } else {
        if (ljack_is_port_udata( L, arg)) {
            PortUserData* portUdata = lua_touserdata(L, arg);
            return portUdata->port;
        } else {
            luaL_argerror(L, arg, "string or port object expected");
            return NULL;
        }
    }
}

/* ============================================================================================ */

static int LjackPort_connect(lua_State* L)
{
    int arg = 1;
    PortUserData* udata = checkPortUdata(L, arg++);
    const char*   other = ljack_port_name_from_arg(L, arg++);
    
    int flags = getPortFlags(udata);
    int rc;
    if (flags & JackPortIsOutput) {
        rc = jack_connect(udata->client, jack_port_name(udata->port), other);
    } else {
        rc = jack_connect(udata->client, other, jack_port_name(udata->port));
    }
    lua_pushboolean(L, (rc == 0 || rc == EEXIST));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_disconnect(lua_State* L)
{
    int arg = 1;
    PortUserData* udata = checkPortUdata(L, arg++);
    const char*   other = ljack_port_name_from_arg(L, arg++);
    
    int flags = getPortFlags(udata);
    int rc;
    if (flags & JackPortIsOutput) {
        rc = jack_disconnect(udata->client, jack_port_name(udata->port), other);
    } else {
        rc = jack_disconnect(udata->client, other, jack_port_name(udata->port));
    }
    lua_pushboolean(L, (rc == 0));
    return 1;
}

/* ============================================================================================ */

static int LjackPort_connected_to(lua_State* L)
{
    int arg = 1;
    PortUserData* udata = checkPortUdata(L, arg++);
    const char*   other = ljack_port_name_from_arg(L, arg++);
    
    bool rslt = udata->port && other && jack_port_connected_to(udata->port, other);
    lua_pushboolean(L, rslt);
    return 1;
}

/* ============================================================================================ */

static const luaL_Reg LjackPortMethods[] = 
{
    { "unregister",       LjackPort_unregister      },
    { "name",             LjackPort_name            },
    { "short_name",       LjackPort_short_name      },
    { "client_prefix",    LjackPort_client_prefix   },
    { "get_client",       LjackPort_get_client      },
    { "is_mine",          LjackPort_is_mine         },
    { "is_input",         LjackPort_is_input        },
    { "is_output",        LjackPort_is_output       },
    { "is_midi",          LjackPort_is_midi         },
    { "is_audio",         LjackPort_is_audio        },
    { "get_connections",  LjackPort_get_connections },
    { "connect",          LjackPort_connect         },
    { "disconnect",       LjackPort_disconnect      },
    { "connected_to",     LjackPort_connected_to    },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg LjackPortMetaMethods[] = 
{
    { "__tostring", LjackPort_toString },
    { "__gc",       LjackPort_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "port_name_size", LjackPort_name_size },
    { NULL,             NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupPortMeta(lua_State* L)
{                                                      /* -> meta */
    lua_pushstring(L, LJACK_PORT_CLASS_NAME);          /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                /* -> meta */

    luaL_setfuncs(L, LjackPortMetaMethods, 0);       /* -> meta */
    
    lua_newtable(L);                                   /* -> meta, PortClass */
    luaL_setfuncs(L, LjackPortMethods, 0);           /* -> meta, PortClass */
    lua_setfield (L, -2, "__index");                   /* -> meta */
    ljack_set_capi(L, -1, &ljack_capi_impl);
}


/* ============================================================================================ */

int ljack_port_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, LJACK_PORT_CLASS_NAME)) {
        setupPortMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */


