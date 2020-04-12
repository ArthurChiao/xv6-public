#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

// Support:
//
// * pipe: "cmd a | cmd b | cmd c"
// * redirect: "cmd a < xxx", "cmd b > yyy"
// * list: "cmd a; cmd b; cmd c;"
// * exec: "(cmd a)"
// * combinations of the above types

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
    int type;
};

struct pipecmd {
    int type;
    struct cmd *left;
    struct cmd *right;
};

struct redircmd {
    int type;
    struct cmd *cmd;
    char *file;
    char *efile;
    int mode;
    int fd;
};

struct listcmd {
    int type;
    struct cmd *left;
    struct cmd *right;
};

struct execcmd {
    int type;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];
};

struct bgcmd { // background cmd
    int type;
    struct cmd *cmd;
};

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

void panic(char *s);
struct cmd* null_terminate(struct cmd *cmd);
struct cmd* parse_line(char **start, char *end);

int
pop_left_token(char **start, char *end, char **q, char **eq)
{
    int ret;

    char *s = *start;
    while(s < end && strchr(whitespace, *s))
        s++;

    if(q)
        *q = s;

    ret = *s;
    switch(*s){
        case 0:
            break;
        case '|':
        case '(':
        case ')':
        case ';':
        case '&':
        case '<':
            s++;
            break;
        case '>':
            s++;
            if(*s == '>'){
                ret = '+';
                s++;
            }
            break;
        default:
            ret = 'a';
            while(s < end && !strchr(whitespace, *s) && !strchr(symbols, *s))
                s++;
            break;
    }

    if(eq)
        *eq = s;

    while(s < end && strchr(whitespace, *s))
        s++;

    *start = s;
    return ret;
}

int
trim_left_and_compare(char **start, char *end, char *toks)
{
  char *s = *start;
  while(s < end && strchr(whitespace, *s)) {
    s++;
  }

  *start = s;
  return *s && strchr(toks, *s);
}

void
trim_left(char **start, char *end)
{
  char *s = *start;
  while(s < end && strchr(whitespace, *s)) {
    s++;
  }

  *start = s;
}

struct cmd*
background_cmd(struct cmd *subcmd) {
    struct bgcmd *cmd = malloc(sizeof(*cmd));

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd;

    return (struct cmd*)cmd;
}

struct cmd*
list_cmd(struct cmd *left, struct cmd *right) {
    struct listcmd *cmd = malloc(sizeof(*cmd));

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*)cmd;
}

struct cmd*
pipe_cmd(struct cmd *left, struct cmd *right) {
    struct pipecmd *cmd = malloc(sizeof(*cmd));

    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*)cmd;
}

struct cmd*
exec_cmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redir_cmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd = malloc(sizeof(*cmd));

  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;

  return (struct cmd*)cmd;
}

///////////////////////////////////////////////////////////////////////////
// Parse routines
///////////////////////////////////////////////////////////////////////////
// 解析重定向命令
//
// 叶子函数，不会再调用其他类型的解析函数
struct cmd*
parse_redirs(struct cmd *cmd, char **start, char *end)
{
    int tok;
    char *q, *eq;

    while(1) {
        trim_left(start, end);
        if (!strchr("<>", **start)) {
            break;
        }

        tok = pop_left_token(start, end, 0, 0); // 去掉重定向符号及后面的空格

        if(pop_left_token(start, end, &q, &eq) != 'a') {
            panic("missing file for redirection");
        }

        switch(tok) {
            case '<':
                cmd = redir_cmd(cmd, q, eq, O_RDONLY, 0);
                break;
            case '>':
                cmd = redir_cmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
                break;
            case '+':  // >>
                cmd = redir_cmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
                break;
        }
    }

    return cmd;
}

// A block is a command (list) inside "()"
// 一个 block 是一个小括号内的命令或命令列表，去掉小括号后，里面的格式和命令行输
// 入是一样的，可以是任何格式，因此该函数会调用 parse_line
//
// * 去掉左括号
// * 解析括号内的命令或命令列表
// * 去掉右括号
// * 解析是否有重定向命令
struct cmd*
parse_block(char **start, char *end)
{
    struct cmd *cmd;

    trim_left(start, end);
    if(*start != "(") { // must start with "("
        panic("parseblock");
    }

    pop_left_token(start, end, 0, 0); // strip left "(" and whitespaces
    cmd = parse_line(start, end);

    if(*start != ")") { // must end with ")"
        panic("syntax - missing )");
    }

    pop_left_token(start, end, 0, 0); // strip left ")" and whitespaces

    cmd = parse_redirs(cmd, start, end);

    return cmd;
}

