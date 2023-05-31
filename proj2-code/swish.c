#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define CMD_LEN 512
#define PROMPT "@> "

int main(int argc, char **argv) {
    struct sigaction sac;
    sac.sa_handler = SIG_IGN;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset");
        return 1;
    }
    sac.sa_flags = 0;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    int echo = 0;
    if (argc > 1 && strcmp(argv[1], "--echo") == 0) {
        echo = 1;
    }

    strvec_t tokens;
    if (strvec_init(&tokens) == 1)
    {
        fprintf(stderr, "strvec_init");
        return 1;
    }
    job_list_t jobs;
    job_list_init(&jobs);
    char cmd[CMD_LEN];

    printf("%s", PROMPT);
    while (fgets(cmd, CMD_LEN, stdin) != NULL) {
        if (echo) {
            printf("%s", cmd);
        }
        // Need to remove trailing '\n' from cmd. There are fancier ways.
        int i = 0;
        while (cmd[i] != '\n') {
            i++;
        }
        cmd[i] = '\0';

        if (tokenize(cmd, &tokens) != 0) {
            printf("Failed to parse command\n");
            strvec_clear(&tokens);
            job_list_free(&jobs);
            return 1;
        }
        if (tokens.length == 0) {
            printf("%s", PROMPT);
            continue;
        }
        const char *first_token = strvec_get(&tokens, 0);
        if (first_token == NULL)
        {
            printf("strvec_get\n");
            strvec_clear(&tokens);
            job_list_free(&jobs);
            return 1;
        }

        if (strcmp(first_token, "pwd") == 0) {
            // TODO Task 1: Print the shell's current working directory
            // Use the getcwd() system call
            char cwd_buf[CMD_LEN];
            if (getcwd(cwd_buf, CMD_LEN) == NULL)
            {
                perror("getcwd");
            }
            // printf("Working directory: %s\n", cwd_buf);
            printf("%s\n", cwd_buf);
        }

        else if (strcmp(first_token, "cd") == 0) {
            if (tokens.length > 1)
            {
                char *second_token = strvec_get(&tokens, 1);
                if (chdir(second_token) != 0) // error check
                {
                    perror("chdir");
                }
            }
            else // If "cd" is only token, default to home directory
            {
                char *home_dir = getenv("HOME");
                if (home_dir != NULL)
                {
                    if (chdir(home_dir) != 0) // error check
                    {
                        perror("chdir to home dir failed");
                    }
                }
                else
                {
                    printf("Couldn't find home directory\n");
                }
            }
        }

        else if (strcmp(first_token, "exit") == 0) {
            strvec_clear(&tokens);
            break;
        }

        // Task 5: Print out current list of pending jobs
        else if (strcmp(first_token, "jobs") == 0) {
            int i = 0;
            job_t *current = jobs.head;
            while (current != NULL) {
                char *status_desc;
                if (current->status == JOB_BACKGROUND) {
                    status_desc = "background";
                } else {
                    status_desc = "stopped";
                }
                printf("%d: %s (%s)\n", i, current->name, status_desc);
                i++;
                current = current->next;
            }
        }

        // Task 5: Move stopped job into foreground
        else if (strcmp(first_token, "fg") == 0) {
            if (resume_job(&tokens, &jobs, 1) == 1) {
                printf("Failed to resume job in foreground\n");
            }
        }

        // Task 6: Move stopped job into background
        else if (strcmp(first_token, "bg") == 0) {
            if (resume_job(&tokens, &jobs, 0) == 1) {
                printf("Failed to resume job in background\n");
            }
        }

        // Task 6: Wait for a specific job identified by its index in job list
        else if (strcmp(first_token, "wait-for") == 0) {
            if (await_background_job(&tokens, &jobs) == 1) {
                printf("Job index is for stopped process not background process\n");
            }
        }

        // Task 6: Wait for all background jobs
        else if (strcmp(first_token, "wait-all") == 0) {
            if (await_all_background_jobs(&jobs) == 1) {
                printf("Failed to wait for all background jobs\n");
            }
        }

        else {
            int exit_code = 0;
            pid_t child_pid = fork();
            if (child_pid < 0) // error
            {
                perror("fork");
                strvec_clear(&tokens);
                job_list_free(&jobs);
                return 1;
            }
            else if (child_pid == 0) // child
            {
                //printf("forked, calling run_command for '%s'\n", strvec_get(&tokens, 0)); //(commented out because failing tests)
                if (run_command(&tokens) == 1)
                {
                    strvec_clear(&tokens);
                    job_list_free(&jobs);
                    return 1; // don't want an additional shell process running, terminate child
                    // NOTE: If this if statement doesn't execute, there won't be a duplicate
                    //        because run_command doesn't return on success
                }
                strvec_clear(&tokens);
                job_list_free(&jobs);
                return 0; // just in case tho
            }
            else if (child_pid > 0) // parent
            {
                if (strvec_find(&tokens, "&") == -1) {//child runs in fg
                    //set child group to be in foreground.
                    if (tcsetpgrp(STDIN_FILENO, child_pid) == -1)
                    {
                        perror("tcsetpgrp");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }

                    if (waitpid(child_pid, &exit_code, WUNTRACED) == -1)
                    { // we need WUNTRACED for it now that we're getting signals that stop the child.
                        perror("waitpid");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                    //printf("child exited with code %d", exit_code);

                    // WUNTRACED stops if a child is stopped. WIFSTOPPED returns 1 if it stopped from a ginal, otherwise 0
                    if (WIFSTOPPED(exit_code) == 1)
                    {
                        if (job_list_add(&jobs, child_pid, first_token, JOB_STOPPED) == 1) {
                            fprintf(stderr, "failed to add job");
                            strvec_clear(&tokens);
                            job_list_free(&jobs);
                            return 1;
                        }
                    }

                    //set parent process back to foreground
                    if (tcsetpgrp(STDIN_FILENO, getpid()) == -1)
                    {
                        perror("tcsetpgrp");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                }
                else {//child as bg job for it, don't wait, don't set group.
                    if (job_list_add(&jobs, child_pid, first_token, JOB_BACKGROUND) == 1) {
                        fprintf(stderr, "failed to add job");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                }
            }
        }

        strvec_clear(&tokens);
        printf("%s", PROMPT);
    }
    job_list_free(&jobs);
    return 0;
}
