#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char* argv[]) {
    int p1_fds[2], p2_fds[2];
    char c = 'a';

    if(pipe(p1_fds) != 0){
        printf(1, "pipe() failed\n");
        exit();
    }

    if(pipe(p2_fds) != 0){
        printf(1, "pipe() failed\n");
        exit();
    }

    if (fork() == 0) { // child
        int pid = getpid();

        if (write(p2_fds[1], &c, 1) != 1) {
            printf(2, "write to parent failed\n");
            exit();
        }

        while(read(p1_fds[0], &c, 1) == 1) {
            printf(1, "%d: received pong\n", pid);

            if (write(p2_fds[1], &c, 1) != 1) {
                printf(2, "write to parent failed\n");
                exit();
            }
        }

        printf(2, "%d: read pipe failed", pid);
    } else { // parent
        int pid = getpid();
        char c;

        while(read(p2_fds[0], &c, 1) == 1) {
            printf(1, "%d: received ping\n", pid);

            if (write(p1_fds[1], &c, 1) != 1) {
                printf(2, "write to parent failed\n");
                exit();
            }
        }

        printf(2, "%d: read pipe failed", pid);
    }

    exit();
}
