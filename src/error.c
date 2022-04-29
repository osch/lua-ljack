#include "error.h"

void ljack_handle_error(error_handler_data* e, const char* msg, size_t msglen)
{
    if (!e->buffer) {
        e->buffer = malloc(msglen + 1);
        if (e->buffer) {
            memcpy(e->buffer, msg, msglen);
            e->buffer[msglen] = '\0';
            e->len = msglen;
        }
    } else {
        char* newB = realloc(e->buffer, e->len + 1 + msglen + 1);
        if (newB) {
            e->buffer = newB;
            e->buffer[e->len] = '\n';
            memcpy(e->buffer + e->len + 1, msg, msglen);
            e->buffer[e->len + 1 + msglen] = '\0';
            e->len += 1 + msglen;
        }
    }
}
