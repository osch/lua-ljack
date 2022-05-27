#ifndef LJACK_CLIENT_H
#define LJACK_CLIENT_H

#include "util.h"

typedef struct LjackClientUserData    LjackClientUserData;

extern const char* const LJACK_CLIENT_CLASS_NAME;

int ljack_client_init_module(lua_State* L, int module);

void ljack_client_check_is_valid(lua_State* L, LjackClientUserData* udata);

void ljack_client_handle_shutdown(LjackClientUserData* clientUserData);

int ljack_client_push_client_object(lua_State* L, LjackClientUserData* clientUserData);


#endif /* LJACK_CLIENT_H */
