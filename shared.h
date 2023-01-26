#ifndef _SHARED_H
#define _SHARED_H
#include <stdio.h>

char* read_file_line(FILE* file);

void check_file(FILE* file, char* errorMessage);

void usage_error(char* errorMessage);

void communications_error();

#endif
