/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_LOG_H
#define _KERNEL_LOG_H

#define LOG_BUF_SIZE 4096

void log_init(void);
void klog(const char* s);
void log_dump(void);
const char* log_get_buffer(void);
int log_get_size(void);

#endif
