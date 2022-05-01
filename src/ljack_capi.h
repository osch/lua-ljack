#ifndef LJACK_CAPI_H
#define LJACK_CAPI_H

#define LJACK_CAPI_ID_STRING     "_capi_ljack"

#define LJACK_CAPI_VERSION_MAJOR -2
#define LJACK_CAPI_VERSION_MINOR  1
#define LJACK_CAPI_VERSION_PATCH  0

typedef struct ljack_capi           ljack_capi;
typedef struct ljack_capi_client    ljack_capi_client;
typedef struct ljack_capi_reg_port  ljack_capi_reg_port;
typedef struct ljack_capi_reg_err   ljack_capi_reg_err;
typedef enum   ljack_capi_port_err  ljack_capi_port_err;
typedef enum   ljack_capi_port_dir  ljack_capi_port_dir;
typedef enum   ljack_capi_port_type ljack_capi_port_type;

#ifndef LJACK_CAPI_IMPLEMENT_SET_CAPI
#  define LJACK_CAPI_IMPLEMENT_SET_CAPI 0
#endif

#ifndef LJACK_CAPI_IMPLEMENT_GET_CAPI
#  define LJACK_CAPI_IMPLEMENT_GET_CAPI 0
#endif

enum ljack_capi_port_dir
{
    LJACK_CAPI_PORT_IN,
    LJACK_CAPI_PORT_OUT
};

enum ljack_capi_port_type
{
    LJACK_CAPI_PORT_AUDIO,
    LJACK_CAPI_PORT_MIDI
};

enum ljack_capi_port_err {
    LJACK_CAPI_PORT_NO_ERROR,
    LJACK_CAPI_PORT_ERR_ARG_INVALID,
    LJACK_CAPI_PORT_ERR_PORT_INVALID,
    LJACK_CAPI_PORT_ERR_CLIENT_MISMATCH,
    LJACK_CAPI_PORT_ERR_PORT_NOT_MINE,
    LJACK_CAPI_PORT_ERR_AUDIO_EXPECTED,
    LJACK_CAPI_PORT_ERR_MIDI_EXPECTED,
    LJACK_CAPI_PORT_ERR_IN_EXPECTED,
    LJACK_CAPI_PORT_ERR_OUT_EXPECTED,
};

struct ljack_capi_reg_port
{
    ljack_capi_port_dir  portDirection;
    ljack_capi_port_type portType;
    jack_port_t*         jackPort;
};

struct ljack_capi_reg_err
{
    ljack_capi_port_err portError;
    int                 portIndex;
};

/**
 *  Receiver C API.
 */
struct ljack_capi
{
    int version_major;
    int version_minor;
    int version_patch;
    
    /**
     * May point to another (incompatible) version of this API implementation.
     * NULL if no such implementation exists.
     *
     * The usage of next_capi makes it possible to implement two or more
     * incompatible versions of the C API.
     *
     * An API is compatible to another API if both have the same major 
     * version number and if the minor version number of the first API is
     * greater or equal than the second one's.
     */
    void* next_capi;
    
    /**
     * Must return a valid pointer if the object at the given stack
     * index can give a valid ljack client object, otherwise must 
     * return NULL. Objects of type ljack.client or ljack.port can
     * be used to obtain a ljack_capi_client pointer.
     */
    ljack_capi_client* (*getLjackClient)(lua_State* L, int index);

    /**
     * Log errror message. May be called from any thread, also in the 
     * processCallback.
     * Should only be called for severe errors. Default logs to stderr.
     *
     * client  - may be NULL is message is not associated to a specific 
     *           client.
     */                               
    void (*logError)(ljack_capi_client* client,
                     const char* msg);
    
    /**
     * Log info message. May be called from any thread, also in the 
     * processCallback. Default is to discard these messages.
     *
     * client  - may be NULL is message is not associated to a specific 
     *           client.
     */                               
    void (*logInfo)(ljack_capi_client* client,
                    const char* msg);
    
