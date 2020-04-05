#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void next_stage(int read_fd, int depth) {
    int new_fds[2];
    int prime;

    int n = read(read_fd, &prime, sizeof(int));
    if (n < 0) {
        printf("read pipe failed\n");
        close(read_fd);
        return;
    }

    if (n == 0) {
        printf("depth %d: pipe closed by write side\n", depth-1);
        close(read_fd);
        return;
    }

    printf("prime %d\n", prime);

    if (pipe(new_fds) != 0) {
        printf("pipe() failed, depth %d\n", depth);
        close(read_fd);
        return;
    }

    // printf(1, "started new pipe at depth %d\n", depth);

    if (fork()) {
        close(new_fds[0]);

        int num;
        while (1) {
            int n = read(read_fd, &num, sizeof(int));
            if (n < 0) {
                printf("read pipe failed\n");
                break;
            }

            if (n == 0) {
                // printf("depth %d: pipe closed by write side\n", depth-1);
                break;
            }

            if (num % prime) {
                if (write(new_fds[1], &num, sizeof(int)) != sizeof(int)) {
                    printf("write pipe failed: num %d\n", num);
                    break;
                }

                // printf("write %d to pipe at depth: %d\n", num, depth);
            }
        }
        
        printf("close pipe at depth %d\n", depth);
        close(read_fd);
        close(new_fds[1]);
    } else {
        close(new_fds[1]);
        next_stage(new_fds[0], ++depth);
    }
}

int main(int argc, char* argv[]) {
    int fds[2];
    int depth = 1;

    if (pipe(fds) != 0) {
        printf("pipe() failed\n");
        exit(0);
    }

    if (fork()) {
        close(fds[0]);

        int i;
        for (i=2; i<=35; i++) {
            if (write(fds[1], &i, sizeof(int)) != sizeof(int)) {
                printf("write %d failed\n", i); 
                break;
            }
        }
    } else {
        // Child process will duplicate the file descriptors of its parent
        close(fds[1]);
        next_stage(fds[0], ++depth);
    }

    return 0;
}
