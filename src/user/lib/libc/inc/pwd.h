/* Minimal pwd.h stub for the Eina port (no real user database). */
#ifndef EIGEN_SHIM_PWD_H
#define EIGEN_SHIM_PWD_H

struct passwd {
    char* pw_name;
    char* pw_passwd;
    int   pw_uid;
    int   pw_gid;
    char* pw_gecos;
    char* pw_dir;
    char* pw_shell;
};

struct passwd* getpwuid(int uid);
struct passwd* getpwnam(const char* name);
struct passwd* getpwent(void);
void endpwent(void);
void setpwent(void);

#endif
