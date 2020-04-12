#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

// Very basic shell
// Only support one-shot command, no pipe, redirection
//
// Originated from:
// http://www.csl.mtu.edu/cs4411.ck/www/NOTES/process/fork/shell.c

void
parse_cmd(char *buf, char *argv[]) {
    while (*buf != '\0') {
        while (*buf == ' ' || *buf == '\t' || *buf == '\n') {
            *buf++ = '\0';
        }

        *argv++ = buf;

        while (*buf != ' ' && *buf != '\t' && *buf != '\n' && *buf != '\0') {
            buf++;
        }
    }

    *argv = NULL;
}

void
execute(char *argv[]) {
    if (fork() == 0) {
        execvp(*argv, argv);

        printf("execvp failed\n");
        exit(1);
    } else {
        int status;
        wait(&status);
    }
}

int
main() {
    char buf[1024];
    char *argv[32];

    while(1) {
        printf("shell> "); // print prompt
        fflush(stdout); // stdout is line buffed, so we have to explicit flush it

        fgets(buf, 1024, stdin);

        parse_cmd(buf, argv);

        if (!strcmp(argv[0], "exit")) {
            exit(0);
        }

        execute(argv);
    }
}
