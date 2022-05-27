#ifndef LJACK_MAIN_H
#define LJACK_MAIN_H

#include "util.h"

bool ljack_log_errorV(const char* fmt, va_list args);
bool ljack_log_infoV(const char* fmt, va_list args);

void ljack_log_error(const char* fmt, ...);
void ljack_log_info(const char* fmt, ...);

DLL_PUBLIC int luaopen_ljack(lua_State* L);

#endif /* LJACK_MAIN_H */
