#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

#include "util.h"

#define AUPROC_CAPI_IMPLEMENT_SET_CAPI 1
#include "auproc_capi_impl.h"

#include "procbuf.h"

/* ============================================================================================ */

static const char* LJACK_ERROR_INVALID_PROCBUF = "invalid process_buffer";

const char* const LJACK_PROCBUF_CLASS_NAME = "ljack.process_buffer";

typedef struct LjackProcBufUserData   ProcBufUserData;
typedef struct LjackClientUserData    ClientUserData;

/* ============================================================================================ */

void ljack_procbuf_clear_midi_events(LjackProcBufUserData* udata)
{
    udata->midiEventCount  = 0;
    if (udata->ringBuffer) {
        udata->midiEventsBegin = (jack_midi_event_t*)udata->ringBuffer->buf;
        udata->midiDataBegin   = udata->ringBuffer->buf + udata->ringBuffer->size;
    } else {
        udata->midiEventsBegin = NULL;
        udata->midiDataBegin   = NULL;
    }
    udata->midiEventsEnd = udata->midiEventsBegin;
    udata->midiDataEnd   = udata->midiDataBegin;
}

/* ============================================================================================ */

uint32_t ljack_procbuf_get_midi_event_count(LjackProcBufUserData* udata)
{
    return udata->midiEventCount;
}

/* ============================================================================================ */

int ljack_procbuf_get_midi_event(jack_midi_event_t*  event,
                                 LjackProcBufUserData* udata,
                                 uint32_t            event_index)
{
    jack_midi_event_t* e = udata->midiEventsBegin + event_index;
    if (e < udata->midiEventsEnd) {
        *event = *e;
        return 0;
    } else {
        return ENODATA;
    }
}

/* ============================================================================================ */

jack_midi_data_t* ljack_procbuf_reserve_midi_event(LjackProcBufUserData*   udata,
                                                   jack_nframes_t          time,
                                                   size_t                  data_size)
{
    jack_midi_event_t* e = udata->midiEventsEnd;
    jack_midi_data_t* eBuf = udata->midiDataBegin - data_size;
    if ((unsigned char*)(e + 1) <= eBuf) {
        e->time = time;
        e->size = data_size;
        e->buffer = eBuf;
        udata->midiEventsEnd += 1;
        udata->midiDataBegin = eBuf;
        udata->midiEventCount += 1;
        return eBuf;
    } else {
        return NULL;
    }
}

/* ============================================================================================ */

static void setupProcBufMeta(lua_State* L);

static int pushProcBufMeta(lua_State* L)
{
    if (luaL_newmetatable(L, LJACK_PROCBUF_CLASS_NAME)) {
        setupProcBufMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

ProcBufUserData* ljack_procbuf_create(lua_State* L)
{
    ProcBufUserData* udata = lua_newuserdata(L, sizeof(ProcBufUserData));
    memset(udata, 0, sizeof(ProcBufUserData));        /* -> udata */
    udata->nameRef = LUA_NOREF;
    pushProcBufMeta(L);                               /* -> udata, meta */

    const char* procBufName = lua_pushfstring(L, "%s: %p", LJACK_PROCBUF_CLASS_NAME, udata);
                                                      /* -> udata, meta, name */
    udata->nameRef = luaL_ref(L, LUA_REGISTRYINDEX);  /* -> udata, meta */
    udata->procBufName = procBufName;                 /* -> udata, meta */
    udata->className = LJACK_PROCBUF_CLASS_NAME;      /* -> udata, meta */
    lua_setmetatable(L, -2);                          /* -> udata */
    return udata;
}

/* ============================================================================================ */

void ljack_procbuf_release(lua_State* L, ProcBufUserData* udata)
{
    if (udata->ringBuffer) {
        jack_ringbuffer_free(udata->ringBuffer);
        udata->ringBuffer = NULL;
    }
    if (udata->processMutex) {
        async_mutex_lock(udata->processMutex);
        {
            if (udata->prevNextProcBufUserData) {
                *udata->prevNextProcBufUserData = udata->nextProcBufUserData;
                if (udata->nextProcBufUserData) {
                    udata->nextProcBufUserData->prevNextProcBufUserData = udata->prevNextProcBufUserData;
                }
            }
        }
        async_mutex_unlock(udata->processMutex);
        udata->processMutex = NULL;
    }
    if (udata->nameRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, udata->nameRef);
        udata->nameRef = LUA_NOREF;
        udata->procBufName = NULL;
    }
    udata->jackClient = NULL;
}

static int LjackProcBuf_release(lua_State* L)
{
    ProcBufUserData* udata = luaL_checkudata(L, 1, LJACK_PROCBUF_CLASS_NAME);
    ljack_procbuf_release(L, udata);
    return 0;
}

/* ============================================================================================ */

static ProcBufUserData* checkProcBufUdata(lua_State* L, int arg)
{
    ProcBufUserData* udata = luaL_checkudata(L, arg, LJACK_PROCBUF_CLASS_NAME);
    ljack_client_check_is_valid(L, udata->clientUserData);
    if (!udata->jackClient) {
        luaL_error(L, LJACK_ERROR_INVALID_PROCBUF);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

bool ljack_is_procbuf_udata(lua_State* L, int index)
{
    if (lua_type(L, index) != LUA_TUSERDATA) {
        return false;
    }
    bool rslt = false;
    if (lua_getmetatable(L, index)) {                     /* -> meta1 */
        if (luaL_getmetatable(L, LJACK_PROCBUF_CLASS_NAME)
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

static int LjackProcBuf_id(lua_State* L)
{
    ProcBufUserData* udata = luaL_checkudata(L, 1, LJACK_PROCBUF_CLASS_NAME);
    lua_pushfstring(L, "%p", udata);
    return 1;
}

/* ============================================================================================ */

static int LjackProcBuf_toString(lua_State* L)
{
    ProcBufUserData* udata = luaL_checkudata(L, 1, LJACK_PROCBUF_CLASS_NAME);

    if (udata->nameRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, udata->nameRef);
    } else {
        lua_pushfstring(L, "%s: %p", LJACK_PROCBUF_CLASS_NAME, udata);
    }

    return 1;
}

/* ============================================================================================ */

static int LjackProcBuf_get_client(lua_State* L)
{
    ProcBufUserData* udata = checkProcBufUdata(L, 1);
    return ljack_client_push_client_object(L, udata->clientUserData);
}

/* ============================================================================================ */

static const luaL_Reg LjackProcBufMethods[] = 
{
    { "id",           LjackProcBuf_id          },
    { "get_client",   LjackProcBuf_get_client  },
    { NULL,           NULL } /* sentinel */
};

static const luaL_Reg LjackProcBufMetaMethods[] = 
{
    { "__tostring", LjackProcBuf_toString },
    { "__gc",       LjackProcBuf_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { NULL,             NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupProcBufMeta(lua_State* L)
{                                                       /* -> meta */
    lua_pushstring(L, LJACK_PROCBUF_CLASS_NAME); /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                 /* -> meta */

    luaL_setfuncs(L, LjackProcBufMetaMethods, 0); /* -> meta */
    
    lua_newtable(L);                                    /* -> meta, ProcBufClass */
    luaL_setfuncs(L, LjackProcBufMethods, 0);           /* -> meta, ProcBufClass */
    lua_setfield (L, -2, "__index");                    /* -> meta */
    auproc_set_capi(L, -1, &auproc_capi_impl);
}


/* ============================================================================================ */

int ljack_procbuf_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, LJACK_PROCBUF_CLASS_NAME)) {
        setupProcBufMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */


