#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WORD_LEN 25

/*
 * Counts the number of occurrences of words of different lengths in a text file and
 * stores the results in an array.
 * file_name: The name of the text file from which to read words
 * counts: An array of integers storing the number of words of each possible length.
 *         counts[0] is the number of 1-character words, counts [1] is the number
 *         of 2-character words, and so on.
 * max_len: The maximum length of any word, and also the length of 'counts'.
 * Returns 0 on success or 1 on error.
 */
int count_word_lengths(const char *file_name, int *counts, int max_len) {
    FILE *f = fopen(file_name, "r");
    if (f == NULL)
    {
        perror("fopen");
        return 1;
    }

    int count = 0;
    char current = 0;
    while (fread(&current, sizeof(char), 1, f) > 0)
    // If fread returns 0, it's either EOF or an error. Error
    // checking is done after the loop using ferror()
    {
        if (isspace(current))
        { // Update the "count-1"th array entry and start a new count to
          // how long the next word is
            counts[count-1]++;
            count = 0;
        }
        else
        { // Still a part of the same word, it has one more character (at least)
            count++;
        }
    }
    if (ferror(f) != 0)
    {
        printf("fread");
        fclose(f); //don't need to error check close in error branches
        return 1;
    }

    if (fclose(f) == EOF) {
        perror("fclose");
    }
    return 0;
}

/*
 * Processes a particular file (counting the number of words of each length)
 * and writes the results to a file descriptor.
 * This function should be called in child processes.
 * file_name: The name of the file to process.
 * out_fd: The file descriptor to which results are written
 * Returns 0 on success or 1 on error
 */
int process_file(const char *file_name, int out_fd) {

    int counts[MAX_WORD_LEN]; //count of each word length in int array. This'll get written to the pipe.
    memset(counts, 0, MAX_WORD_LEN * sizeof(int)); //initalize counts to 0.

    if (count_word_lengths(file_name, counts, MAX_WORD_LEN) != 0) {
        fprintf(stderr, "count_word_lengths\n");
        return 1; //error. doesn't close pipe in this one bc it will be closed after return
    }
    //write to pipe + error check for that.
    write(out_fd, counts, MAX_WORD_LEN * sizeof(int));
    //error checking
    //don't need to close here bc closed in main().
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        // No files to consume, return immediately
        return 0;
    }
    //don't need a file name (I think) bc can get from args?
    int* pipe_fd = malloc(2 * (argc-1) * sizeof(int)); //allocate pipe space. we don't need to errorcheck malloc right?
    for (int i = 0; i < argc-1; i++) {
        pipe(&pipe_fd[2 * i]);
        //error check pipe creation.
        pid_t child_pid = fork(); 
        //error check fork.
        if (child_pid == 0) {//child
            if (close(pipe_fd[2 * i]) == -1) {
                perror("close");
            } // dont need to read in child

            if (process_file(argv[i + 1], pipe_fd[2 * i + 1]) != 0) { //error checking. if this branch doesn't trigger it worked
                fprintf(stderr, "failed to process file\n");
                if (close(pipe_fd[2 * i + 1]) == -1) {
                    perror("close");
                }//error check close?
                exit(1); //error code?
            }
            if (close(pipe_fd[2 * i + 1]) == -1) {
                perror("close");
            } //may need to error check.
            exit(0); //success otherwise. process_file writes to pipe_fd.
        }
        else {//parent; would have error checked first.
            if (close(pipe_fd[2 * i + 1]) == -1) {
                perror("close");
            } // parent doesn't need to write
        }
    }

    // After for loop that created children, aggregate results
    int exit_code;
    int counts[MAX_WORD_LEN];
    int temp_counts[MAX_WORD_LEN];
    memset(counts, 0, MAX_WORD_LEN * sizeof(int));
    memset(temp_counts, 0, MAX_WORD_LEN * sizeof(int));
    for (int i = 0; i < argc-1; i++)
    {
        wait(&exit_code);
        if (exit_code != 0) // check if child fails
        {
            printf("child process failed\n");
            break; // Break out of the loop and still print (according to test 4)
        }
        // now that this child is done (waited for good practice), read from pipe
        if (read(pipe_fd[2*i], temp_counts, MAX_WORD_LEN * sizeof(int)) == -1)
        {
            perror("read");
            close(pipe_fd[2*i]);  //don't need to error check in error checks.
            return 1;
        }
        // temp_counts holds the counts from file i (processed by child i),
        // go through each entry and add to total (counts)
        for (int i = 0; i < MAX_WORD_LEN; i++)
        {
            counts[i] += temp_counts[i];
        }
        if (close(pipe_fd[2 * i]) == -1) {
            perror("close");
        }
        // Got the info, don't need to read from this child anymore
    }

    // print out results
    for (int i = 1; i <= MAX_WORD_LEN; i++) {
        printf("%d-Character Words: %d\n", i, counts[i-1]);
    }

    free(pipe_fd);
    return 0;
}
