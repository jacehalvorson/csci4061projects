#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"
#include "connection_queue.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

typedef struct {
    connection_queue_t *queue;
    const char *server_dir;
} args_t;

int keep_going = 1;
int code = 0; //used to hold return valuefor main

void handle_sigint(int signo) { //rewrite for queues.
    keep_going = 0;
}

// THREAD FUNCTION
void* respond(void* details) {
    // printf("respond entered\n"); // debugging
    int exit_code = 0;
    while (keep_going) { // Thread will repeatedly pick up connections from queue until server shutdown
        args_t *args = (args_t *)details;
        int client_fd;
        if ((client_fd = connection_dequeue(args->queue)) == -1) {
            fprintf(stderr, "connection_dequeue failed\n");
            exit_code = 1;
            pthread_exit(&exit_code);
        }
        // printf("keep_going = %d\n", keep_going); // debugging
        if (!keep_going) { // check if server has been shut down because this thread has been waiting
            close(client_fd);
            // printf("thread exiting\n");
            pthread_exit(&exit_code); // Succesfully shut down, exit code 0
        }
        // printf("client fd = %d\n", client_fd); // debugging

        //do stuff with http requests while connected to client.
        int http_ret; //return value for read_http_request
        char resource[BUFSIZE]; //probably won't get a file path longer than bufsize long?
        if ((http_ret = read_http_request(client_fd, resource)) == 0) { //1 is error value for http_read_request

            //http req read in, convert what's stored in resource to a proper file path for use in write_http_response
            char temp[BUFSIZE]; //temp to store rest of file path so "/server_files" can be appended to the start of resource.
            strncpy(temp, resource, strlen(resource) + 1);
            strncpy(resource, args->server_dir, strlen(args->server_dir) + 1);
            strncat(resource, temp, strlen(temp) + 1); // serve_dir is directory to serve, the "/" should be part of what read_http_request returns

            //printf("%s", resource); //debugging

            //write result
            if (write_http_response(client_fd, resource) == 1) {//write error
                fprintf(stderr, "http write failure\n");
                close(client_fd); //close since can't write to it
                exit_code = 1;
                pthread_exit(&exit_code); // returns void *
            }

            close(client_fd);
            // printf("client closed\n"); // debugging
        } //end read_http_request
        else { // read_http_request failed
            fprintf(stderr, "read_http_request failed\n");
            close(client_fd);
            exit_code = 1;
            pthread_exit(&exit_code);
        }
    } // end while (keep_going)

    printf("thread exiting\n");
    pthread_exit(&exit_code);
}