    /**
     * Registers native procesor object to the Ljack client. Returns a pointer
     * to the native jack client on success. Returns NULL on failure.
     * This method may also raise a lua error.
     *
     * firstPortStackIndex - stack index of the first lua port object
     * portCount           - number of lua port objects on the stack
     * client              - Ljack client obtained by method getLjackClient (s.a.)
     * processorData       - pointer to data that is reached into processor callbacks
     * processCallback     - realtime process callback 
     * clientClosed        - called if the Ljack client is closed.
     * portRegList         - list with registration data for portCount lua port objects
     *                       on the stack at firstPortStackIndex. The members portDirection
     *                       and portType must match the corresponding lua port object on
     *                       stack given at firstPortStackIndex.
     *                       After successful registration, the member jackPort contains the 
     *                       pointer to the native jack port.
     * portError           - this method returns NULL on failure and gives additional error 
     *                       information in portError if portError != NULL. Member portIndex
     *                       contains the index offset of the port, i.e. if there is an error with
     *                       the first port at lua stack index firstPortStackIndex the member 
     *                       portIndex has value 0.
     */
    jack_client_t* (*registerProcessor)(lua_State* L, 
                                        int firstPortStackIndex, int portCount,
                                        ljack_capi_client* client, 
                                        void* processorData,
                                        int  (*processCallback)(jack_nframes_t nframes, void* processorData),
                                        void (*clientClosed)(lua_State* L, void* processorData),
                                        ljack_capi_reg_port* portRegList,
                                        ljack_capi_reg_err*  portError);

    int (*unregisterProcessor)(lua_State* L,
                               ljack_capi_client* client,
                               void* processorData);

    /**
     * Activates the processor. After this point the processCallback is called for realtime
     * processing.
     */
    void (*activateProcessor)(lua_State* L,
                              ljack_capi_client* client,
                              void* processorData);

    /**
     * Deactivates the processor. After this point the processCallback is not called.
     */
    void (*deactivateProcessor)(lua_State* L,
                                ljack_capi_client* client,
                                void* processorData);
};


#if LJACK_CAPI_IMPLEMENT_SET_CAPI
/**
 * Sets the Receiver C API into the metatable at the given index.
 * 
 * index: index of the table that is be used as metatable for objects 
 *        that are associated to the given capi.
 */
static int ljack_set_capi(lua_State* L, int index, const ljack_capi* capi)
{
    lua_pushlstring(L, LJACK_CAPI_ID_STRING, strlen(LJACK_CAPI_ID_STRING));           /* -> key */
    void** udata = lua_newuserdata(L, sizeof(void*) + strlen(LJACK_CAPI_ID_STRING) + 1); /* -> key, value */
    *udata = (void*)capi;
    strcpy((char*)(udata + 1), LJACK_CAPI_ID_STRING);  /* -> key, value */
    lua_rawset(L, (index < 0) ? (index - 2) : index);     /* -> */
    return 0;
}
#endif /* LJACK_CAPI_IMPLEMENT_SET_CAPI */

#if LJACK_CAPI_IMPLEMENT_GET_CAPI
/**
 * Gives the associated Receiver C API for the object at the given stack index.
 */
static const ljack_capi* ljack_get_capi(lua_State* L, int index, int* versionError)
{
    if (luaL_getmetafield(L, index, LJACK_CAPI_ID_STRING) == LUA_TUSERDATA) /* -> _capi */
    {
        void** udata = lua_touserdata(L, -1);                                  /* -> _capi */

        if (   (lua_rawlen(L, -1) >= sizeof(void*) + strlen(LJACK_CAPI_ID_STRING) + 1)
            && (memcmp((char*)(udata + 1), LJACK_CAPI_ID_STRING, 
                       strlen(LJACK_CAPI_ID_STRING) + 1) == 0))
        {
            const ljack_capi* capi = *udata;                                /* -> _capi */
            while (capi) {
                if (   capi->version_major == LJACK_CAPI_VERSION_MAJOR
                    && capi->version_minor >= LJACK_CAPI_VERSION_MINOR)
                {                                                              /* -> _capi */
                    lua_pop(L, 1);                                             /* -> */
                    return capi;
                }
                capi = capi->next_capi;
            }
            if (versionError) {
                *versionError = 1;
            }
        }                                                                 /* -> _capi */
        lua_pop(L, 1);                                                    /* -> */
    }                                                                     /* -> */
    return NULL;
}
#endif /* LJACK_CAPI_IMPLEMENT_GET_CAPI */

#endif /* LJACK_CAPI_H */
