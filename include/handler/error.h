#ifndef _HANDLER_ERROR_H
#define _HANDLER_ERROR_H

#include <stdint.h>

void kerror(const char* fmt, ...);
void kerror_render(void);
void kerror_clear(void);

int kpanic_active(void);
void kpanic_trigger(const char* title, const char* msg);
void kpanic_render(void);
void kpanic_handle_click(int mx, int my, int clicked);

#endif
