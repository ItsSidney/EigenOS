/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>

#define MAX_USERS 16
#define MAX_SESSIONS 8

// Privilege levels
typedef enum {
    PRIV_ADMIN = 0, // Ring 0 access allowed, full permissions
    PRIV_USER = 1,  // Ring 3 isolated, limited permissions
    PRIV_GUEST = 2  // Ring 3 highly restricted
} privilege_level_t;

typedef struct {
    int uid;
    char username[32];
    char password_hash[64]; // simple hash for now
    privilege_level_t priv_level;
} user_t;

typedef struct {
    int session_id;
    int uid;
    int is_active;
    uint64_t login_time;
    // Pointers to isolated page directory (CR3) for this session
    uint64_t page_directory_physical;
} session_t;

void init_security(void);
int create_user(const char* username, const char* password, privilege_level_t priv);
int authenticate_user(const char* username, const char* password);
int start_session(int uid);
void end_session(int session_id);
session_t* get_current_session(void);

// Ring 3 Transition (Architecture specific)
void switch_to_user_mode(uint64_t entry_point, uint64_t user_stack);

#endif
