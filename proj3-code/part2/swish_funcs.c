#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string.h>

#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

/*
 * Helper function to close all file descriptors in an array. You are
 * encouraged to use this to simplify your error handling and cleanup code.
 * fds: An array of file descriptor values (e.g., from an array of pipes)
 * n: Number of file descriptors to close
 */
int close_all(int *fds, int n) {
    int ret_val = 0;
    for (int i = 0; i < n; i++) {
        if (close(fds[i]) == -1) {
            perror("close");
            ret_val = 1;
        }
    }
    return ret_val;
}

// Function to help with debugging
// prints out every string in a string vector
void print_all_strings(strvec_t* vec)
{
    char *token;
    int i = 0;
    token = strvec_get(vec, 0);
    while (token != NULL)
    {
        printf("%s, ", token);
        i++;
        token = strvec_get(vec, i);
    }
    printf("\n");
}

// Function to help with debugging
// prints out all elements in a pipe_fd array
void print_pipe_fds(int *pipes, int npipes)
{
    for (int i = 0; i < 2 * npipes; i++)
    {
        printf("%d, ", pipes[i]);
    }
    printf("\n");
}

/*
 * Helper function to run a single command within a pipeline. You should make
 * make use of the provided 'run_command' function here.
 * tokens: String vector containing the tokens representing the command to be
 * executed, possible redirection, and the command's arguments.
 * pipes: An array of pipe file descriptors.
 * n_pipes: Length of the 'pipes' array
 * in_idx: Index of the file descriptor in the array from which the program
 *         should read its input, or -1 if input should not be read from a pipe.
 * out_idx: Index of the file descriptor int he array to which the program
 *          should write its output, or -1 if output should not be written to
 *          a pipe.
 * Returns 0 on success or 1 on error.
 */
int run_piped_command(strvec_t *tokens, int *pipes, int n_pipes, int in_idx, int out_idx) {
    // printf("running command with indexes from %d to %d\n", in_idx, out_idx); debugging
    for (int i = 0; i < n_pipes * 2; i++)
    {
        if (i != in_idx && i != out_idx) // close all pipes besides the two we need
        {   // If we only need one of the indexes we can close everything except that one
            // printf("closing pipes[%d], fd = %d\n", i, pipes[i]); debugging
            if (close(pipes[i]) == -1)
            {
                perror("close"); // dont return to keep the loop closing the other pipes
            }
        }
        // else {printf("leaving pipes[%d] open, fd = %d\n", i, pipes[i]);} debugging
    }

    if (in_idx != -1) { // If it's not the first command, redirect the input to a pipe
        // printf("calling dup2 on pipes[%d], fd = %d (input)\n", in_idx, pipes[in_idx]);
        if (dup2(pipes[in_idx], STDIN_FILENO) == -1) {
            perror("dup2");
            // close pipes that are still open
            if (close(pipes[in_idx]) == -1)
            {
                perror("close");
            }
            if (out_idx != -1) {
                if (close(pipes[out_idx]) == -1) {
                    perror("close");
                }

            } // make sure not to double close
            return 1;
        }
        // printf("closing pipe %d\n", in_idx);
        // if (close(pipes[in_idx]) == -1) {
        //     perror("close");
        //     return 1;
        // }
    }

    if (out_idx != -1) { // If it's not the last command, redirect the output to a pipe
        // printf("calling dup2 on pipes[%d], fd = %d (output)\n", out_idx, pipes[out_idx]);
        if (dup2(pipes[out_idx], STDOUT_FILENO) == -1) {
            perror("dup2");
            // close pipes that are still open
            if (close(pipes[out_idx]) == -1) {
                perror("close");
            }
            // in_idx has already been closed
            return 1;
        }
        // printf("closing pipe %d\n", out_idx);
        // if (close(pipes[out_idx]) == -1) {
        //     perror("close");
        //     return 1;
        // }
    }
    
    // printf("call to run_command\n"); debugging
    if (run_command(tokens) != 0) {
        fprintf(stderr, "run_command failed\n");
        if (out_idx != -1) {
            if (close(pipes[out_idx]) == -1) {
                perror("close");
            }
        } // If there is no out_idx, it's already closed
        if (in_idx != -1) {
            if (close(pipes[in_idx]) == -1) {
                perror("close");
            }
        } // If there is no in_idx, it's already closed
        return 1;
    }
    
    // This is never reached
    return 0;
}