// * 如果以左括号开头，解析括号里面的命令，返回
// * 
struct cmd*
parse_exec(char **start, char *end) {
    char *q, *eq;
    int tok, argc;
    struct execcmd *cmd;
    struct cmd *ret;

    trim_left(start, end);
    if(*start == "(") {
        return parse_block(start, end);
    }

    ret = exec_cmd();
    cmd = (struct execcmd*)ret;

    argc = 0;
    ret = parse_redirs(ret, start, end);

    while(1) {
        trim_left(start, end);
        if (strchr("|)&;", **start)){
            break;
        }

        if((tok=pop_left_token(start, end, &q, &eq)) == 0)
            break;

        if(tok != 'a')
            panic("syntax");

        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;

        if(argc >= MAXARGS)
            panic("too many args");

        ret = parse_redirs(ret, start, end);
    }

    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

// * 解析开头的第一个命令
// * 如果有 "|" 符号，递归解析后面的命令
struct cmd*
parse_pipe(char **start, char *end) {
    struct cmd *cmd = parse_exec(start, end);

    trim_left(start, end);
    if (*start == "|") {
        pop_left_token(start, end, 0, 0); // trim left '|' and whitespaces
        cmd = pipe_cmd(cmd, parse_pipe(start, end));
    }

    return cmd;
}

// 解析命令行入口
//
// 解析顺序：
// * 解析第一个命令后面是否有 pipe，例如 "ls . | grep abcd"
// * 解析是否是后台任务，例如 "sleep 5 &"
// * 解析是否是分号隔开的多个命令，如果是，递归解析后面的命令
struct cmd*
parse_line(char **start, char *end) {
    struct cmd *cmd = parse_pipe(start, end);

    while (1) {
        trim_left(start, end);
        if (*start != "&") {
            break;
        }

        pop_left_token(start, end, 0, 0); // strip left '&' and whitespaces
        cmd = background_cmd(cmd);
    }

    trim_left(start, end);
    if (*start == ";") {
        pop_left_token(start, end, 0, 0); // strip left ';' and whitespaces
        cmd = list_cmd(cmd, parse_line(start, end));
    }

    return cmd;
}

// 几种字符串及解析状态机
//
// LINE：表示一行命令，可以是任意命令及其组合，最宽泛
// PIPE：可能包含管道（"|"）、但一定不包含分号（";"）的 LINE。遇到分号时只解析分
//       号前面的部分，然后返回上一级，由后者去掉分号，继续解析后面的部分（子 LINE）
// EXEC: PIPE 的左半部分，一般情况下就是普通命令及其参数，但也可能包括
//       * 小括号格式的子命令：BLOCK
//       * 重定向：REDIRS
// BLOCK：用小括号括起来的子命令，括号内是一个子 LINE
// REDIRS：重定向命令。REDIRS 是叶子节点，不再包含其他 pattern。
// BG：后台命令，以 "&" 结尾。BG 是叶子节点，不再包含其他 pattern。
//
//
// parse_LINE       <------------------------------------------------------
//   |                                                                    |
//   |-parse_PIPE   <---------------------------------                    |
//   |   |                                           |                    |
//   |   |-parse_EXEC                                |                    |
//   |   |  |                                        |                    |
//   |   |  |-if is block                            |                    |
//   |   |  |   -return parse_BLOCK                  |                    |
//   |   |  |             |                          |                    |
//   |   |  |             |-strip "("                |                    |
//   |   |  |             |------------>--------------------------------->|
//   |   |  |             |-strip ")"                |                    |
//   |   |  |                                        |                    |
//   |   |  |-parse_REDIRS                           |                    |
//   |   |  |                                        |                    |
//   |   |  |-parse exec cmd args                    |                    |
//   |   |                                           |                    |
//   |   |                                           |                    |
//   |   |-if is pipe                                |                    |
//   |       -strip "|"                              |                    |
//   |       -parse right cmd----------------------->|                    |
//   |                                                                    |
//   |-if is background job                                               |
//   |   -strip "&"                                                       |
//   |   -cmd = background_cmd()                                          |
//   |                                                                    |
//   |-if is cmd list                                                     |
//       -strip ";"                                                       |
//       -parse right cmd ----------------------------------------------->|

struct cmd*
parse_cmd(char *s) {
    char *start = s;
    char *end = s + strlen(s);

    struct cmd *cmd = parse_line(&start, end);

    trim_left(&start, end);
    if (start != end) {
        printf("leftovers: %s\n", start);
        panic("syntax error");
    }

    null_terminate(cmd);

    return cmd;
}

// NUL-terminate all the counted strings.
struct cmd*
null_terminate(struct cmd *cmd)
{
  int i;
  struct bgcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == NULL)
    return NULL;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    null_terminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    null_terminate(pcmd->left);
    null_terminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    null_terminate(lcmd->left);
    null_terminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct bgcmd*)cmd;
    null_terminate(bcmd->cmd);
    break;
  }
  return cmd;
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

void
panic(char *s) {
    fprintf(stderr, "%s\n", s);
    exit(1);
}

int
main() {
    char buf[1024];
    char *argv[32];

    while(1) {
        printf("shell> "); // print prompt
        fflush(stdout); // stdout is line buffed, so we must explicitly flush it

        fgets(buf, 1024, stdin);
        if (*buf == '\n') { // empty line
            continue;
        }

        parse_cmd(buf);

        if (!strcmp(argv[0], "exit")) {
            exit(0);
        }
        
        if (!strcmp(argv[0], "cd")) {
            // handle chdir
        }

        execute(argv);
    }
}
