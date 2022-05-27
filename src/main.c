#include <jack/jack.h>

#define RECEIVER_CAPI_IMPLEMENT_GET_CAPI 1

#define AUPROC_CAPI_IMPLEMENT_SET_CAPI 1

#include "main.h"
#include "client.h"
#include "port.h"
#include "procbuf.h"
#include "midi_receiver.h"
#include "midi_sender.h"
#include "audio_mixer.h"
#include "midi_mixer.h"
#include "audio_sender.h"
#include "receiver_capi.h"
#include "error.h"
#include "auproc_capi_impl.h"



#ifndef LJACK_VERSION
    #error LJACK_VERSION is not defined
#endif 

#define LJACK_STRINGIFY(x) #x
#define LJACK_TOSTRING(x) LJACK_STRINGIFY(x)
#define LJACK_VERSION_STRING LJACK_TOSTRING(LJACK_VERSION)

const char* const LJACK_MODULE_NAME = "ljack";

/* ============================================================================================ */

static void logSilent(const char* msg) 
{}
static void logStdOut(const char* msg) {
    fprintf(stdout, "%s\n", msg);
}
static void logStdErr(const char* msg) {
    fprintf(stderr, "%s\n", msg);
}

typedef void (*LogFunc)(const char* msg);

static const char *const builtin_log_options[] = {
    "SILENT",
    "STDOUT",
    "STDERR"
};
static LogFunc builtin_log_functions[] = {
    logSilent,
    logStdOut,
    logStdErr
};

/* ============================================================================================ */

#define LJACK_LOG_BUFFER_SIZE 2048

typedef struct {
    bool                 isError;
    Lock                 lock;
    AtomicCounter        initStage;
    LogFunc              logFunc;
    const receiver_capi* capi;
    receiver_object*     receiver;
    receiver_writer*     writer;
    char                 logBuffer[LJACK_LOG_BUFFER_SIZE];
} LogSetting;


static LogSetting errorLog = { true,  0 };
static LogSetting infoLog  = { false, 0 };

static void assureMutexInitialized(LogSetting* s)
{
    if (atomic_get(&s->initStage) != 2) {
        if (atomic_set_if_equal(&s->initStage, 0, 1)) {
            async_lock_init(&s->lock);
            if (s->isError) {
                s->logFunc = logStdErr;
            }
            atomic_set(&s->initStage, 2);
        } 
        else {
            while (atomic_get(&s->initStage) != 2) {
                Mutex waitMutex;
                async_mutex_init(&waitMutex);
                async_mutex_lock(&waitMutex);
                async_mutex_wait_millis(&waitMutex, 1);
                async_mutex_destruct(&waitMutex);
            }
        }
    }

}

/* ============================================================================================ */

static void handleLogError(void* ehdata, const char* msg, size_t msglen)
{
    ljack_handle_error((error_handler_data*)ehdata, msg, msglen);
}


static void logToReceiver_LOCKED(LogSetting* s, const char* msg)
{
    if (s->receiver) {
        s->capi->addStringToWriter(s->writer, msg, strlen(msg));
        error_handler_data ehdata = {0};
        int rc = s->capi->msgToReceiver(s->receiver, s->writer, false, false, handleLogError, &ehdata);
        if (rc == 1) {
            // receiver closed
            s->capi->freeWriter(s->writer);
            s->capi->releaseReceiver(s->receiver);
            s->writer = NULL;
            s->receiver = NULL;
            s->logFunc  = NULL;
            if (s->isError) {
                jack_set_error_function(logSilent);
            } else {
                jack_set_info_function(logSilent);
            }
        }
        else if (rc != 0) {
            // other error
            s->capi->clearWriter(s->writer);
        }
        if (ehdata.buffer) {
            fprintf(stderr, "Error while calling ljack %s log callback: %s\n", 
                            s->isError ? "error" : "info", ehdata.buffer);
            free(ehdata.buffer);
        }
    } else if (s->logFunc) {
        s->logFunc(msg);
    }
}

static void logToReceiver1(LogSetting* s, const char* msg)
{
    async_lock_acquire(&s->lock);
    logToReceiver_LOCKED(s, msg);
    async_lock_release(&s->lock);
}

static bool logToReceiver2(LogSetting* s, const char* fmt, va_list args)
{
    async_lock_acquire(&s->lock);
    int size = vsnprintf(s->logBuffer, LJACK_LOG_BUFFER_SIZE, fmt, args);
    if (size >= LJACK_LOG_BUFFER_SIZE) {
        strcpy(s->logBuffer + LJACK_LOG_BUFFER_SIZE - 4, "...");
    }
    logToReceiver_LOCKED(s, s->logBuffer);
    async_lock_release(&s->lock);
    return true;
}

/* ============================================================================================ */

static void errorCallback(const char* msg)
{
    logToReceiver1(&errorLog, msg);
}

static void infoCallback(const char* msg)
{
    logToReceiver1(&infoLog, msg);
}


/* ============================================================================================ */

