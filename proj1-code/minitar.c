#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "minitar.h"

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 512

int update_archive(const char *archive_name, file_list_t *files);
int file_list_no_print(const char* archive_name, file_list_t* files);
int cursorToNextHeader(tar_header *current, FILE *archive);
int sizeFromOctal(char *digits);

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *)header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%7o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or 1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return 1;
    }

    strncpy(header->name, file_name, 100); // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%7o", stat_buf.st_mode & 07777); // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%7o", stat_buf.st_uid); // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid); // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return 1;
    }
    strncpy(header->uname, pwd->pw_name, 32); // Owner  name of the file, null-terminated string

    snprintf(header->gid, 8, "%7o", stat_buf.st_gid); // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid); // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return 1;
    }
    strncpy(header->gname, grp->gr_name, 32); // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%11o", (unsigned)stat_buf.st_size); // File size, 0-padded octal
    snprintf(header->mtime, 12, "%11o", (unsigned)stat_buf.st_mtime); // Modification time, 0-padded octal
    header->typeflag = REGTYPE; // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6); // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2); // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%7o", major(stat_buf.st_dev)); // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%7o", minor(stat_buf.st_dev)); // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
* Removes 'nbytes' bytes from the file identified by 'file_name'
* Returns 0 upon success, -1 upon error
* Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
*/
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];
    // Note: ftruncate does not work with O_APPEND
    int fd = open(file_name, O_WRONLY);
    if (fd == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to open file %s", file_name);
        perror(err_msg);
        return 1;
    }
    //  Seek to end of file - nbytes
    off_t current_pos = lseek(fd, -1 * nbytes, SEEK_END);
    if (current_pos == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to seek in file %s", file_name);
        perror(err_msg);
        close(fd);
        return 1;
    }
    // Remove all contents of file past current position
    if (ftruncate(fd, current_pos) == -1) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}


// Code from this point on is written by
// Jace Halvorson and
// Skylar Volden


