/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/security/security.h"
#include "kernel/time/timer.h"

static user_t users[MAX_USERS];
static session_t sessions[MAX_SESSIONS];
static int current_session_id = -1;
static int next_uid = 1;

static int streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static int str_len(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}

void init_security(void) {
    for (int i = 0; i < MAX_USERS; i++) {
        users[i].uid = 0;
        users[i].username[0] = 0;
        users[i].password_hash[0] = 0;
    }
    for (int i = 0; i < MAX_SESSIONS; i++) sessions[i].is_active = 0;

    // Default accounts
    extern void boot_log_add(const char*, const char*, uint32_t, uint32_t);
    extern void draw_boot_log(void);
    create_user("root",  "toor",  PRIV_ADMIN);
    boot_log_add("SEC", "users initialized (root, guest)", 0xF85149, 0xE8EAED);
    draw_boot_log();
    create_user("guest", "",      PRIV_GUEST);

    // Auto-login as root at boot
    start_session(1);
}

int create_user(const char* username, const char* password, privilege_level_t priv) {
    int slot = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].uid == 0) { slot = i; break; }
    }
    if (slot == -1) return -1;

    users[slot].uid = next_uid++;
    users[slot].priv_level = priv;

    int j = 0;
    while (username[j] && j < 31) { users[slot].username[j] = username[j]; j++; }
    users[slot].username[j] = 0;

    int k = 0;
    while (password[k] && k < 63) { users[slot].password_hash[k] = password[k]; k++; }
    users[slot].password_hash[k] = 0;

    return users[slot].uid;
}

int authenticate_user(const char* username, const char* password) {
    if (!username || !password) return 0;

    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].uid == 0) continue;
        if (!streq(users[i].username, username)) continue;

        if (users[i].priv_level == PRIV_GUEST) {
            return users[i].uid;
        }

        if (streq(users[i].password_hash, password)) {
            return users[i].uid;
        }

        return 0;
    }
    return 0;
}

int start_session(int uid) {
    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].is_active) { slot = i; break; }
    }
    if (slot == -1) return -1;

    sessions[slot].session_id = slot;
    sessions[slot].uid = uid;
    sessions[slot].is_active = 1;
    sessions[slot].login_time = timer_get_ms();
    sessions[slot].page_directory_physical = 0;

    current_session_id = slot;
    return slot;
}

void end_session(int session_id) {
    if (session_id >= 0 && session_id < MAX_SESSIONS) {
        sessions[session_id].is_active = 0;
        if (current_session_id == session_id) current_session_id = -1;
    }
}

session_t* get_current_session(void) {
    if (current_session_id == -1) return 0;
    return &sessions[current_session_id];
}
