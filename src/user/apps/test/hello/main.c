/***************************************************************/
/* hello — minimal fd-stdout child for spawn-fd redirection.   */
/* Writes to inherited stdout (fd 1), echoes argv, exits 42.   */
/***************************************************************/
#include <string.h>
#include <stdint.h>
#include "userlib.h"

int main(int argc, char** argv) {
    const char* msg = "Hello from a spawned child (inherited stdout works)!\n";
    eigen_write(1, msg, strlen(msg));
    for (int i = 1; i < argc; i++) {
        eigen_write(1, "arg: ", 5);
        eigen_write(1, argv[i], strlen(argv[i]));
        eigen_write(1, "\n", 1);
    }
    return 42;
}
