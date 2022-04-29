#ifndef LJACK_MIDI_RECEIVER_H
#define LJACK_MIDI_RECEIVER_H

#include "util.h"

extern const char* const LJACK_MIDI_RECEIVER_CLASS_NAME;

int ljack_midi_receiver_init_module(lua_State* L, int module);

#endif // LJACK_MIDI_RECEIVER_H