static int setLogFunction(lua_State* L, LogSetting* s)
{
    const receiver_capi* api = NULL;
    receiver_object*     rcv = NULL;
    LogFunc              logFunc = NULL;
    int arg = 1;
    if (lua_type(L, arg) == LUA_TSTRING) {
        int opt = luaL_checkoption(L, arg, NULL, builtin_log_options);
        logFunc = builtin_log_functions[opt];
    }
    else {
        int versErr = 0;
        api = receiver_get_capi(L, arg, &versErr);
        if (!api) {
            if (versErr) {
                return luaL_argerror(L, arg, "receiver api version mismatch");
            } else {
                return luaL_argerror(L, arg, "expected string or receiver object");
            }
        }
        rcv = api->toReceiver(L, arg);
        if (!rcv) {
            return luaL_argerror(L, arg, "invalid receiver object");
        }
    }
    receiver_writer* wrt = NULL;
    if (rcv) {
        wrt = api->newWriter(1024, 2);
        if (!wrt) {
            return luaL_error(L, "cannot create writer to log receiver");
        }
        api->retainReceiver(rcv);
        logFunc = s->isError ? errorCallback : infoCallback;
    }
    async_lock_acquire(&s->lock);
        if (s->receiver) {
            s->capi->releaseReceiver(s->receiver);
            s->capi->freeWriter(s->writer);
        }
        s->logFunc  = logFunc;
        s->capi     = api;
        s->receiver = rcv;
        s->writer   = wrt;
        if (s->isError) {
            jack_set_error_function(logFunc);
        } else {
            jack_set_info_function(logFunc);
        }
    async_lock_release(&s->lock);
    return 0;
}


/* ============================================================================================ */

static int Ljack_set_error_log(lua_State* L)
{
    assureMutexInitialized(&errorLog);
    return setLogFunction(L, &errorLog);
}

/* ============================================================================================ */

static int Ljack_set_info_log(lua_State* L)
{
    assureMutexInitialized(&infoLog);
    return setLogFunction(L, &infoLog);
}

/* ============================================================================================ */

bool ljack_log_errorV(const char* fmt, va_list args)
{
    assureMutexInitialized(&errorLog);
    return logToReceiver2(&errorLog, fmt, args);
}

void ljack_log_error(const char* fmt, ...)
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

bool ljack_log_infoV(const char* fmt, va_list args)
{
    assureMutexInitialized(&infoLog);
    return logToReceiver2(&infoLog, fmt, args);
}

void ljack_log_info(const char* fmt, ...)
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

static int Ljack_log_error(lua_State* L)
{
    luaL_checkstring(L, 1);
    ljack_log_error(lua_tostring(L, 1));
    return 0;
}

/* ============================================================================================ */

static int Ljack_log_info(lua_State* L)
{
    luaL_checkstring(L, 1);
    ljack_log_info(lua_tostring(L, 1));
    return 0;
}

/* ============================================================================================ */

static int Ljack_threadid(lua_State* L)
{
    lua_pushfstring(L, "%p", (void*)async_current_threadid());
    return 1;
}

/* ============================================================================================ */

static int Ljack_type(lua_State* L)
{
    luaL_checkany(L, 1);
    int tp = lua_type(L, 1);
    if (tp == LUA_TUSERDATA) {
        if (lua_getmetatable(L, 1)) {
            if (lua_getfield(L, -1, "__name") == LUA_TSTRING) {
                lua_pushvalue(L, -1);
                if (lua_gettable(L, LUA_REGISTRYINDEX) == LUA_TTABLE) {
                    if (lua_rawequal(L, -3, -1)) {
                        lua_pop(L, 1);
                        return 1;
                    }
                }
            }
        }
    }
    lua_pushstring(L, lua_typename(L, tp));
    return 1;
}

/* ============================================================================================ */

static const luaL_Reg ModuleFunctions[] = 
{
    { "set_error_log",  Ljack_set_error_log  },
    { "set_info_log",   Ljack_set_info_log   },
    { "log_error",      Ljack_log_error      },
    { "log_info",       Ljack_log_info       },
    { "threadid",       Ljack_threadid       },
    { "type",           Ljack_type           },
    { NULL,             NULL } /* sentinel */
};

/* ============================================================================================ */

DLL_PUBLIC int luaopen_ljack(lua_State* L)
{
    luaL_checkversion(L); /* does nothing if compiled for Lua 5.1 */

    /* ---------------------------------------- */

    int n = lua_gettop(L);
    
    int module      = ++n; lua_newtable(L);
    int errorModule = ++n; lua_newtable(L);

    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);
    
    lua_pushliteral(L, LJACK_VERSION_STRING);
    lua_setfield(L, module, "_VERSION");
    
    lua_checkstack(L, LUA_MINSTACK);
    
    ljack_client_init_module        (L, module);
    ljack_port_init_module          (L, module);
    ljack_procbuf_init_module       (L, module);

    auproc_midi_receiver_init_module(L, module);
    auproc_midi_sender_init_module  (L, module);
    auproc_audio_mixer_init_module  (L, module);
    auproc_midi_mixer_init_module   (L, module);
    auproc_audio_sender_init_module (L, module);
    
    lua_newtable(L);                                   /* -> meta */
    lua_pushstring(L, "ljack");                        /* -> meta, "ljack" */
    lua_setfield(L, -2, "__metatable");                /* -> meta */
    lua_pushstring(L, "ljack");                        /* -> meta, "ljack" */
    lua_setfield(L, -2, "__name");                     /* -> meta */
    auproc_set_capi(L, -1, &auproc_capi_impl);         /* -> meta */
    lua_setmetatable(L, module);                       /* -> */
    
    lua_settop(L, module);
    return 1;
}

/* ============================================================================================ */
