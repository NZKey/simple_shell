#ifndef __PARSE_H__
#define __PARSE_H__
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#define PROMPT "ish$ " /* 入力ライン冒頭の文字列 */
#define NAMELEN 32    /* 各種名前の長さ */
#define ARGLSTLEN 16  /* 1つのプロセスがとる実行時引数の数 */
#define LINELEN 256   /* 入力コマンドの長さ */
#define PATH_DIR_BUFSIZE 512
#define COMMAND_BUFSIZE 512
#define TOKEN_BUFSIZE 128
#define TOKEN_SEPARATION " \t\r\n\a"

#define EXIT_COMMAND 0
#define BG_COMMAND 1
#define FG_COMMAND 2
#define CD_COMMAND 3
#define COMMAND_ETC 4

typedef enum write_option_ {
    TRUNC,
    APPEND,
} write_option;


typedef struct process_ {
    pid_t pid;
    char*        program_name;//command
    char**       argument_list;//argv
    int process_argc;//argc
    char*        input_redirection;//input_path
    int process_type;//type
    int process_status;//status
    write_option output_option;
    char*        output_redirection;//output_puth

    struct process_* next;//next_process
} process;

typedef enum job_mode_ {
    FOREGROUND,
    BACKGROUND,
    PIPELINE,
} job_mode;

typedef struct job_ {
    job_mode     mode;//mode
    int id;//id
    pid_t pgid;//pgid
    char *job_command;
    process*     process_list;//root
    struct job_* next;
} job;

typedef enum parse_state_ {
    ARGUMENT,
    IN_REDIRCT,
    OUT_REDIRCT_TRUNC,
    OUT_REDIRCT_APPEND,
} parse_state;

char* get_line(char *, int);
job* parse_line(char *);
void free_job(job *);
job* my_shell_parse_command(char *line);
char* my_get_line();
int get_command_type(char *command);
#endif
