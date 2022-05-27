#ifndef LJACK_CLIENT_INTERN_H
#define LJACK_CLIENT_INTERN_H

typedef struct LjackClientUserData   LjackClientUserData;
typedef struct LjackPortUserData     LjackPortUserData;
typedef struct LjackProcReg          LjackProcReg;
typedef struct LjackProcBufUserData  LjackProcBufUserData;
typedef struct LjackConnectorInfo    LjackConnectorInfo;

struct LjackConnectorInfo
{
    bool isPort;
    bool isProcBuf;
    bool isInput;
    bool isOutput;
    
    LjackPortUserData*    portUdata;
    LjackProcBufUserData* procBufUdata;
};

struct LjackProcReg
{
    void* processorData;
    int  (*processCallback)(jack_nframes_t nframes, void* processorData);
    int  (*sampleRateCallback)(jack_nframes_t nframes, void* processorData);
    int  (*bufferSizeCallback)(jack_nframes_t nframes, void* processorData);
    void (*engineClosedCallback)(void* processorData);
    void (*engineReleasedCallback)(void* processorData);      
    char* processorName;
    jack_nframes_t bufferSize;
    jack_nframes_t sampleRate;
    bool activated;
    bool outBuffersCleared;
    int  connectorTableRef;
    int  connectorCount;
    LjackConnectorInfo* connectorInfos;
};

struct LjackClientUserData
{
    const char*          className;
    jack_client_t*       client;
    bool                 activated;
    AtomicCounter        shutdownReceived;
    AtomicCounter        severeProcessingError;
    
    const receiver_capi* receiver_capi;
    receiver_object*     receiver;
    receiver_writer*     receiver_writer;
    
    int                  weakTableRef;
    int                  strongTableRef;
    
    LjackPortUserData*     firstPortUserData;
    LjackProcBufUserData*  firstProcBufUserData;
    
    LjackProcReg**         procRegList;
    int                    procRegCount;
    LjackProcReg**         activeProcRegList;
    LjackProcReg**         confirmedProcRegList;
    
    Mutex                processMutex;
    bool                 closed;
    jack_nframes_t       bufferSize;
    jack_nframes_t       sampleRate;
    size_t               audioBufferSize;
    size_t               midiBufferSize;
};

void ljack_client_intern_register_callbacks(LjackClientUserData* udata);

void ljack_client_intern_activate_proc_list_LOCKED(LjackClientUserData* udata,
                                                   LjackProcReg**       newList);

void ljack_client_intern_release_proc_reg(lua_State* L, LjackProcReg* reg);

void ljack_client_intern_get_connector(lua_State* L, int arg, 
                                       LjackPortUserData** portUdata, 
                                       LjackProcBufUserData** procBufUdata);

#endif /* LJACK_CLIENT_INTERN_H */
