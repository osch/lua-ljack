#ifndef LJACK_MIDI_SENDER_H
#define LJACK_MIDI_SENDER_H

#include "util.h"

extern const char* const LJACK_MIDI_SENDER_CLASS_NAME;

int ljack_midi_sender_init_module(lua_State* L, int module);

#endif // LJACK_MIDI_SENDER_H
