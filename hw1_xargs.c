#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

#define DEBUG 1

void execute(int argc, char* cmd[]) {
#if DEBUG
    int i;

    printf(1, "Command to be executed: ");
    for (i=0; i<argc;i++) {
        printf(1, "%s ", cmd[i]);
    }
    printf(1, "\n");
#endif

    int pid;
    if((pid = fork()) < 0) {
        printf(1, "fork() failed\n");
        exit();
    } else if (pid == 0) { // child process
        exec(cmd[0], cmd);

        // if execvp returned, it must have failed
        printf(2, "execv() failed\n");
        exit();
    }

    wait();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        exit();
    }

    char* cmd[MAXARG];
    int i, idx;
    argc -= 1; // skip argv[0]
    argv += 1;
    for(i=0; i<argc; i++) { // copy parameters
        cmd[i] = argv[i];
    }
    idx = i;

    char buf[4], c;
    char line[MAXARG * 16]; // max length of sum(all lines)
    char *s, *e;             // word start and end

    s = line;
    e = line;
    while (1) {
        gets(buf, 2); // gets(buf, max) will skip reading if max==1, see ulib.c
        c = buf[0];
        if (c == '\0') { // CTL-D generates EOF
            break;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (s == e) {
                continue;
            }

            *e++ = '\0';     // get an intact word
            cmd[idx++] = s;  // store to cmd[]
            s = e;           // prepare for next word
        } else {
            *e++ = c;
        }
    }

    execute(idx, cmd);

    exit();
}
