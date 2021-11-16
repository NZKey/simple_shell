#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <pwd.h>
#include "parse.h"

#define MAX_JOBS_ID 16

#define PROCESS_INIT 0
#define PROCESS_DONE 1
#define PROCESS_REMAIN 2

#define STATUS_PROC_RUNNING 0
#define STATUS_PROC_SUSPENDED 1
#define STATUS_PROC_CONTINUED 2
#define STATUS_PROC_TERMINATED 3
#define STATUS_PROC_DONE 4


const char* PROCESS_STATUS_MODE[] = {
    "running",
    "suspended",
    "continued",
    "terminated",
    "done"
};

struct shell_information{
    char cur_user[TOKEN_BUFSIZE];
    char cur_dir[PATH_DIR_BUFSIZE];
    char pw_dir[PATH_DIR_BUFSIZE];
    job *jobs[MAX_JOBS_ID + 1];
};

struct shell_information *shell;

//pid -> job id
int get_job_id_by_pid(int pid){
    process* proc;

    for (int i = 1;i <= MAX_JOBS_ID;i++){
        if (shell->jobs[i] != NULL){
            for (proc = shell->jobs[i]->process_list; proc != NULL;proc = proc->next){
                if (proc->pid == pid){//hit
                    return i;
                }
            }
        }
    }
    return -1;//fault
}

//job id -> job
job* get_job_by_job_id(int id){
    if (id > MAX_JOBS_ID){
        return NULL;//fault
    }
    return shell->jobs[id];
}


//job id -> job pgid
int get_job_pgid_by_job_id(int id){
    job* job_temp = get_job_by_job_id(id);
    if(job_temp == NULL){
        return -1;//fault
    }
    return job_temp->pgid;
}


//search id of empty job 
int search_job_id_of_empty_job(){
    
    for(int i = 1;i <= MAX_JOBS_ID;i++){
        if(shell->jobs[i] == NULL){
            return i;
        }
    }
    return -1;//fault
}

//print -> [job_id] process_pid
int print_process_of_job_by_job_id(int id){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return -1;//fault
    }
    //print job id
    printf("[%d]",id);

    //print process pid
    process* proc;
    for (proc = shell->jobs[id]->process_list; proc != NULL;proc = proc->next){
        printf(" %d",proc->pid);
    }
    printf("\n");
    return 0;
}

//print -> job status
int print_job_status_by_job_id(int id){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return -1;//fault
    }
    //print job id
    printf("[%d]",id);

    process* proc;
    for (proc = shell->jobs[id]->process_list; proc != NULL;proc = proc->next){
        //print "running" or "suspended" or "continued" or "terminated" or "done"
        printf("\t%d\t%s\t%s",proc->pid,PROCESS_STATUS_MODE[proc->process_status],proc->program_name);
        if(proc->next != NULL){
            printf("|\n");
        }
        else{//finish
            printf("\n");
        }
    }
    return 0;
}

//my free job
int my_free_job(int id){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return -1;//fault
    }

    job* job_temp = shell->jobs[id];
    process* proc;
    process* tmp;
    for(proc = job_temp->process_list;proc != NULL; ){
        tmp = proc->next;//leave next process
        //free this process
        free(proc->program_name);
        free(proc->argument_list);
        free(proc->input_redirection);
        free(proc->output_redirection);
        free(proc);
        proc = tmp;
    }
    //complete free job
    free(job_temp->job_command);
    free(job_temp);
    return 0;
}

//job -> give id to job
int give_job_id_to_new_job(job* job_tmp){
    int id = search_job_id_of_empty_job();//search empty id
    if(id < 0){
        return -1;//fault
    }

    job_tmp->id = id;//givr id to job
    shell->jobs[id] = job_tmp;
    return id;
}


//id -> remove id from job
int remove_id_from_job(int id){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return -1;
    }
    my_free_job(id);
    shell->jobs[id] = NULL;
    return 0;
}