int create_archive(const char *archive_name, const file_list_t *files) {
    char buf[BLOCK_SIZE]; //buffer, gets set to 512 bytes worth of random memory.

    //unsure if we actually need to check if archive file exists- "w" mode creates/opens a file from the beginning, and we're overwriting any existing file.
    //if we do, might print "WARNING: Overwriting existing file" or sth?
    FILE *archive = fopen(archive_name, "w");

    //WHILE loop- dependent on file list.
    node_t  *cur = files->head;
    while (cur != NULL) {
        //open file in read mode
        FILE *src = fopen(cur->name, "r");

        //create and fill header, hopefully.
        tar_header *head;
        head = malloc(sizeof(tar_header)); //unsure what else to do besides malloc; the heap is. a bit
        fill_tar_header(head, cur->name);
        
        //write the head to the file now, probably.
        fwrite(head,sizeof(tar_header),1,archive); //this should write the head to the file.
        if (ferror(archive) != 0) {
            printf("Failed to write file header to archive");
            fclose(src);
            fclose(archive);
            free(head);
            return 1;
        }

        struct stat stat_buf; //set up a stat buffer that we'll only grab the size from. this is to set up the loop that'll show up in a few lines.
        stat(cur->name, &stat_buf); //according to the header function, stat is a system call to inspect file metadata. neat!
        int src_size = stat_buf.st_size; //this is what we actually want. could drop an error check here to make sure that this is a positive number *shrug*

        //buffer will contain 512 bytes of data; FULL BUFFER must always be written to tar so it's 512-byte blocks, even if that's mostly 0s.
        //probably set buf to all 0's first, then fread in and then fwrite the entire buffer. for loop for the case of files that are greater than 512 bytes.

        for (int i = 0; i < ceil(src_size/512.0); i++) { //ceiling of file size/512. (is file size from stat in bytes? It'd better be.)
                //set buf to all 0, using memset. this way if the file/file piece is less than 512 bytes, it'll be nice and fill the rest with 0 automatically.
                // Using 512.0 to perform float division

                memset(buf, 0, BLOCK_SIZE);
                //now read to the buffer; any bytes that are NOT filled will remain zeroes I believe.
                fread(buf, BLOCK_SIZE, 1, src);
                if (ferror(src) != 0) { //gotta error check! If you don't, you could get trapped in a roguelike card game or something.
                    printf("Failed to read from source"); //Which I think is the plot of Inscryption, actually.
                    fclose(src); //not sure, I can't get past the fact that it's a roguelike yet.
                    fclose(archive);
                    free(head);
                    return 1;
                }

                //and now write to the file.
                fwrite(buf, BLOCK_SIZE, 1, archive); //write a 512-byte block to the archive file.
                if (ferror(archive) != 0) {
                    printf("Failed to write file to archive");
                    fclose(src);
                    fclose(archive); //the magnus archives is a podcast distributed by rusty quill that also has tape archives in it.
                    free(head);
                    return 1;
                }
        }
        
        cur = cur->next;
        free(head); //since it's a for loop this prevents mem leaks? may need to swap how we do this later depending on how fwrite works.
        fclose(src); //don't want to forget to close the file before opening the next one.
    }

    //quick and easy for loop to get the footer in nicely.
    memset(buf, 0, 512);
    for (int i = 0; i < 2; i++) {
        fwrite(buf, BLOCK_SIZE, 1, archive); //write the footer block. should happen twice
    }

    fclose(archive);
    return 0;
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    //strip the trailing 0 blocks (there are two of them!). Looks like the provided function opens and closes the file in it, so it comes before our fopen().
    if (remove_trailing_bytes(archive_name, (512 * 2)) == -1) { // should be fine to do it in one block? also, error checking
        printf("Error removing zero blocks from archive");
        //remove_trailing_bytes closes the file, so we don't have to do any cleanup for this error in particular.
        return 1; //error value
    }

    //open the archive file in append mode so we don't completely overwrite it.
    FILE* archive = fopen(archive_name, "a"); //this should write from the end of the file.
    char buf[BLOCK_SIZE]; //buffer, gets set to 512 bytes worth of random memory.
    
    node_t* cur = files->head;
    while (cur != NULL) {
        //open file in read mode
        FILE* src = fopen(cur->name, "r");
        //create and fill header, hopefully.
        tar_header* head;
        head = malloc(sizeof(tar_header)); //unsure what else to do besides malloc; the heap is. a bit 
        fill_tar_header(head, cur->name);

        //write the head to the file now, probably.
        fwrite(head, sizeof(tar_header), 1, archive); //this should write the head to the file.
        if (ferror(archive) != 0) {
            printf("Failed to write file header to archive");
            fclose(src);
            fclose(archive);
            free(head);
            return 1;
        }

        struct stat stat_buf; //set up a stat buffer that we'll only grab the size from. this is to set up the loop that'll show up in a few lines.
        stat(cur->name, &stat_buf); //according to the header function, stat is a system call to inspect file metadata. neat!
        int src_size = stat_buf.st_size; //this is what we actually want. could drop an error check here to make sure that this is a positive number *shrug*

        //buffer will contain 512 bytes of data; FULL BUFFER must always be written to tar so it's 512-byte blocks, even if that's mostly 0s.
        //probably set buf to all 0's first, then fread in and then fwrite the entire buffer. for loop for the case of files that are greater than 512 bytes.

        for (int i = 0; i < ceil(src_size / 512.0); i++) { //ceiling of file size/512. (is file size from stat in bytes? It'd better be.)
                //set buf to all 0, using memset. this way if the file/file piece is less than 512 bytes, it'll be nice and fill the rest with 0 automatically.
                // Using 512.0 to perform float division
            memset(buf, 0, BLOCK_SIZE);
            //now read to the buffer; any bytes that are NOT filled will remain zeroes I believe.
            fread(buf, BLOCK_SIZE, 1, src);
            if (ferror(src) != 0) { //gotta error check! If you don't, you could get trapped in a roguelike card game or something.
                perror("Failed to read from source"); //Which I think is the plot of Inscryption, actually.
                fclose(src); //not sure, I can't get past the fact that it's a roguelike yet.
                fclose(archive);
                free(head);
                return 1;
            }
            //and now write to the file.
            fwrite(buf, BLOCK_SIZE, 1, archive); //write a 512-byte block to the archive file.
            if (ferror(archive) != 0) {
                perror("Failed to write file to archive");
                fclose(src);
                fclose(archive); //the magnus archives is a podcast distributed by rusty quill that also has tape archives in it.
                free(head);
                return 1;
            }
        }

        cur = cur->next;
        free(head); //since it's a for loop this prevents mem leaks? may need to swap how we do this later depending on how fwrite works.
        fclose(src); //don't want to forget to close the file before opening the next one.
    }


    //NOTE: this loop is EXACTLY COPIED from the create_archive function. if the create_archive function's loop needs changes, this will too.
    //re-add the zero-blocks
    memset(buf, 0, BLOCK_SIZE);
    for (int i = 0; i < 2; i++) {
        fwrite(buf, BLOCK_SIZE, 1, archive); //write the footer block. should happen 2x because two of them.
        if (ferror(archive) != 0) {
            perror("Failed to write footer to archive");
            fclose(archive);
            return 1;
        }
    }

    //and we're done.
    fclose(archive);
    return 0;
}


