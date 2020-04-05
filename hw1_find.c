#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#ifndef SUPPORT_REGIX
#define SUPPORT_REGIX 1
#endif

#ifdef SUPPORT_REGIX
// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  while(*text!='\0' && (*text++==c || c=='.')) {
    if(matchhere(re, text))
      return 1;
  }

  return 0;
}
#endif

char*
filename(char *path)
{
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;

  return ++p;
}

void find(char* path, char *target) {
    int fd = -1;
    char buf[512], *p;
    struct stat st;
    struct dirent de;

    if(stat(path, &st) < 0){
        printf(1, "find: cannot stat %s\n", path);
        return;
    }

    switch(st.type){
        case T_FILE:
#ifdef SUPPORT_REGIX
            if (match(target, filename(path))) {
#else
            if (!strcmp(target, filename(path))) {
#endif
                printf(1, "%s\n", path);
            } else {
                // printf(1, "Skip file %s %d %d %d\n", filename(path), st.type, st.ino, st.size);
            }
            break;

        case T_DIR:
            if ((fd = open(path, 0)) < 0) {
                printf(2, "find: cannot open %s\n", path);
                break;
            }

            if(fstat(fd, &st) < 0){
                printf(2, "find: cannot fstat %s\n", path);
                close(fd);
                break;
            }

            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
                printf(1, "find: path too long\n");
                break;
            }

            strcpy(buf, path);
            p = buf+strlen(path);
            *p++ = '/';
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0)
                    continue;

                if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) {
                    // printf(1, "Skip directory . and ..\n");
                    continue;
                }

                memmove(p, de.name, sizeof(de.name));
                p[sizeof(de.name)] = 0;
                // printf(1, "next dir: %s\n", buf);

                find(buf, target);
            }
            break;
        /* default: */
        /*     printf(2, "Unknown file type %d\n", st.type); */
    }

    if (fd >= 0) {
        close(fd);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf(2, "Usage: %s <path> <file>\n", argv[0]);
        exit();
    }

    char* path = argv[1];
    char* target = argv[2];
    find(path, target);

    exit();
}
