#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#ifndef MAXARG
#define MAXARG 1024
#endif

#define DEBUG 1

void execute(int argc, char* cmd[]) {
#if DEBUG
    int i;

    printf("Command to be executed: ");
    for (i=0; i<argc;i++) {
        printf("%s ", cmd[i]);
    }
    printf("\n");
#endif

    int pid = fork();
    if (pid < 0) {
        printf("fork() failed\n");
        exit(1);
    }

    if (pid == 0) {
        execvp(cmd[0], cmd);

        // if execvp returned, it must have failed
        perror("execv() failed\n");
        exit(0);
    }

    int status;
    wait(&status);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        exit(0);
    }

    char* cmd[MAXARG];
    int i, idx;
    argc -= 1; // skip argv[0]
    argv += 1;
    for(i=0; i<argc; i++) { // copy parameters
        cmd[i] = argv[i];
    }
    idx = i;
    printf("Program: %s\n", cmd[0]);

    char c;
    char line[MAXARG * 16]; // max length of sum(all lines)
    char *s, *e;             // word start and end

    s = line;
    e = line;
    while (1) {
        c = getchar();
        if (c == EOF) { // CTL-D generates EOF
            break;
        } else if (c == ' ' || c == '\t' || c == '\n') {
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

    exit(0);
}