//id -> search  job[id] is completed or not
int search_job_is_completed_or_not(int id){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return 0;//not
    }

    process* proc;

    for (proc = shell->jobs[id]->process_list; proc != NULL;proc = proc->next){
        if(proc->process_status != STATUS_PROC_DONE){
            return 0;//not
        }
    }
    return 1;//yes
}

//process (have pid) and status -> process (have pid and satatus)
int give_status_to_process(int pid,int status){
    process* proc;
    //exhaustive search process which have pid(input) and give this process status(input)
    for(int i = 1;i <= MAX_JOBS_ID;++i){
        if(shell->jobs[i] == NULL){
            continue;
        }
        for(proc = shell->jobs[i]->process_list; proc != NULL;proc = proc->next){
            if(proc->pid == pid){
                proc->process_status = status;
                return 0;
            }
        }
    }
    return -1;
}

//set all process in job[id] satatus
int give_status_to_job(int id,int status){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return 0;
    }
    process* proc;

    for(proc = shell->jobs[id]->process_list; proc != NULL;proc = proc->next){
        if (proc->process_status != STATUS_PROC_DONE){
            proc->process_status = status;
        }
    }
    return 0;
}

//wait process (which have pid) 
int wait_for_pid(int pid){
    int status = 0;//status running
    waitpid(pid,&status,WUNTRACED);
    //done
    if(WIFEXITED(status)){
        give_status_to_process(pid,STATUS_PROC_DONE);
    }
    //terminated
    else if(WIFSIGNALED(status)){
        give_status_to_process(pid,STATUS_PROC_TERMINATED);
    }
    //suspended
    else if(WSTOPSIG(status)){
        status = -1;
        give_status_to_process(pid,STATUS_PROC_SUSPENDED);
    }
    return status;
}

//count process (which need wait) in job[id]
int get_proc_count(int id,int filter){
    if(id > MAX_JOBS_ID || shell->jobs[id] == NULL){
        return -1;
    }
    int cnt = 0;
    process* proc;

    for(proc = shell->jobs[id]->process_list;proc != NULL;proc = proc->next){
        if (filter == PROCESS_INIT || (filter == PROCESS_DONE && proc->process_status == STATUS_PROC_DONE) ||
        (filter == PROCESS_REMAIN && proc->process_status != STATUS_PROC_DONE)){
            cnt++;
        }
    }
    return cnt;
}

//wait job[id]
int wait_for_job(int id) {
    if (id > MAX_JOBS_ID || shell->jobs[id] == NULL) {
        return -1;
    }

    int proc_cnt = get_proc_count(id, PROCESS_REMAIN);//search process (status != STATUS_PROC_DONE)

    int wait_pid = -1;
    int wait_cnt = 0;
    int status = 0;//running

    do {
        wait_pid = waitpid(shell->jobs[id]->pgid, &status, WUNTRACED);
        wait_cnt++;

        if (WIFEXITED(status)) {
            give_status_to_process(wait_pid, STATUS_PROC_DONE);
        } 
        else if (WIFSIGNALED(status)) {
            give_status_to_process(wait_pid, STATUS_PROC_TERMINATED);
        } 
        else if (WSTOPSIG(status)) {
            status = -1;
            give_status_to_process(wait_pid, STATUS_PROC_SUSPENDED);
            if (wait_cnt == proc_cnt) {
                print_job_status_by_job_id(id);
            }
        }
    } while (wait_cnt < proc_cnt);

    return status;
}


//prepare for cd command
void my_shell_prepare_for_my_cd() {
    if(getcwd(shell->cur_dir, sizeof(shell->cur_dir)) == NULL){
        printf("my_shell_prepare_for_my_cd\n");
    }
}