// Takes in the 0-padded octal string representation of a file's size and outputs a decimal integer
// Not guaranteed to return a multiple of 512
int sizeFromOctal(char *digits)
{
    // Iterate through size character array converting octal characters to a decimal integer
    // printf("size of %s: %s", current->name, current->size);
    int size_bytes = 0; // must remain a multiple of 512
    int power = 10; // power represents the current power of 8, starts at 10 and goes down to 0
    int i = 0;
    char currDigit = digits[0];
    while (currDigit != '\0')
    {
        if (currDigit >= 48 && currDigit <= 57) //only convert to decimal if digit is integer (not null)
        {
            size_bytes += ((int)currDigit - 48) * pow(8, power); // integers (0) start at 48 in ASCII table, subtract to get integer value
        }

        i++;
        power--;
        currDigit = digits[i];
    }

    return size_bytes;
}


// Returns 0 if next header exists, 1 if end of archive
// When called cursor must be at beginning of current header
int cursorToNextHeader(tar_header *current, FILE *archive)
{
    int size_bytes = sizeFromOctal(current->size);
    // size_bytes now holds the true size of the file
    // Need to make sure it is a multiple of 512 to iterate to next header so round up
    int remainder = size_bytes % 512;
    if (remainder != 0)
    {
        size_bytes += (512 - remainder);
    }
    // printf("%s converts to %d bytes\n", current->size, size_bytes);

    // cursor is at beginning of head
    fseek(archive, sizeof(tar_header) + size_bytes, SEEK_CUR); // Take the pointer to the current head and move it to next header
    if (ferror(archive) != 0)
    {
        printf("Failed to move cursor %d bytes forward", size_bytes);
        perror("");
        fclose(archive);
        return 1;
    }
    // printf("cursor position after fseek: %ld\n", ftell(archive));

    // To check results of this conversion and return if there is a header or not
    int returnVal;
    int name = 0;
    fread(&name, sizeof(int), 1, archive);
    if (ferror(archive) != 0)
    {
        perror("Failed to read from source");
        fclose(archive);
        return 1;
    }

    if (name) // If the byte is 0 that's the footer, no header to read
    {
        // printf("next header exists, not end of archive\n");
        returnVal = 0;
    }
    else
    {
        // printf("unable to read next header\n");
        returnVal = 1;
    }

    // Now that we've read in 4 bytes from the next header to test if it's actually a header,
    // move the cursor back to the beginning of the header
    fseek(archive, -1 * sizeof(int), SEEK_CUR); // Take the pointer to the current head and move it to next header
    if (ferror(archive) != 0)
    {
        printf("Failed to move cursor %ld bytes forward", -1 * sizeof(int));
        perror("");
        fclose(archive);
        return 1;
    }

    // printf("cursor position at cursorToNextHeader return: %ld\n", ftell(archive));
    return returnVal;
} 

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    FILE *archive = fopen(archive_name, "r");
    tar_header *curr = malloc(sizeof(tar_header));

    // initialize curr, read in the first header and move the cursor back to the beginning of the header
    fread(curr, sizeof(tar_header), 1, archive);
    if (ferror(archive) != 0)
    {
        perror("Failed to read from source");
        fclose(archive);
        free(curr);
        return 1;
    }

    fseek(archive, -1 * sizeof(tar_header), SEEK_CUR);
    if (ferror(archive) != 0)
    {
        printf("Failed to move cursor %ld bytes forward\n", -1 * sizeof(tar_header));
        perror("");
        fclose(archive);
        return 1;
    }

    // printf("cursor pos: %ld\n", ftell(archive));
    
    while (1)
    {
        printf("%s\n", curr->name);
        file_list_add(files, curr->name);

        if (cursorToNextHeader(curr, archive) == 1) // If the next header doesn't exist were done here
        {
            break;
        }

        // printf("reading in header at pos %ld\n", ftell(archive));
        // iterate curr, read in the next header and move the cursor back to the beginning of the header
        fread(curr, sizeof(tar_header), 1, archive);
        if (ferror(archive) != 0)
        {
            perror("Failed to read from source");
            fclose(archive);
            free(curr);
            return 1;
        }

        fseek(archive, -1 * sizeof(tar_header), SEEK_CUR);
        if (ferror(archive) != 0)
        {
            printf("Failed to move cursor %ld bytes forward", -1 * sizeof(tar_header));
            perror("");
            fclose(archive);
            free(curr);
            return 1;
        }
    }
    
    // printf("cursor pos: %ld\n", ftell(archive));
   
    free(curr);
    fclose(archive);

    return 0;
}

