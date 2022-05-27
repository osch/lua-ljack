#ifndef LJACK_PROCBUF_H
#define LJACK_PROCBUF_H

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

#include "util.h"
#include "client.h"
#include "auproc_capi.h"

extern const char* const LJACK_PROCBUF_CLASS_NAME;

struct LjackClientUserData;

/* ============================================================================================ */

typedef struct LjackProcBufUserData
{
    const char*        className;
    jack_client_t*     jackClient;

    const char*        procBufName;
    int                nameRef;
    
    jack_nframes_t     bufferSize;
    jack_ringbuffer_t* ringBuffer;
    
    uint32_t           midiEventCount;
    jack_midi_event_t* midiEventsBegin;
    jack_midi_event_t* midiEventsEnd;
    jack_midi_data_t*  midiDataBegin;
    jack_midi_data_t*  midiDataEnd;
    
    bool               isMidi;
    bool               isAudio;

    int              procUsageCounter;
    
    int              inpUsageCounter;
    int              outUsageCounter;
    
    int              inpActiveCounter;
    int              outActiveCounter;
    
    AtomicCounter*   shutdownReceived;
    Mutex*           processMutex;

    struct LjackClientUserData* clientUserData;
    
    struct LjackProcBufUserData**  prevNextProcBufUserData;
    struct LjackProcBufUserData*   nextProcBufUserData;
    
} LjackProcBufUserData;

/* ============================================================================================ */

LjackProcBufUserData* ljack_procbuf_create(lua_State* L);

void ljack_procbuf_release(lua_State* L, LjackProcBufUserData* udata);

int ljack_procbuf_init_module(lua_State* L, int module);

/* ============================================================================================ */

void ljack_procbuf_clear_midi_events(LjackProcBufUserData* udata);

/* ============================================================================================ */

uint32_t ljack_procbuf_get_midi_event_count(LjackProcBufUserData* udata);

/* ============================================================================================ */

int ljack_procbuf_get_midi_event(jack_midi_event_t*  event,
                                 LjackProcBufUserData* udata,
                                 uint32_t            event_index);
                                 
/* ============================================================================================ */

jack_midi_data_t* ljack_procbuf_reserve_midi_event(LjackProcBufUserData*   udata,
                                                   jack_nframes_t          time,
                                                   size_t                  data_size);

/* ============================================================================================ */


#endif /* LJACK_PROCBUF_H */
