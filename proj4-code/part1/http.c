#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    char buf[BUFSIZE]; // here is our buffer
    // Start by reading the entirety of the request (likely) into a char* (buf)
    int nbytes = read(fd, buf, BUFSIZE);
    if (nbytes < 0) {
        perror("read");
        return 1;
    } else if (nbytes == 0) {
        fprintf(stderr, "empty HTTP request\n");
        return 1;
    }
    
    // Make a copy of buf so we can iterate through it by modifying it without
    // losing the original HTTP request
    char *buf_copy = malloc(sizeof(buf)); // use pointer so we can iterate easily
    char *pointer_to_free = buf_copy; // when buf_copy gets incremented we lose track of what to free
    strncpy(buf_copy, buf, nbytes);

    // find the start of the file name
    while (strncmp(buf_copy, "/", 1) != 0)
    {
        if (strncmp(buf_copy, "\r", 1) == 0) {
            fprintf(stderr, "no file name specified on first line of request\n");
            free(pointer_to_free);
            return 1;
        }
        // iterate to next character
        buf_copy++;
    }

    // copy the file name into resource_name
    //buf_copy++; // skip past '/'
    int i = 0; // used to keep track of the position of resource_name we're copying to
    while (strncmp(buf_copy, " ", 1) != 0 && strncmp(buf_copy, "\r", 1) != 0)
    {
        strncpy(resource_name+i, buf_copy, 1);
        i++;
        buf_copy++; // iterate to next character to copy over
    }
    if (strncmp(buf_copy, "\r", 1) == 0) { // we can take this out later but it might help debugging
        fprintf(stderr, "HTML request not formatted properly\n"); //eh it seems like an error that a poorly coded client might run into, let's keep it.
        free(pointer_to_free);
        return 1;
    }
    *(resource_name+i) = '\0'; // terminate with null character

    // read the rest of the request (if there is anything left) because that's
    // what the project specification said to do
    //(it also clears stuff to read in the next request from the client, I think, which is why it's in the spec)
    if (nbytes != BUFSIZE) {
        // printf("nothing left to read\n");
        free(pointer_to_free);
        return 0;
    }
    while ((nbytes = read(fd, buf, BUFSIZE)) > 0)
    {
        // do nothing with it
        //added an error check in here too just to be on the safe side, since one read might fail badly and then a later one might not.
        //not sure how that'd work but better to be safe.
        if (nbytes == -1) {
            perror("read");
            free(pointer_to_free);
            return 1;
        }
    }
    if (nbytes == -1) {
        perror("read");
        free(pointer_to_free);
        return 1;
    }

    free(pointer_to_free);
    return 0;
}


// Returns size of resource_path on success, -1 if resource_path doesn't exist, or -2 on other error
int get_file_size(const char *resource_path) {
    struct stat stat_buf;
    if (stat(resource_path, &stat_buf) == -1) {
        if (errno == ENOENT) {
            fprintf(stderr, "file doesn't exist\n"); // SHOULD SEND BACK 404, JUST PUT THIS HERE FOR NOW
            return -1;
        }
        else {
            perror("stat");
            return -2;
        }
    }
    return stat_buf.st_size;
}

// Returns the file type of resource_path (e.g. application/pdf, text/plain) on success and "\0" on error
const char *get_file_type(const char *resource_path) {
    int resource_length = strlen(resource_path);
    char *file_extension = (char *) (resource_path + resource_length);

    int i = 0; // make sure this doesn't loop infinitely
    while (strncmp(file_extension,".",1) != 0 && i <= resource_length) {
        file_extension--; // iterate until it points to the last '.', e.g. ".txt\0" from "a.b.c.d.txt\0"
        i++;
    }
    if (i > resource_length) {
        fprintf(stderr, "resource_path %s doesn't have a file type\n", resource_path); //print to stderr so serverside knows what's going on.
        return "unknown"; //content type is unknown. better to plan to deal with this case than be taken by surprise if someone requests a makefile
    }

    if ((strncmp(file_extension, ".txt", 1) != 0) || (strncmp(file_extension, ".html", 1) != 0) || (strncmp(file_extension, ".jpg", 1) != 0) || (strncmp(file_extension, ".png", 1) != 0)
        || (strncmp(file_extension, ".pdf", 1) != 0)) {
        fprintf(stderr, "file type not supported");
        return "file type not supported";
    }

    return get_mime_type(file_extension);
}

