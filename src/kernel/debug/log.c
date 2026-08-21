/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/log.h"
#include "kernel/mem/kheap.h"
#include "drivers/video/framebuffer.h"
#include <string.h>

static char* log_buffer;
static int log_index = 0;
static int logging_locked = 0;

void log_init(void) {
    if (log_buffer) return;
    log_buffer = kmalloc(LOG_BUF_SIZE);
    memset(log_buffer, 0, LOG_BUF_SIZE);
}

void klog(const char* s) {
    if (!log_buffer || logging_locked) {
        return;
    }
    
    logging_locked = 1;
    int len = strlen(s);
    if (log_index + len >= LOG_BUF_SIZE) {
        logging_locked = 0;
        return;
    }
    
    memcpy(log_buffer + log_index, s, len);
    log_index += len;
    logging_locked = 0;
}

const char* log_get_buffer(void) {
    return log_buffer;
}

int log_get_size(void) {
    return log_index;
}

void log_dump(void) {
    if (!log_buffer) {
        print_string("  Log: Not initialized\n");
        return;
    }
    
    logging_locked = 1;
    print_string("\n--- Boot Log ---\n");
    if (log_index == 0) {
        print_string("  Log is empty\n");
    } else {
        print_string(log_buffer);
    }
    print_string("\n--- End Log ---\n");
    logging_locked = 0;
}
