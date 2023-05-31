#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5

int keep_going = 1;

void handle_sigint(int signo) { //we'll need to write cleanup code for when keep_going = 0 in main() but this ensures cleanup happens.
    keep_going = 0;
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }
    // Uncomment the lines below to use these definitions:
     const char *serve_dir = argv[1]; //directory to serve
     const char *port = argv[2]; //port to bind to

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
         return 1; //still not a ton of cleanup because we failed in setup.
     }

     //set up socket
     int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
     if (sock_fd == -1) {//socket setup failed
         perror("socket");
         freeaddrinfo(server); //freeaddrinfo MUST be done, similar to free with malloc
         return 1;
     }

     //bind socket to receive at specific port so clients know where to connect.
     if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {//more error handling yay \o/
         perror("bind");
         freeaddrinfo(server);
         close(sock_fd); //don't need to have nested error branches for the sake of projects. a real server might have one but we don't need it.
         return 1;
     }
     freeaddrinfo(server); //don't need server's addrinfo now that we're set up.

     //designate socket as server socket
     if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) { //LISTEN_QUEUE_LEN is the num clients that can be kept waiting for a server connection I think
         perror("listen");
         close(sock_fd);
         return 1;
     }

     while (keep_going != 0) { //server loop for receiving and servicing requests
         //wait to receive a req from a client; don't bother saving client address info because this is tcp and we have an active connection
         printf("Waiting for a client to connect\n"); //not strictly necessary, but might be nice for debugging later or for matching test output >_>
         int client_fd = accept(sock_fd, NULL, NULL); //NULLs since we don't need to save address info
         if (client_fd == -1) {
             if (errno != EINTR) {
                 perror("accept"); //not much use having a server that can't accept clients, so clean up and terminate upon accept error
                 close(sock_fd);
                 return 1;
             }
             else {
                 break; //SIGINT received, terminating loop to shut down server.
             }
         }

         //do stuff with http requests while connected to client.
         int http_ret; //return value for read_http_request
         char resource[BUFSIZE]; //probably won't get a file path longer than bufsize long?
         if ((http_ret = read_http_request(client_fd, resource)) == 0) { //1 is error value for http_read_request

             //http req read in, convert what's stored in resource to a proper file path for use in write_http_response
             char temp[BUFSIZE]; //temp to store rest of file path so "/server_files" can be appended to the start of resource.
             strncpy(temp, resource, strlen(resource)+1);
             strncpy(resource, serve_dir, strlen(serve_dir)+1);
             strncat(resource, temp, strlen(temp)+1); // serve_dir is directory to serve, the "/" should be part of what read_http_request returns

             //printf("%s", resource); //debugging

             //write result
             if (write_http_response(client_fd, resource) == 1) {//write error
                 fprintf(stderr, "http write failure\n");
                 close(client_fd); //close since can't write to it
                 close(sock_fd);
                 return 1;
             }

             close(client_fd);
             
         } //end client r/w loop
         if (http_ret == 1) { // read_http_request failed
            fprintf(stderr, "read_http_request failed\n");
            close(sock_fd);
            close(client_fd);
            return 1;
         }

     }//end accept loop

    //cleanup; reached even if sigint thanks to our handler.
     if (close(sock_fd) == -1) {
         perror("close");
         return 1; //error return val
     }

    return 0;
}
