#include <jack/jack.h>

#include "util.h"
#include "ljack_capi_impl.h"

#include "receiver_capi.h"

#include "port.h"
#include "client_intern.h"

#include "main.h"

/* ============================================================================================ */

typedef LjackPortUserData   PortUserData;
typedef LjackClientUserData ClientUserData;

/* ============================================================================================ */

static ljack_capi_client* getLjackClient(lua_State* L, int index)
{
    if (   lua_type(L, index) == LUA_TUSERDATA
        && lua_getmetatable(L, index))
    {                                                     /* -> meta1 */
        bool isPort   = false;
        bool isClient = false;
        if (luaL_getmetatable(L, LJACK_PORT_CLASS_NAME)
                != LUA_TNIL)                              /* -> meta1, meta2 */
        {
            if (lua_rawequal(L, -1, -2)) {
                isPort = true;
            }
            lua_pop(L, 1);                                /* -> meta1 */
        }
        if (!isPort && 
            luaL_getmetatable(L, LJACK_CLIENT_CLASS_NAME)
                != LUA_TNIL)                              /* -> meta1, meta2 */
        {
            if (lua_rawequal(L, -1, -2)) {
                isClient = true;
            }
            lua_pop(L, 1);                                /* -> meta1 */
        }
        lua_pop(L, 1);                                    /* -> */
        ClientUserData* rslt = NULL;
        if (isPort) {
            PortUserData* udata = lua_touserdata(L, index);
            rslt = udata->clientUserData;
        }
        else if (isClient) {
            ClientUserData* udata = lua_touserdata(L, index);
            rslt = udata;
        }
        ljack_client_check_shutdown(L, rslt);
        return (ljack_capi_client*) rslt;
    } else {                                              /* -> */
        return NULL;
    }
}

/* ============================================================================================ */

jack_client_t* registerProcessor(lua_State* L, 
                                 int firstPortStackIndex, int portCount,
                                 ljack_capi_client* client, 
                                 void* processorData,
                                 int  (*processCallback)(jack_nframes_t nframes, void* processorData),
                                 void (*clientClosedCallback)(lua_State* L, void* processorData),
                                 ljack_capi_reg_port* portRegList,
                                 ljack_capi_reg_err*  portError)
{
    ClientUserData* clientUdata = (ClientUserData*) client;
    
    if (!processorData || !processCallback) {
        if (portError) {
            portError->portError = LJACK_CAPI_PORT_ERR_ARG_INVALID;
            portError->portIndex = -1;
        }
        return NULL;
    }
    
    for (int i = 0; i < portCount; ++i) {
        if (!ljack_is_port_udata(L, firstPortStackIndex + i)) {
            if (portError) {
                portError->portError = LJACK_CAPI_PORT_ERR_PORT_INVALID;
                portError->portIndex = i;
            }
            return NULL;
        }
        ljack_capi_reg_port* portReg = portRegList + i;
        ljack_capi_port_err err = ljack_port_check_port_req(clientUdata,
                                                            lua_touserdata(L, firstPortStackIndex + i),
                                                            portReg);
        if (err != LJACK_CAPI_PORT_NO_ERROR) {
            if (portError) {
                portError->portError = err;
                portError->portIndex = i;
            }
            return NULL;
        }
    }
    
    lua_newtable(L);                                     /* -> portTable */
    for (int i = 0; i < portCount; ++i) {
        lua_pushvalue(L, firstPortStackIndex + i);       /* -> portTable, port */
        lua_rawseti(L, -2, i + 1);                       /* -> portTable */
    }
    int portTableRef = luaL_ref(L, LUA_REGISTRYINDEX);   /* -> */
    
    LjackProcReg* oldList = clientUdata->procRegList;
    int n = clientUdata->procRegCount;
    LjackProcReg* newList = malloc(sizeof(LjackProcReg) * (n + 2));
    if (!newList) {
        luaL_unref(L, LUA_REGISTRYINDEX, portTableRef);
        luaL_error(L, "out of memory");
        return NULL;
    }
    if (oldList) {
        memcpy(newList, oldList, sizeof(LjackProcReg) * n);
    }
    LjackProcReg* newReg = newList + n;
    memset(newReg, 0, 2 * sizeof(LjackProcReg));

    newReg->processorData        = processorData;
    newReg->processCallback      = processCallback;
    newReg->clientClosedCallback = clientClosedCallback;
    newReg->portTableRef         = LUA_REFNIL;
    newReg->portTableRef         = portTableRef;
    newReg->portCount            = portCount;
    
    clientUdata->procRegList  = newList;
    clientUdata->procRegCount = n + 1;

    ljack_client_intern_activate_proc_list(clientUdata);
    
    if (oldList) {
        free(oldList);
    }
    
    for (int i = 0; i < portCount; ++i) {
        PortUserData* portUdata = lua_touserdata(L, firstPortStackIndex + i);
        portUdata->procUsageCounter += 1;
        portRegList[i].jackPort = portUdata->port;
    }
    return clientUdata->client;
}

