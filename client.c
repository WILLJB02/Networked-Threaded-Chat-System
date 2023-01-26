#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include "shared.h"
#include <unistd.h>
#define KICKED 3
#define AUTH_ERROR 4
#define NORMAL_EXIT 0


/*
 * Function which sends a message to the server based on what the client types
 * into stdin
 * Paramatesr:
 * toServer - file to send message to server
 * */
void* send_message(void* toServer);

/*
 * Function which recieves message from server and prints to stdout the 
 * approriate response.
 * Parameter:
 * fromServer - where client recieves command from server.
 */
void* recieve_message(void* fromServer);

/*
 * Function which attempts to connect the client to the server given to port
 * Paramters:
 * port - port given to client to attempt to connect to the server
 *
 * (code addpated from CSSE2310 lecture code)
 */
int connect_socket(const char* port) {
    struct addrinfo* addressInfo = 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;          
    hints.ai_socktype = SOCK_STREAM;
    int error;

    if ((error = getaddrinfo("localhost", port, &hints, &addressInfo))) {
        freeaddrinfo(addressInfo);
        communications_error();
    }

    int connectionDiscriptor = socket(AF_INET, SOCK_STREAM, 0); 
    if (connect(connectionDiscriptor, (struct sockaddr*)addressInfo->ai_addr, 
            sizeof(struct sockaddr))) {
        communications_error();
    }
    return connectionDiscriptor;
}

/*
 * Function which checks if there has been an authentication error after
 * sending auth string to server. (ie checks if server disconnected client)
 * Parameters:
 * fromServer - infromation recieved from server.
 */
void authentication_error(FILE* fromServer) {
    if (feof(fromServer) || ferror(fromServer)) {
        fprintf(stderr, "Authentication error\n");
        fflush(stderr);
        exit(AUTH_ERROR);
    }
}

/*
 * Function which sends the clients name to the server based on the name 
 * supplied when running the program and how many times NAME_TAKEN: has
 * been recieved by the server:
 * Paramaters:
 * to - send information to server
 * name - name of client supplied by user
 * iteration - number of times NAME_TAKEN has been recieved by the client.
 */
void send_name(FILE* toServer, char* name, int iteration) {
    if (iteration == -1) {
        fprintf(toServer, "NAME:%s\n", name);
    } else {
        fprintf(toServer, "NAME:%s%d\n", name, iteration);
    }
    fflush(toServer);
}

/*
 * Function which waits for a specified command to be recieved from the server
 * Parameter:
 * from - communication from server
 * command - command that client is waiting on form server.
 */
void wait_for_server(FILE* from, char* command) {
    char* serverCommand;
    do {
        serverCommand = read_file_line(from);
        if (feof(from) || ferror(from)) {
            communications_error();
        }
    } while (strcmp(serverCommand, command) != 0);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        usage_error("Usage: client name authfile port\n");
    }
    const char* port = argv[3];
    int toDiscriptor = connect_socket(port);
    int authfileDiscriptor = open(argv[2], O_RDONLY);
    FILE* authfile = fdopen(authfileDiscriptor, "r");
    check_file(authfile, "Usage: client name authfile port\n");    
    int fromDiscriptor = dup(toDiscriptor);
    FILE* to = fdopen(toDiscriptor, "w");
    FILE* from = fdopen(fromDiscriptor, "r");
    char* serverCommand;
    int iteration = -1;
    char* name = argv[1];

    wait_for_server(from, "AUTH:");
    char* auth = read_file_line(authfile);
    fprintf(to, "AUTH:%s\n", auth);
    fflush(to);

    do {
        serverCommand = read_file_line(from);
        authentication_error(from);
    } while (strcmp(serverCommand, "OK:") != 0);
    do {  
        wait_for_server(from, "WHO:");
        send_name(to, name, iteration);
        do {
            serverCommand = read_file_line(from);
            if (feof(from) || ferror(from)) {
                communications_error();
            }
        } while (strcmp(serverCommand, "NAME_TAKEN:") != 0 && 
                strcmp(serverCommand, "OK:") != 0);
        iteration++;
    } while (strcmp(serverCommand, "OK:") != 0);
    pthread_t tid1, tid2;
    pthread_create(&tid1, 0, send_message, (void*) to);
    pthread_create(&tid2, 0, recieve_message, (void*) from);
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    exit(NORMAL_EXIT);
}

void* send_message(void* toServer) {
    FILE* to = (FILE*) toServer;
    char* clientInput;
    do {
        //checking if client should leave before sending command
        clientInput = read_file_line(stdin);
        if (feof(stdin)) {
            exit(NORMAL_EXIT);
        }
        if (ferror(to)) {
            communications_error();
        }
        // sending command to server based on client input
        if (clientInput[0] == '*') {
            memmove(clientInput, clientInput + 1, strlen(clientInput));
            if (strcmp(clientInput, "LEAVE:") == 0) {
                fprintf(to, "%s\n", clientInput);
                exit(NORMAL_EXIT);
            } else {
                fprintf(to, "%s\n", clientInput);
            }
        } else {
            fprintf(to, "SAY:%s\n", clientInput);
        }
        fflush(to);
    } while (1);
    fclose(to);
    return (void*)0;
}

void* recieve_message(void* fromServer) {
    FILE* from = (FILE*) fromServer;
    char* serverCommand;
    do {
        serverCommand = read_file_line(from);
        //checking if client has left before processing message
        if (feof(from) || ferror(from)) {
            communications_error();
        }
        if (strcmp(serverCommand, "KICK:") == 0) {
            fprintf(stderr, "Kicked\n");
            exit(KICKED);
        }
        char* commandType;
        char* commandArgument;
        char* rest;
        if (serverCommand[0] != '\0') {
            commandType = strtok_r(serverCommand, ":", &rest);
            commandArgument = strtok_r(rest, ":", &rest);
            // checking command type and completing corresponding action
            if (strcmp(commandType, "ENTER") == 0 && commandArgument != NULL) {
                fprintf(stdout, "(%s has entered the chat)\n", 
                        commandArgument); 
            } else if (strcmp(commandType, "LEAVE") == 0 && 
                    commandArgument != NULL) {
                fprintf(stdout, "(%s has left the chat)\n", commandArgument);  
            } else if (strcmp(commandType, "LIST") == 0 && 
                    commandArgument != NULL) {
                fprintf(stdout, "(current chatters: %s)\n", commandArgument);  
            } else if (strcmp(commandType, "MSG") == 0 && 
                    commandArgument != NULL) {
                fprintf(stdout, "%s: %s\n", commandArgument, rest);  
            }
        }
        fflush(stdout);
    } while (1);
    fclose(from);
    return (void*)0;
}