//cd
int my_shell_cd(int argc, char** argv) {

    if (argc == 1) {
        if(chdir(shell->pw_dir) == -1){
            printf("my_shell_cd\n");
            return 0;
        }
        my_shell_prepare_for_my_cd();
        return 0;
    }

    if (chdir(argv[1]) == 0) {
        my_shell_prepare_for_my_cd();
        return 0;
    } 
    else {
        printf("No such file or directory\n");
        return 0;
    }
}

//search background job
pid_t search_background_job(){
    //process* proc;
    for(int i = 1;i <= MAX_JOBS_ID;++i){
        if(shell->jobs[i] == NULL){
            continue;
        }
        if(shell->jobs[i]->mode == BACKGROUND){
            return shell->jobs[i]->process_list->pid;
        }
    }
    return -100;
}

//fg
int my_shell_fg() {

    pid_t pid;
    
    pid = search_background_job();
    if(pid == -100){
        printf("no background job\n");
    }

    if (kill(pid, SIGCONT) < 0) {
        printf("job not found\n");
        return -1;
    }
    //tcsetgrp
    tcsetpgrp(0, pid);

    wait_for_pid(pid);

    signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(0, getpid());
    signal(SIGTTOU, SIG_DFL);

    return 0;
}

//search suspended process
pid_t search_process_status(){
    process* proc;
    for(int i = 1;i <= MAX_JOBS_ID;++i){
        if(shell->jobs[i] == NULL){
            continue;
        }
        for(proc = shell->jobs[i]->process_list; proc != NULL;proc = proc->next){
            if(proc->process_status == STATUS_PROC_SUSPENDED){
                return proc->pid;
            }
        }
    }
    return -100;
}

//bg
int my_shell_bg(/*int argc, char **argv*/) {
    
    pid_t pid;

    pid = search_process_status();
    if(pid == -100){
        printf("no suspended job\n");
    }

    if (kill(-pid, SIGCONT) < 0) {
        printf("my_shell: bg %d: job not found\n", pid);
        return -1;
    }

    return 0;
}

//exit
int my_shell_exit() {
    exit(0);
}

//check
void check_zombi_process() {
    int status;
    int pid;

    while ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0) {
        //give status to process
        if (WIFEXITED(status)) {
            give_status_to_process(pid, STATUS_PROC_DONE);
        } else if (WIFSTOPPED(status)) {
            give_status_to_process(pid, STATUS_PROC_SUSPENDED);
        } else if (WIFCONTINUED(status)) {
            give_status_to_process(pid, STATUS_PROC_CONTINUED);
        }

        int job_id = get_job_id_by_pid(pid);
        if (job_id > 0 && search_job_is_completed_or_not(job_id)) {
            print_job_status_by_job_id(job_id);
            remove_id_from_job(job_id);
        }
    }
}

//handler for sigint
void handler_of_sigint(int signal) {
    printf("\n");
}

//commnd -> each function
int my_shell_execute_command(process *proc) {
    int status = 1;

    switch (proc->process_type) {
        case EXIT_COMMAND:
            my_shell_exit();
            break;
        case CD_COMMAND:
            my_shell_cd(proc->process_argc, proc->argument_list);
            break;
        case FG_COMMAND:
            my_shell_fg(proc->process_argc, proc->argument_list);
            break;
        case BG_COMMAND:
            my_shell_bg(proc->process_argc, proc->argument_list);
            break;
        default:
            status = 0;
            break;
    }

    return status;
}


