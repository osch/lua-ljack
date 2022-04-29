#ifndef LJACK_ERROR_H
#define LJACK_ERROR_H

#include "util.h"

typedef struct {
    char*  buffer;
    size_t len;
} error_handler_data;

void ljack_handle_error(error_handler_data* error_handler_data, const char* msg, size_t msglen);

#endif /* LJACK_ERROR_H */
