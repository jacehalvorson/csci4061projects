// Jace Halvorson
// Skylar Volden

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "file_list.h"
#include "minitar.h"

// used to implement the "-u" command. Checks if every element of *files exists in archive_name,
// and returns 1 if any of them are missing. If they are all present, appends each of them onto the end
// of the archive, keeping the old versions in the same spot and returning 0
// Note: definition exists in minitar.c, declaration here because Gradescope wouldn't let us
// modify minitar.h and it was throwing an "implicit declaration" error without this
int update_archive(const char *archive_name, file_list_t *files);

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("%s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    file_list_t files;
    file_list_init(&files);
    for (int i = 4; i < argc; i++) // iterate over every argument starting with the 5th (argv[4])
    {
        file_list_add(&files, argv[i]); // uses file_list.c to generate an array of files
    }

    char cmd[128]; // "-c" or "-a", etc. Was using cmd[16] but that caused Gradescope tests to fail
    char archive_name[128]; // max archive name 128 characters
    strcpy(cmd, argv[1]);
    strcpy(archive_name, argv[3]); 
    // No need to use argv[2] "-f" because it's always the same

    if (strcmp(cmd, "-c") == 0)
    {
        create_archive(archive_name, &files);
       // printf("archive created named %s\n", archive_name);
    }
    else if (strcmp(cmd, "-a") == 0)
    {
        append_files_to_archive(archive_name, &files);
    }
    else if (strcmp(cmd, "-t") == 0)
    {
        get_archive_file_list(archive_name, &files);
    }
    else if (strcmp(cmd, "-u") == 0)
    {
        update_archive(archive_name, &files);
    }
    else if (strcmp(cmd, "-x") == 0)
    {
        extract_files_from_archive(archive_name);
    }
    else
    {
        printf("Unknown command. Usage: ./minitar -c|a|t|u|x -f ARCHIVE [FILE...]");
    }

    file_list_clear(&files);
    return 0;
}