int main(int argc, char** argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }
    // Uncomment the lines below to use these definitions:
    const char* server_dir = argv[1]; //directory to serve
    const char* port = argv[2]; //port to bind to

    //install sigint handler before starting setup (similar to in-class example) for tcp server
    struct sigaction sact;
    sact.sa_handler = handle_sigint;
    if (sigfillset(&sact.sa_mask) == -1) { //didn't error check in in-class example but we probably should since there's an error return val
        perror("sigfillset"); //errno is set so perror.
        return 1; //not in server loop yet so we can just terminate.
    }
    sact.sa_flags = 0; //don't want to restart system calls here- more detail on why in project writeup. thus, no SA_RESTART
    if (sigaction(SIGINT, &sact, NULL) == -1) { //also want to error check this to make sure handler installed correctly
        perror("sigaction");
        return 1; //still terminate relatively normally because not in server loop yet. no cleanup (yet).
    }

    //malloc and init queue
    connection_queue_t* q = malloc(sizeof(connection_queue_t));
    if (q == NULL) {
        perror("malloc");
        return 1;
    }
    if (connection_queue_init(q) == -1) {
        fprintf(stderr, "queue initialization failed");
        free(q);
        return 1;
    }

    // CREATE NEW THREADS FOR RESPONDING TO REQUESTS
    // Block signals in all threads (signal masks get inherited)
    sigset_t sigset;
    sigset_t oldset;
    if (sigfillset(&sigset) != 0) {
        perror("sigfillset");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        return 1;
    }
    if (sigprocmask(SIG_BLOCK, &sigset, &oldset) != 0) {
        perror("sigprocmask");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        return 1;
    }

    int err_code = 0;
    pthread_t threads[N_THREADS]; 
    args_t details; // Thread arguments, they're the same for every thread
    // populate details with thread args
    details.queue = q;
    details.server_dir = server_dir;
    for (int i = 0; i < N_THREADS; i++) {
        if ((err_code = pthread_create(threads + i, NULL, respond, &details)) != 0) {
            fprintf(stderr, "pthread_create: %s", strerror(err_code));
            connection_queue_shutdown(q);
            connection_queue_free(q);
            return 1;
        }
    }

    // Unblock all signals in the parent
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL)) {
        perror("sigprocmask");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        return 1;
    }
    // Block the signals that were previously blocked
    if (sigprocmask(SIG_BLOCK, &oldset, NULL)) {
        perror("sigprocmask");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        return 1;
    }
    // end creating thread


    //set up hints for getaddrinfo()- remember! server rather than client; use tcp
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints)); //set all fields to 0
    hints.ai_family = AF_UNSPEC; //either ipv4 or ipv6
    hints.ai_socktype = SOCK_STREAM; //tcp connection for a http server.
    hints.ai_flags = AI_PASSIVE; //magical girl transformation into a server goes here via setting this flag.
    struct addrinfo* server; //actual addrinfo construct to populate if I remember correctly.

    //getaddrinfo() call to make our life easier later
    int ret_val = getaddrinfo(NULL, port, &hints, &server); //not sure why we need a double ptr to server, but *shrugs*
    if (ret_val != 0) { //error, setup failed.
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret_val)); //pretty much only need ret_val as a glorified errno, but we do need it.
        connection_queue_shutdown(q);
        connection_queue_free(q);
        return 1; //still not a ton of cleanup because we failed in setup.
    }

    //set up socket
    int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sock_fd == -1) {//socket setup failed
        perror("socket");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        freeaddrinfo(server); //freeaddrinfo MUST be done, similar to free with malloc
        return 1;
    }

    //bind socket to receive at specific port so clients know where to connect.
    if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {//more error handling yay \o/
        perror("bind");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        freeaddrinfo(server);
        close(sock_fd); //don't need to have nested error branches for the sake of projects. a real server might have one but we don't need it.
        return 1;
    }
    freeaddrinfo(server); //don't need server's addrinfo now that we're set up.

    //designate socket as server socket
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) { //LISTEN_QUEUE_LEN is the num clients that can be kept waiting for a server connection I think
        perror("listen");
        connection_queue_shutdown(q);
        connection_queue_free(q);
        close(sock_fd);
        return 1;
    }

    while (keep_going) { //server loop for receiving and servicing requests
        //wait to receive a req from a client; don't bother saving client address info because this is tcp and we have an active connection
        // printf("Waiting for a client to connect\n"); //not strictly necessary, but might be nice for debugging later or for matching test output >_>
        int client_fd = accept(sock_fd, NULL, NULL); //NULLs since we don't need to save address info
        if (client_fd == -1) {
            if (errno != EINTR) {
                perror("accept"); //not much use having a server that can't accept clients, so clean up and terminate upon accept error
                close(sock_fd);
                code = 1;
                break; // let clean up happen instead of returning
            }
            else {
                break; //SIGINT received, terminating loop to shut down server.
            }
        }
        if (connection_enqueue(q, client_fd) == -1) {
            fprintf(stderr, "connection_enqueue failed\n");
            code = 1;
            break;
        }

    }//end accept loop


    // printf("cleanup\n"); // debugging
    //cleanup; reached even if sigint thanks to our handler.
    // even if one fails, we should clean up the rest
    if (connection_queue_shutdown(q) != 0) {
        fprintf(stderr, "connection_queue_shutdown failed\n");
        code = 1;
    }
    // wait for all threads before destroying mutex variables and freeing memory
    for (int i = 0; i < N_THREADS; i++) {
        // printf("waiting on thread %d\n", i); // debugging
        if ((err_code = pthread_join(threads[i], NULL)) != 0) {
            fprintf(stderr, "pthread_join: %s", strerror(err_code));
            code = 1; // error return val
        }
    }
    // printf("done waiting for threads\n"); // debugging
    if (connection_queue_free(q) != 0) {
        fprintf(stderr, "connection_queue_free failed\n");
        code = 1;
    }

    if (close(sock_fd) == -1) {
        perror("close");
        code = 1; //error return val
    }

    return code;
}