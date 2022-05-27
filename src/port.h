#ifndef LJACK_PORT_H
#define LJACK_PORT_H

#include <jack/jack.h>

#include "util.h"
#include "client.h"
#include "auproc_capi.h"

extern const char* const LJACK_PORT_CLASS_NAME;

struct LjackClientUserData;

typedef struct LjackPortUserData
{
    const char*          className;
    jack_client_t*       client;
    jack_port_t*         port;

    bool                 isInput;
    bool                 isOutput;

    bool                 isMidi;
    bool                 isAudio;
    
    int              procUsageCounter;
    AtomicCounter*   shutdownReceived;
    
    struct LjackClientUserData* clientUserData;
    struct LjackPortUserData**  prevNextPortUserData;
    struct LjackPortUserData*   nextPortUserData;
    
} LjackPortUserData;

typedef enum {
    MIDI  = 0,
    AUDIO = 1,
} LjackPortType;

typedef enum {
    IN  = 0,
    OUT = 1
} LjackPortDirection;

LjackPortUserData* ljack_port_register(lua_State* L, jack_client_t* client, const char* name, LjackPortType type, LjackPortDirection dir);

LjackPortUserData* ljack_port_create(lua_State* L, jack_client_t* client, jack_port_t* port);

void ljack_port_release(lua_State* L, LjackPortUserData* udata);

int ljack_port_init_module(lua_State* L, int module);

bool ljack_is_port_udata(lua_State* L, int index);

const char* ljack_port_name_from_arg(lua_State* L, int arg);

jack_port_t* ljack_port_ptr_from_arg(lua_State* L, jack_client_t* client, int arg);


#endif /* LJACK_PORT_H */