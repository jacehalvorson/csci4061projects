#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    char* token = strtok(s, " "); // Set to first token
    while (token != NULL)
    {
        if (strvec_add(tokens, token) == 1) // add token to string vector
        {
            fprintf(stderr,"strvec_add");
            return 1;
        }
        token = strtok(NULL, " "); // iterate to next token
    }
    return 0;
}

int run_command(strvec_t *tokens) {
    struct sigaction sac;
    sac.sa_handler = SIG_DFL; //change handler to default response
    if (sigfillset(&sac.sa_mask) == -1) { //fill in mask, code adapted from parent process's. iirc nothing SHOULD be blocked here.
        perror("sigfillset"); //error message
        return 1;
    }
    sac.sa_flags = 0; //don't need any flags atm I think?
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) { //attempt to change to not ignoring the signals given our sac mask.
        perror("sigaction"); //error message
        return 1;
    }

    // Create array to hold tokens and copy every token over from strvector
    char *toks[MAX_ARGS];
    int i;
    int j = 0; //j stores actual index of where we are in toks, since we don't necessarily want everything in the strvec to be in toks.
    for (i = 0; i < tokens->length; i++)
    {
        char* temp; 
        if ((temp = strvec_get(tokens, i))==NULL) {//should set normally if not error.
            fprintf(stderr, "strvec_get");
            return 1;
        }
        //don't actually want ">", "<", or ">>" in our toks array, it'll screw up the exec.
        if (((strcmp(temp, ">")==0)||(strcmp(temp, "<")==0)||(strcmp(temp, ">>")==0) || (strcmp(temp, "&") == 0))) { //it's a long if but I figured this was the best way to do it.
            //don't  use >, <, or >> or the file name, skip past those. I hope.
            i += 1; //the i++ at the end of the loop will skip past the file name as well
        }
        else {
            toks[j] = temp;
            j++;
        }
    }
    toks[j] = NULL; //i persists outside of loop.

    //not sure EXACTLY where to put the process group change, let's put it here.
    if (setpgid(getpid(),getpid()) == -1)
    { //sets group id to process id- project specs said this was fine. could make a var for it but idk if we need?
        perror("setpgid");
        return 1;
    }

    char* file_name;
    int fd;
    int index_found = strvec_find(tokens, "<");
    if (index_found != -1) // '<' is found in strvec at index_found
    {
        // redirect input
        if ((file_name = strvec_get(tokens, index_found + 1)) == NULL) // get file name from next token (e.g. "ls < in.txt")
        {
            fprintf(stderr, "strvec_get");
            return 1;
        }
        fd = open(file_name, O_RDONLY);
        if (fd == -1)
        {
            perror("Failed to open input file");
            return 1;
        }
        if (dup2(fd, STDIN_FILENO) == -1)
        {
            return 1;
        }
    }

    index_found = strvec_find(tokens, ">");
    if (index_found != -1) // '>' is found in strvec at index_found
    {
        // redirect output
        if ((file_name = strvec_get(tokens, index_found + 1)) == NULL) // get file name from next token (e.g. "ls > out.txt")
        {
            fprintf(stderr, "strvec_get");
        }
        fd = open(file_name, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR); //should we add more permissions? (don't think it'd be necessary? -shrug-)
        if (fd == -1)
        {
            perror("Failed to open output file");
            return 1;
        }
        if (dup2(fd, STDOUT_FILENO) == -1)
        {
            return 1;
        }
    }
    
    index_found = strvec_find(tokens, ">>");
    if (index_found != -1) // '>>' is found in strvec at index_found, will never have both '>' and '>>'
    {
        // redirect and append output
        if ((file_name = strvec_get(tokens, index_found + 1)) == NULL) // get file name from next token (e.g. "ls >> out.txt")
        {
            fprintf(stderr, "strvec_get");
        }
        fd = open(file_name, O_WRONLY|O_APPEND|O_CREAT, S_IRUSR|S_IWUSR); //should we add more permissions? (see above? if the tests fail once we fix this segfault we can try)
        if (fd == -1)
        {
            perror("Failed to open output file");
            return 1;
        }
        if (dup2(fd, STDOUT_FILENO) == -1)
        {
            return 1;
        }
    }

   // printf("exec'ing into '%s'\n", toks[0]);
    if (execvp(toks[0], toks) == -1)
    {
        perror("exec");
        return 1;
    }

    // Not reachable after a successful exec(), but retain here to keep compiler happy
    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    int exit_code;
    char* second_arg = strvec_get(tokens, 1);
    if (second_arg == NULL)
    {
        printf("strvec_get() failed\n");
        return 1;
    }
    int job_index = atoi(second_arg);

    if (job_index >= jobs->length)
    {
        printf("Job index out of bounds\n");
        return 1;
    }
    job_t* job = job_list_get(jobs, job_index);
    if (job == NULL) {
        fprintf(stderr, "job_list_get");
        return 1;
    }

    if (is_foreground)
    {
        // If running in foreground, this job should receive the signals from the keyboard
        if (tcsetpgrp(STDIN_FILENO, job->pid) == -1)
        {
            perror("tcsetpgrp");
            return 1;
        }

        if (kill(job->pid, SIGCONT) == -1) // Continue the program
        {
            perror("kill");
            return 1;
        }

        if (waitpid(job->pid, &exit_code, WUNTRACED) == -1)
        {     // we need WUNTRACED for it now that we're getting signals that stop the child.
            perror("waitpid");
            return 1;
        }
        //printf("child exited with code %d", exit_code);

        // WUNTRACED stops if this job is stopped. WIFSTOPPED returns 1 if it stopped from a signal, otherwise 0
        if (!WIFSTOPPED(exit_code))
        {   //  If this process wasn't stopped by a signal, remove it from job list as it probably terminated on its own
            if (job_list_remove(jobs, job_index) == 1) {
                fprintf(stderr, "failed to remove job"); //else it removed succesfully
                return 1;
            }
        }

        //set parent process back to foreground
        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1)
        {
            perror("tcsetpgrp");
            return 1;
        }
    }
    else
    {
        job->status = JOB_BACKGROUND; // change from JOB_STOPPED to JOB_BACKGROUND
        if (kill(job->pid, SIGCONT) == -1) // Continue the program
        {
            perror("kill");
            return 1;
        }
    }

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    int exit_code;
    char* second_arg = strvec_get(tokens, 1);
    if (second_arg == NULL)
    {
        printf("strvec_get() failed\n");
        return 1;
    }
    int job_index = atoi(second_arg);

    job_t *job = job_list_get(jobs, job_index);

    if (job == NULL) {//get failed
        fprintf(stderr, "job index out of bounds\n"); //this is the most common reason for it to fail.
        return 1;
    }

    if (job->status != JOB_BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        fprintf(stderr, "Failed to wait for background job\n");
        return 0; //successfully didn't wait because it wasn't bg. Might change to a 1 later if that's what the specs want.
    }

    waitpid(job->pid, &exit_code, WUNTRACED);
    if (!WIFSTOPPED(exit_code)) {//if process terminates
        if (job_list_remove(jobs, job_index) == 1) {
            fprintf(stderr, "failed to remove job"); //else it removed succesfully
            return 1;
        }
    }

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    job_t *cur = jobs->head;
    int exit_code;

    while (cur != NULL) { //iterate through the list.
        if (cur->status == JOB_BACKGROUND) {
            waitpid(cur->pid, &exit_code, WUNTRACED);
            if (WIFSTOPPED(exit_code)) {
                cur->status = JOB_STOPPED; //change status to stopped
            }
        }
        cur = cur->next; //iterate to next node. (note how it ignores stopped jobs.)
    }

    job_list_remove_by_status(jobs, JOB_BACKGROUND); //this one doesn't have an error handling return value, weirdly? so I won't error check it.
    return 0;
}
