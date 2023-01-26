#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include "shared.h"
#define USAGE_ERROR_EXIT 1
#define COMMS_ERROR 2

/*
 * Function which checks if a given file can be opened. If it cant be opened
 * a usage error is tripped.
 */
void check_file(FILE* file, char* errorMessage) {
    if (file == 0) {
        usage_error(errorMessage);
    }
}

/*
 * Function which causes program to exit with usage error and print
 * to stderr the given errorMessage
 * Paramters:
 * errorMessage - message to be printed to stderr
 */
void usage_error(char* errorMessage) {
    fprintf(stderr, errorMessage);
    exit(USAGE_ERROR_EXIT);
}

/*
 * Function which causes program to exit with comunications error as per spec
 */
void communications_error() {
    fprintf(stderr, "Communications error\n");
    exit(COMMS_ERROR);
}

/*
 * Function which reads a line of text from a file until end of line or
 * end of file is reached. Returns the line of text read.
 * Paramaters 
 * file - file to read form
 * Return:
 * char* - line of text read
 */
char* read_file_line(FILE* file) {
    int bufferSize = 256;
    int position = 0;
    char* buffer = malloc(bufferSize * sizeof(char));
    int c;
    while ((c = fgetc(file)) != '\n' && !feof(file)) {
        fflush(stdout);
        buffer[position] = c;
        if (++position == bufferSize) {
            buffer = realloc(buffer, (bufferSize *= 2) * sizeof(char));
        }
    }
    fflush(stdout);
    buffer = realloc(buffer, (position + 1) * sizeof(char));
    buffer[position] = '\0';
    return buffer;
}