int my_shell_execute_process(job *job,process *proc, int input_fd, int output_fd, int mode) {
    proc->process_status = STATUS_PROC_RUNNING;

    if (proc->process_type != COMMAND_ETC && my_shell_execute_command(proc)) {
        return 0;//exist command
    }

    pid_t childpid;
    int status = 0;

    childpid = fork();

    if (childpid < 0) {
        return -1;
    } else if (childpid == 0) {
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        proc->pid = getpid();

        if (job->pgid > 0) {
            setpgid(0, job->pgid);
        } else {
            job->pgid = proc->pid;
            setpgid(0, job->pgid);
        }

        if (input_fd != 0) {
            dup2(input_fd, 0);
            close(input_fd);
        }

        if (output_fd != 1) {
            dup2(output_fd, 1);
            close(output_fd);
        }
        //exe
        if (execvp(proc->argument_list[0], proc->argument_list) < 0) {
            printf("command not found\n");
            exit(0);
        }
        exit(0);
    } 
    else {
        proc->pid = childpid;

        if (job->pgid > 0) {
            setpgid(childpid, job->pgid);
        } else {
            job->pgid = proc->pid;
            setpgid(childpid, job->pgid);
        }

        if (mode == FOREGROUND) {
            tcsetpgrp(0, job->pgid);
            status = wait_for_job(job->id);

            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(0, getpid());
            signal(SIGTTOU, SIG_DFL);
        }
    }

    return status;
}

//execute job
int my_shell_launch_job(job *job) {
    process *proc;
    int status = 0, input_fd = 0, fd[2], job_id = -1;

    check_zombi_process();

    if (job->process_list->process_type == COMMAND_ETC) {
        job_id = give_job_id_to_new_job(job);
    }

    for (proc = job->process_list; proc != NULL; proc = proc->next) {
        if (proc == job->process_list && proc->input_redirection != NULL) {
            input_fd = open(proc->input_redirection, O_RDONLY);
            if (input_fd < 0) {
                printf("no such file or directory\n");
                remove_id_from_job(job_id);
                return -1;
            }
        }
        if (proc->next != NULL) {
            if(pipe(fd) == -1){
                printf("pipe error\n");
                return -1;
            }
            status = my_shell_execute_process(job, proc, input_fd, fd[1], PIPELINE);
            close(fd[1]);
            input_fd = fd[0];
        } 
        else {
            int output_fd = 1;
            if (proc->output_redirection != NULL) {
                output_fd = open(proc->output_redirection, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
                if (output_fd < 0) {
                    output_fd = 1;
                }
            }
            status = my_shell_execute_process(job, proc, input_fd, output_fd, job->mode);
        }
    }

    if (job->process_list->process_type == COMMAND_ETC) {
        //foreground
        if (status >= 0 && job->mode == FOREGROUND) {
            remove_id_from_job(job_id);
        } 
        //background
        else if (job->mode == BACKGROUND) {
            print_process_of_job_by_job_id(job_id);
        }
    }

    return status;
}

void my_shell_print_promt() {
    printf("ish$ ");
}


void my_shell_exe() {
    char *line;
    //char line[LINELEN];
    job *job_tmp;
    

    while (1) {
        my_shell_print_promt();
        line = my_get_line();
        //get_line(line,LINELEN);

        if (strlen(line) == 0) {
            check_zombi_process();
            continue;
        }

        job_tmp = my_shell_parse_command(line);
        my_shell_launch_job(job_tmp);
    }
}

void my_shell_init() {
    
    signal(SIGINT,SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    pid_t pid = getpid();
    setpgid(pid, pid);
    tcsetpgrp(0, pid);

    shell = (struct shell_information*) malloc(sizeof(struct shell_information));
    getlogin_r(shell->cur_user, sizeof(shell->cur_user));

    struct passwd *pw = getpwuid(getuid());
    strcpy(shell->pw_dir, pw->pw_dir);

    int i;
    for (i = 0; i < MAX_JOBS_ID; i++) {
        shell->jobs[i] = NULL;
    }

    my_shell_prepare_for_my_cd();
}


int main(int argc, char **argv) {
    my_shell_init();
    my_shell_exe();

    return 0;
}

// int main(int argc, char *argv[]) {
//     char s[LINELEN];
//     job *curr_job;

//     while(get_line(s, LINELEN)) {
//         if(!strcmp(s, "exit\n"))
//             break;

//         curr_job = parse_line(s);

//         print_job_list(curr_job);

//         free_job(curr_job);
//     }

//     return 0;
// }
//
