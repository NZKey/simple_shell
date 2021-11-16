#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include "parse.h"





/* 標準入力から最大size-1個の文字を改行またはEOFまで読み込み、sに設定する */
char* get_line(char *s, int size) {

    printf(PROMPT);

    while(fgets(s, size, stdin) == NULL) {
        if(errno == EINTR)
            continue;
        return NULL;
    }

    return s;
}

static char* initialize_program_name(process *p) {

    if(!(p->program_name = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->program_name, 0, NAMELEN);

    return p->program_name;
}

static char** initialize_argument_list(process *p) {

    if(!(p->argument_list = (char**)malloc(sizeof(char*)*ARGLSTLEN)))
        return NULL;

    int i;
    for(i=0; i<ARGLSTLEN; i++)
        p->argument_list[i] = NULL;

    return p->argument_list;
}

static char* initialize_argument_list_element(process *p, int n) {

   if(!(p->argument_list[n] = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

   memset(p->argument_list[n], 0, NAMELEN);

   return p->argument_list[n];
}

static char* initialize_input_redirection(process *p) {

    if(!(p->input_redirection = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->input_redirection, 0, NAMELEN);

    return p->input_redirection;
}

static char* initialize_output_redirection(process *p) {

    if(!(p->output_redirection = (char*)malloc(sizeof(char)*NAMELEN)))
        return NULL;

    memset(p->output_redirection, 0, NAMELEN);

    return p->output_redirection;
}

static process* initialize_process() {

    process *p;

    if((p = (process*)malloc(sizeof(process))) == NULL)
        return NULL;

    initialize_program_name(p);
    initialize_argument_list(p);
    initialize_argument_list_element(p, 0);
    p->input_redirection = NULL;
    p->output_option = TRUNC;
    p->output_redirection = NULL;
    p->next = NULL;

    return p;
}

static job* initialize_job() {

    job *j;

    if((j = (job*)malloc(sizeof(job))) == NULL)
        return NULL;

    j->mode = FOREGROUND;
    j->process_list = initialize_process();
    j->next = NULL;

    return j;
}

static void free_process(process *p) {

    if(!p) return;

    free_process(p->next);

    if(p->program_name) free(p->program_name);
    if(p->input_redirection) free(p->input_redirection);
    if(p->output_redirection) free(p->output_redirection);

    if(p->argument_list) {
        int i;
        for(i=0; p->argument_list[i] != NULL; i++)
            free(p->argument_list[i]);
        free(p->argument_list);
    }

    free(p);
}

void free_job(job *j) {

    if(!j) return;

    free_job(j->next);

    free_process(j->process_list);

    free(j);
}

/* parser */
/* 受け付けた文字列を解析して結果をjob構造体に入れる関数 */
job* parse_line(char *buf) {

    job *curr_job = NULL;
    process *curr_prc = NULL;
    parse_state state = ARGUMENT;
    int index=0, arg_index=0;

    /* 改行文字まで解析する */
    while(*buf != '\n') {
        /* 空白およびタブを読んだときの処理 */
        if(*buf == ' ' || *buf == '\t') {
            buf++;
            if(index) {
                index = 0;
                state = ARGUMENT;
                ++arg_index;
            }
        }
        /* 以下の3条件は、状態を遷移させる項目である */
        else if(*buf == '<') {
            state = IN_REDIRCT;
            buf++;
            index = 0;
        } else if(*buf == '>') {
            buf++;
            index = 0;
            if(state == OUT_REDIRCT_TRUNC) {
                state = OUT_REDIRCT_APPEND;
                if(curr_prc)
                    curr_prc->output_option = APPEND;
            }
            else {
                state = OUT_REDIRCT_TRUNC;
            }
        } else if(*buf == '|') {
            state = ARGUMENT;
            buf++;
            index = 0;
            arg_index = 0;
            if(curr_job) {
                strcpy(curr_prc->program_name,
                       curr_prc->argument_list[0]);
                curr_prc->next = initialize_process();
                curr_prc = curr_prc->next;
            }
        }
        /* &を読めば、modeをBACKGROUNDに設定し、解析を終了する */
        else if(*buf == '&') {
            buf++;
            if(curr_job) {
                curr_job->mode = BACKGROUND;
                break;
            }
        }
        /* 以下の3条件は、各状態でprocess構造体の各メンバに文字を格納する */
        /* 状態ARGUMENTは、リダイレクション対象ファイル名以外の文字(プログラム名、オプション)を
           読む状態 */
        /* 状態IN_REDIRCTは入力リダイレクション対象ファイル名を読む状態 */
        /* 状態OUT_REDIRCT_*は出力リダイレクション対象ファイル名を読む状態 */
        else if(state == ARGUMENT) {
            if(!curr_job) {
                curr_job = initialize_job();
                curr_prc = curr_job->process_list;
            }

            if(!curr_prc->argument_list[arg_index])
                initialize_argument_list_element(curr_prc, arg_index);

            curr_prc->argument_list[arg_index][index++] = *buf++;
        } else if(state == IN_REDIRCT) {
            if(!curr_prc->input_redirection)
                initialize_input_redirection(curr_prc);

            curr_prc->input_redirection[index++] = *buf++;
        } else if(state == OUT_REDIRCT_TRUNC || state == OUT_REDIRCT_APPEND) {
            if(!curr_prc->output_redirection)
                initialize_output_redirection(curr_prc);

            curr_prc->output_redirection[index++] = *buf++;
        }
    }

    /* 最後に、引数の0番要素をprogram_nameにコピーする */
    if(curr_prc)
        strcpy(curr_prc->program_name, curr_prc->argument_list[0]);

    return curr_job;
}

int get_command_type(char *command) {
    if (strcmp(command, "exit") == 0) {
        return EXIT_COMMAND;
    } 
    else if (strcmp(command, "cd") == 0) {
        return CD_COMMAND;
    } 
    else if (strcmp(command, "fg") == 0) {
        return FG_COMMAND;
    } 
    else if (strcmp(command, "bg") == 0) {
        return BG_COMMAND;
    }
    else 
        return COMMAND_ETC;
}

process* my_shell_parse_command_pre_pre(char *str) {
    int bufsize = TOKEN_BUFSIZE;

    int position = 0;

    char *command = strdup(str);
    char *token;
    char **tokens = (char**) malloc(bufsize * sizeof(char*));

    if (!tokens) {
        printf("my_shell_parse_command_pre_pre error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(str, TOKEN_SEPARATION);
    while (token != NULL) {

        if (position >= bufsize) {
            bufsize += TOKEN_BUFSIZE;
            tokens = (char**) realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                printf("allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
            tokens[position] = token;
            position++;

        token = strtok(NULL, TOKEN_SEPARATION);
    }

    int i = 0, argc = 0;
    char *input_redirec = NULL, *output_redirec = NULL;

    while (i < position) {
        if (tokens[i][0] == '<' || tokens[i][0] == '>') {
            break;
        }
        i++;
    }
    argc = i;

    for (; i < position; i++) {
        // < 
        if (tokens[i][0] == '<') {
            //after < -> 隙間あり
            if (strlen(tokens[i]) == 1) {
                input_redirec = (char *) malloc((strlen(tokens[i + 1]) + 1) * sizeof(char));
                strcpy(input_redirec, tokens[i + 1]);
                i++;
            }
            //after < -> 隙間なし
            else {
                input_redirec = (char *) malloc(strlen(tokens[i]) * sizeof(char));
                strcpy(input_redirec, tokens[i] + 1);
            }
        }
        // > 
        else if (tokens[i][0] == '>') {
            //after > -> 隙間あり
            if (strlen(tokens[i]) == 1) {
                output_redirec = (char *) malloc((strlen(tokens[i + 1]) + 1) * sizeof(char));
                strcpy(output_redirec, tokens[i + 1]);
                i++;
            }
            //after > -> 隙間なし 
            else {
                output_redirec = (char *) malloc(strlen(tokens[i]) * sizeof(char));
                strcpy(output_redirec, tokens[i] + 1);
            }
        } else {
            break;
        }
    }

    for (i = argc; i <= position; i++) {
        tokens[i] = NULL;
    }

    //input
    process *new_proc = (process*) malloc(sizeof(process));
    new_proc->program_name = command;
    new_proc->argument_list = tokens;
    new_proc->process_argc = argc;
    new_proc->input_redirection = input_redirec;
    new_proc->output_redirection = output_redirec;
    new_proc->pid = -1;
    new_proc->process_type = get_command_type(tokens[0]);
    new_proc->next = NULL;
    return new_proc;
}

//remove ' '
char* my_shell_parse_command_pre(char* line) {
    char *hd = line;
    char *tl = line + strlen(line);

    while (*hd == ' ') {
        hd++;
    }

    while (*tl == ' ') {
        tl--;
    }

    *(tl + 1) = '\0';

    return hd;
}

//parse
job* my_shell_parse_command(char *line) {
    line = my_shell_parse_command_pre(line);
    char *command = strdup(line);

    //init status
    process *root_proc = NULL;
    process *proc = NULL;
    char *line_using = line;
    char *com = line;
    char *seg;
    int seg_len = 0;
    int mode = FOREGROUND;

    //background
    if (line[strlen(line) - 1] == '&') {
        mode = BACKGROUND;
        line[strlen(line) - 1] = '\0';
    }

    while (1) {
        //go until last or pipe
        if (*com == '\0' || *com == '|') {

            seg = (char*) malloc((seg_len + 1) * sizeof(char));
            //そこまでの内容 -> seg
            strncpy(seg, line_using, seg_len);
            seg[seg_len] = '\0';

            process* new_proc = my_shell_parse_command_pre_pre(seg);
            if (!root_proc) {
                root_proc = new_proc;
                proc = root_proc;
            } 
            else {
                proc->next = new_proc;
                proc = new_proc;
            }

            //pipe
            if (*com != '\0') {
                line_using = com;
                while (*(++line_using) == ' ');
                com = line_using;
                seg_len = 0;
                continue;
            } 
            else {
                break;
            }
        } 
        else {
            seg_len++;
            com++;
        }
    }
    //input
    job *new_job = (job*) malloc(sizeof(job));
    new_job->process_list = root_proc;
    new_job->job_command = command;
    new_job->pgid = -1;
    new_job->mode = mode;
    return new_job;
}

//get line
char* my_get_line() {
    int bufsize = COMMAND_BUFSIZE;
    int position = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int com;

    if (!buffer) {
        printf("get line error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        //input
        com = getchar();
        if (com == EOF || com == '\n') {
            buffer[position] = '\0';
            return buffer;
        } 
        else {
            buffer[position] = com;
        }
        position++;

        if (position >= bufsize) {
            bufsize += COMMAND_BUFSIZE;
            buffer = realloc(buffer, bufsize);

            if (!buffer) {
                printf("get line error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}