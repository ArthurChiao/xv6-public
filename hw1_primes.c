#include "types.h"
#include "stat.h"
#include "user.h"

void next_stage(int read_fd, int depth) {
    int new_fds[2];
    int prime_num;

    int n = read(read_fd, &prime_num, sizeof(int));
    if (n < 0) {
        printf(2, "read pipe failed\n");
        close(read_fd);
        exit();
    }

    if (n == 0) {
        // printf(1, "depth %d: pipe closed by write side\n", depth-1);
        close(read_fd);
        exit();
    }

    printf(1, "prime %d\n", prime_num);

    if (pipe(new_fds) != 0) {
        printf(2, "pipe() failed, depth %d\n", depth);
        close(read_fd);
        exit();
    }

    // printf(1, "started new pipe at depth %d\n", depth);

    if (fork()) {
        close(new_fds[0]);

        int num;
        while (1) {
            int n = read(read_fd, &num, sizeof(int));
            if (n < 0) {
                printf(2, "read pipe failed\n");
                break;
            }

            if (n == 0) {
                // printf(1, "depth %d: pipe closed by write side\n", depth-1);
                break;
            }

            if (num % prime_num) {
                if (write(new_fds[1], &num, sizeof(int)) != sizeof(int)) {
                    printf(2, "write pipe failed: num %d\n", num);
                    break;
                }

                // printf(1, "write %d to pipe at depth: %d\n", num, depth);
            }
        }
        
        // printf(1, "close pipe at depth %d\n", depth);
        close(read_fd);
        close(new_fds[1]);
        wait();
    } else {
        close(new_fds[1]);
        next_stage(new_fds[0], ++depth);
    }
}

int main(int argc, char* argv[]) {
    int fds[2];
    int depth = 1;

    if (pipe(fds) != 0) {
        printf(2, "pipe() failed\n");
        exit();
    }

    if (fork()) {
        close(fds[0]);

        int i;
        for (i=2; i<=35; i++) {
            if (write(fds[1], &i, sizeof(int)) != sizeof(int)) {
                printf(2, "write %d failed\n", i); 
                break;
            }
        }

        // printf(1, "close pipe at depth 1\n");
        close(fds[1]);

        // remove the wait() out will also work, but will results to many zombie
        // processes, try this by commenting wait() out in this file
        wait();
    } else {
        // Child will duplicate parent's file descriptors
        close(fds[1]);
        next_stage(fds[0], ++depth);
    }

    exit();
}