int write_http_response(int fd, const char *resource_path) {
    int file_exists = 1;
    int file_size = get_file_size(resource_path);
    if (file_size == -2) { // 
        return 1; // error already printed
    } else if (file_size == -1) {
        file_exists = 0; // still send back 404
    }

    const char *file_type;
    if (file_exists) { // only get file type if file exists
        file_type = get_file_type(resource_path);
        if (strcmp(file_type, "\0") == 0) {return 1;} // no "." found in resource_path, error already printed
    }

    char header[BUFSIZE] = "HTTP/1.0 ";
    // char next_line[BUFSIZE] = "";
    if (file_exists && (strcmp(file_type, "file type not supported") != 0)) {
        // add "200 OK\r\n" to header
        strncat(header, "200 OK\r\n", 9);
        // add "Content-Type: file_type\r\n" to header
        sprintf(header + strlen(header), "Content-Type: %s\r\n", file_type);
        // add "Content-Length: file_size\r\n" to header
        sprintf(header + strlen(header), "Content-Length: %d\r\n", file_size);
    }
    else if(strcmp(file_type, "file type not supported") == 0){ //handling files like .c, .h, a.out
        //file type not supported. project documentation didn't tell us to do this BUT we should anyways
        strncat(header, "415 Unsupported Media Type\r\n", 29); //add "415 Unsupported Media Type" to header
        strncat(header, "Content-Length: 0\r\n", 20); //content length 0 returned to client since this is an unsupported media type.
    }
    
    else {
        // add "404 Not Found\r\n" to header
        strncat(header, "404 Not Found\r\n", 16);
        // add "Content-Length: 0\r\n" to header
        strncat(header, "Content-Length: 0\r\n", 20);
    }
    // add "\r\n" to header
    strncat(header, "\r\n", 3);
    //printf("%s", header); used for debugging

    //write header.
    if (write(fd, header, strlen(header)) == -1) { //write the header to the socket
        perror("write");
        return 1;
    }

    if (!file_exists || (strcmp(file_type, "file type not supported") == 0)) {return 0;} // 404 not found is done here, should we return 0 or 1? (probably 0, it succeeded.)
    //415 unsupported media type is ALSO done here, added it to the if.

    // write the file to fd in a loop

    int target_file = open(resource_path, O_RDONLY);
    if (target_file == -1) {
        perror("open");
        return 1;
    }

    void *buf = malloc(sizeof(void *) * BUFSIZE); // buffer to read from target_file and write to fd
    int nbytes; // to store how many bytes are read from target_file
    while ((nbytes = read(target_file, buf, BUFSIZE)) > 0){//know it'll be > 0 at first because know the target_file exists

        if (write(fd, buf, nbytes) == -1) { // write the same amount of bytes that was read from target_file
            perror("write");
            free(buf);
            close(target_file);
            return 1;
        }
    }
    if (nbytes < 0) { // If loop breaks because read returned an error
        perror("read");
        free(buf);
        close(target_file);
        return 1;
    }

    free(buf); // done reading and writing, don't need this anymore

    if (close(target_file) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}

//int main(int argc, char *argv[]) {
    // READ_HTTP_REQUEST TEST
    // int fd = open("GET.txt", O_RDONLY | O_CREAT);
    // if (fd == -1) {perror("open");return 1;}
    // char *resource_name = malloc(BUFSIZE);
    // read_http_request(fd, resource_name);
    // printf("%s\n", resource_name);
    // close(fd);
    // free(resource_name);
    // return 0;

    // GET FILE TYPE TEST
    // const char *testing = "test.extension.stripping.txt";
    // printf("%s\n", get_file_type(testing));
    // testing = "test.extension.stripping.html";
    // printf("%s\n", get_file_type(testing));
    // testing = "test.extension.stripping.jpg";
    // printf("%s\n", get_file_type(testing));
    // testing = "test.extension.stripping.png";
    // printf("%s\n", get_file_type(testing));
    // testing = "test.extension.stripping.pdf";
    // printf("%s\n", get_file_type(testing));
    // testing = "test_extension_stripping"; // no "."
    // printf("%s\n", get_file_type(testing));

    // GET FILE SIZE TEST
    // printf("%d\n", get_file_size("GET.txt"));
    // printf("%d\n", get_file_size("non_existent.blah"));
    // printf("%d\n", get_file_size("Makefile"));

    // WRITE_HTTP_RESPONSE TEST
  //  char *file_names[4] = {"GET.txt", "server_files/africa.jpg", "server_files/gatsby.txt", "non_existent.txt"};
 //   for (int i = 0; i < 4; i++) {
 //       printf("Header generated for %s:\n", file_names[i]);
//        write_http_response(2, file_names[i]);
//    }
    // doesn't work with ".c" files, ".h" files, or "a.out"
    //should now work with .c, .h, and a.out files insofar as letting the client know they're not supported
//}