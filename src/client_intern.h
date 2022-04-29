#ifndef LJACK_CLIENT_INTERN_H
#define LJACK_CLIENT_INTERN_H

typedef struct LjackClientUserData     LjackClientUserData;
typedef struct LjackPortUserData       LjackPortUserData;
typedef struct LjackProcReg            LjackProcReg;

struct LjackProcReg
{
    void* processorData;
    int  (*processCallback)(jack_nframes_t nframes, void* processorData);
    void (*clientClosedCallback)(lua_State* L, void* processorData);
    bool activated;
    int  portTableRef;
    int  portCount;
};

struct LjackClientUserData
{
    jack_client_t*       client;
    bool                 activated;
    AtomicCounter        shutdownReceived;
    
    const receiver_capi* receiver_capi;
    receiver_object*     receiver;
    receiver_writer*     receiver_writer;
    
    int                  weakTableRef;
    int                  strongTableRef;
    
    LjackPortUserData*       firstPortUserData;
    
    LjackProcReg*            procRegList;
    int                      procRegCount;
    
    LjackProcReg*            activatedProcRegList;
    
    Mutex                processMutex;
    bool                 closed;
};

void ljack_client_intern_register_callbacks(LjackClientUserData* udata);

void ljack_client_intern_activate_proc_list(LjackClientUserData* udata);

void ljack_client_intern_release_proc_reg(lua_State* L, LjackProcReg* reg);

#endif /* LJACK_CLIENT_INTERN_H */
