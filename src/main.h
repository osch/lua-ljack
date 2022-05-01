#ifndef LJACK_MAIN_H
#define LJACK_MAIN_H

#include "util.h"

void ljack_log_error(const char* msg);
void ljack_log_info(const char* msg);

DLL_PUBLIC int luaopen_ljack(lua_State* L);

#endif /* LJACK_MAIN_H */