int file_list_no_print(const char* archive_name, file_list_t* files) {
    FILE* archive = fopen(archive_name, "r");
    tar_header* curr = malloc(sizeof(tar_header));

    // initialize curr, read in the first header and move the cursor back to the beginning of the header
    fread(curr, sizeof(tar_header), 1, archive);
    if (ferror(archive) != 0)
    {
        perror("Failed to read from source");
        fclose(archive);
        free(curr);
        return 1;
    }

    fseek(archive, -1 * sizeof(tar_header), SEEK_CUR);
    if (ferror(archive) != 0)
    {
        printf("Failed to move cursor %ld bytes forward\n", -1 * sizeof(tar_header));
        perror("");
        fclose(archive);
        return 1;
    }

    // printf("cursor pos: %ld\n", ftell(archive));

    while (1)
    {
        file_list_add(files, curr->name);

        if (cursorToNextHeader(curr, archive) == 1) // If the next header doesn't exist were done here
        {
            break;
        }

        // printf("reading in header at pos %ld\n", ftell(archive));
        // iterate curr, read in the next header and move the cursor back to the beginning of the header
        fread(curr, sizeof(tar_header), 1, archive);
        if (ferror(archive) != 0)
        {
            perror("Failed to read from source");
            fclose(archive);
            free(curr);
            return 1;
        }

        fseek(archive, -1 * sizeof(tar_header), SEEK_CUR);
        if (ferror(archive) != 0)
        {
            printf("Failed to move cursor %ld bytes forward", -1 * sizeof(tar_header));
            perror("");
            fclose(archive);
            free(curr);
            return 1;
        }
    }

    // printf("cursor pos: %ld\n", ftell(archive));

    free(curr);
    fclose(archive);

    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    //open archive in "r" mode.
    FILE *archive = fopen(archive_name, "r");

    //create buffer
    char buf[BLOCK_SIZE];

    //extract file list from header
    file_list_t *files = malloc(sizeof(file_list_t));
    file_list_init(files);
    file_list_no_print(archive_name, files);
    node_t* cur = files->head;

    //initialize a header to iterate through files in archive, grabbing the size of each one
    tar_header* h = malloc(sizeof(tar_header));
    
    //LOOP START- until end of list; assume files later in the archive are more recently added for multiple versions.
    //can add efficiency w/checking later if needed
    while (cur != NULL) {
        //open file; "w" mode
        FILE* dest = fopen(cur->name, "w");
        if (ferror(dest) != 0) {
            perror("Failed to open destination");
            fclose(archive);
            free(cur);
            free(h);
            return 1;
        }

        //fread past file header; will need to get size field from it.
        fread(h, sizeof(tar_header), 1, archive);
        if (ferror(archive) != 0) {
            perror("Failed to read header from archive");
            fclose(archive);
            fclose(dest);
            free(cur);
            free(h);
            return 1;
        }
        //get file size; save as int.
        int size = sizeFromOctal(h->size);
        printf("file size: %d\n", size);
        int roundedUpSize = ceil(size / 512.0);
        
        //data-writing loop; probably do in 512-blocks with memset for zeroes, for Consistency.
        for (int i = 0; i < roundedUpSize; i++) {
            memset(buf, 0, BLOCK_SIZE);
            fread(buf, BLOCK_SIZE, 1, archive);
            if (ferror(archive) != 0) { //this is from create/append but reversed and with perror
                perror("Failed to read from archive"); 
                fclose(dest); 
                fclose(archive);
                free(cur);
                free(h);
                return 1;
            }
            //write from buf to new file
            fwrite(buf, BLOCK_SIZE, 1, dest); //write a 512-byte block to the destination file
            if (ferror(archive) != 0) { //same with this.
                perror("Failed to write file to archive");
                fclose(dest);
                fclose(archive); 
                free(cur);
                free(h);
                return 1;
            }
            printf("%dth 512 block written of %f total\n", i+1, ceil(size / 512.0));
        } //end data-writing loop
        //close file
        fclose(dest);

        //remove trailing zeroes after we write the data to a file in the working directory
        //(this call opens & closes by itself, so we do it after we close to avoid weirdness)
        int remainder = size % 512;
        printf("remainder = %d\n", remainder);
        if (remainder != 0)
        {
            printf("removing %d bytes from %s\n", 512 - remainder, cur->name);
            remove_trailing_bytes(cur->name, 512 - remainder); //512 - remainder, computes distance from EOF to next block
        }
        
        //loop end?
        cur = cur->next; //move to next node.
    }

    //clear file list
    file_list_clear(files);

    //close archive
    fclose(archive);

    //free malloc'd space
    free(h);
    free(files);

    return 0;
}

// used to implement the "-u" command. Checks if every element of *files exists in archive_name,
// and returns 1 if any of them are missing. If they are all present, appends each of them onto the end
// of the archive, keeping the old versions in the same spot and returning 0
int update_archive(const char *archive_name, file_list_t *files)
{
    // initialize a file list to hold every file in archive
    file_list_t *archive_files = malloc(sizeof(file_list_t));
    file_list_init(archive_files);
    // initialize a node to iterate through the files passed in as command line arguments
    node_t *cur = files->head;

    if (file_list_no_print(archive_name, archive_files) == 0) // if the files from archive_name made it into archive_files
    {
        while (cur != NULL) // iterate through files passed in and make sure thay're all contained in archive_files
        {
            if (file_list_contains(archive_files, cur->name) == 0)
            {
                file_list_clear(archive_files);
                free(archive_files);
                return 1;
            }

            cur = cur->next;
        }

        // once we know all the files exist in the archive, we can append them all
        append_files_to_archive(archive_name, files);

        file_list_clear(archive_files);
        free(archive_files);
        return 0;
    }
    else
    {
        file_list_clear(archive_files);
        free(archive_files);
        return 1;
    }
}