int run_pipelined_commands(strvec_t *tokens) {
    // TODO Complete this function's implementation
    int npipes;
    if ((npipes = strvec_num_occurrences(tokens, "|")) == 0) {
        printf("No pipelines are required\n");
        return 1;
    }
    int *pipes = malloc(sizeof(int) * 2 * npipes);
    for (int i = 0; i < npipes; i++) { //set up pipes.
        if (pipe(&pipes[i * 2]) == -1) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                if (close(pipes[j * 2]) == -1) {
                    perror("close");
                }
                if (close(pipes[j * 2 + 1]) == -1) {
                    perror("close");
                }
            }
            free(pipes);
            return 1;
        }
    }
    // print_pipe_fds(pipes, npipes); debugging

    //used to calculate beginning and end of commands in tokens in following loop
    int start = 0; // index of beginning of command i in tokens
    int end = 0; // index of the next "|" occurence in tokens
    strvec_t* command = malloc(sizeof(strvec_t)); // just initializing it to something so it compiles

    //fork child processes + use run_piped_command
    for (int i = 0; i < npipes + 1; i++) {
        //grabbing the command to send to the child process, so it can be inherited upon fork()
        strvec_init(command);
        char* token = ""; // initialize to something non-null, used to iterate through tokens

        while (end < tokens->length-1 && strcmp(token, "|") != 0) { //iterate through tokens until encountering the next "|" token
            end++;
            token = strvec_get(tokens, end); // iterate to next token
            if (token == NULL) {
                fprintf(stderr, "strvec_get failed\n");
                close_all(pipes, 2 * npipes);
                free(pipes);
                strvec_clear(command);
                free(command);
                return 1;
            }
        }

        if (end == tokens->length-1) {end++;} // special case for last token in last command (end=length to include last token)
        if (strvec_slice(tokens, command, start, end) == 1) {//end isn't inclusive
            fprintf(stderr, "strvec_slice failed\n");
            close_all(pipes, 2 * npipes);
            free(pipes);
            strvec_clear(command);
            free(command);
            return 1;
        }
        // print_all_strings(tokens);
        // printf("child %d: ", i);
        // print_all_strings(command);
        start = end + 1; //setting up for next command if applicable.
        end = start;

        pid_t child_pid = fork();
        if (child_pid == -1) {//error
            perror("fork");
            close_all(pipes, 2 * npipes);
            free(pipes);
            strvec_clear(command);
            free(command);
            return 1;
        }
        else if (child_pid == 0) {//child
            //prep stuff for sending to run_piped_command
            // printf("forked child %d\n", i); debugging
            int in_index = 2 * (i-1); // should read from previous pipe, because that's where last command wrote to
            int out_index = 2 * i + 1;
            if (i == 0) {in_index = -1;} // first command shouldn't redirect input
            if (i == npipes) {out_index = -1;} // last command shouldn't redirect output

            if (run_piped_command(command, pipes, npipes, in_index, out_index) == 1) {
                // pipes already closed in run_piped_command
                exit(1);
            }

            // memory is freed in parent outside of loop, no need to free here
            if (out_index != -1) {
                if (close(pipes[out_index]) == -1) {
                    perror("close");
                }
            } // If there is no out_idx, it's already closed
            if (in_index != -1) {
                if (close(pipes[in_index]) == -1) {
                    perror("close");
                }
            } // If there is no in_idx, it's already closed
            exit(0); // everything went well
        }
        // parent doesn't do anything
    }
    close_all(pipes, npipes * 2); // parent doesn't need any of these ERROR CHECK

    // commands starting running, now wait for them and check for errors
    // int status;
    for (int i = 0; i < npipes + 1; i++) // ncommands = npipes + 1, indexing starts at 0 as per usual
    {
        // printf("waiting for child %d\n", i); 
        wait(NULL);
        // if (WEXITSTATUS(status) != 0)
        // {
        //     // fprintf(stderr, "child exited with code %d\n", status);
        //     free(pipes);
        //     strvec_clear(command);
        //     free(command);
        //     return 1;
        // }
    }


    free(pipes);
    strvec_clear(command);
    free(command);
    return 0;
}