/* ============================================================================================ */

static void logError(ljack_capi_client* client, const char* msg)
{
    ljack_log_error(msg);
}

/* ============================================================================================ */

static void logInfo(ljack_capi_client* client, const char* msg)
{
    ljack_log_info(msg);
}


/* ============================================================================================ */

static int unregisterProcessor(lua_State* L,
                               ljack_capi_client* client,
                               void* processorData)
{
    ClientUserData* clientUdata = (ClientUserData*) client;

    int           n       = clientUdata->procRegCount;
    LjackProcReg* oldList = clientUdata->procRegList;
    
    if (!oldList || n <= 0) {
        return 0;
    }
    int index = -1;
    for (int i = 0; i < n; ++i) {
        if (oldList[i].processorData == processorData) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return 0;
    }
    
    LjackProcReg* newList = malloc(sizeof(LjackProcReg) * (n - 1 + 1));
    
    if (!newList) {
        luaL_error(L, "out of memory");
        return 0;
    }
    if (index > 0) {
        memcpy(newList, oldList, sizeof(LjackProcReg) * index);
    }
    if (index + 1 < n) {
        memcpy(newList + index, oldList + index + 1, sizeof(LjackProcReg) * (n - (index + 1)));
    }
    memset(newList + n - 1, 0, sizeof(LjackProcReg));
    
    clientUdata->procRegList  = newList;
    clientUdata->procRegCount = n - 1;

    ljack_client_intern_activate_proc_list(clientUdata);
    
    ljack_client_intern_release_proc_reg(L, oldList + index);
    
    if (oldList) {
        free(oldList);
    }

    return 1;
}

/* ============================================================================================ */

static void activateProcessor(lua_State* L,
                              ljack_capi_client* client,
                              void* processorData)
{
    ClientUserData* clientUdata = (ClientUserData*) client;

    for (int i = 0; i < clientUdata->procRegCount; ++i) {
        LjackProcReg* reg = clientUdata->procRegList + i;
        if (reg->processorData == processorData) {
            reg->activated = true;
            break;
        }
    }
}

/* ============================================================================================ */

static void deactivateProcessor(lua_State* L,
                                ljack_capi_client* client,
                                void* processorData)
{
    ClientUserData* clientUdata = (ClientUserData*) client;

    for (int i = 0; i < clientUdata->procRegCount; ++i) {
        LjackProcReg* reg = clientUdata->procRegList + i;
        if (reg->processorData == processorData) {
            reg->activated = false;
            break;
        }
    }
}

/* ============================================================================================ */

const ljack_capi ljack_capi_impl = 
{
    LJACK_CAPI_VERSION_MAJOR,
    LJACK_CAPI_VERSION_MINOR,
    LJACK_CAPI_VERSION_PATCH,
    
    NULL, /* next_capi */
    
    getLjackClient,
    logError,
    logInfo,
    registerProcessor,
    unregisterProcessor,
    activateProcessor,
    deactivateProcessor
};

/* ============================================================================================ */
