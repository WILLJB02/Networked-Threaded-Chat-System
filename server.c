#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#define MAX_COMMAND_LENGTH 6
#define VALID_CHARACTERS 32
#define NORMAL_EXIT 0

/*
 * Data structure which stores information about a client which has connected
 * to the server.
 */
typedef struct Client {
    char* name;
    FILE* to;
    FILE* from;
    struct Client* previous;
    struct Client* next;
    //counts of command sent by clients
    int say;
    int kick;
    int list;
} Client;

/*
 * Stores informaiton about the linked list which stores clients.
 */
typedef struct {
    int count;
    Client* head;
    Client* tail;
    pthread_mutex_t mutex;
    // counts of total number of commands sent to server
    int auth;
    int name;
    int say;
    int kick;
    int list;
    int leave;
} ClientList;

/*
 * Structure which stores information required for a client connection to be
 * added to the server. This information will be passed to a client thread
 * when created."
 */
typedef struct {
    int serverDiscriptor;
    char* serverAuth;
    ClientList* clientList;
} ClientData;

/*
 * Strucutre which stores informaiton required for the SIGHUP statitics 
 * thread. This informaiton will be passed to the statitics thread when 
 * generated.
 */
typedef struct {
    sigset_t* set;
    ClientList* clientList;
} StatisticsData;

void* client_thread(void* arg);

char* read_file_line(FILE* file);

char* set_port_number(char* portInput, int argc);

void check_file(FILE* file, char* errorMessage);

void usage_error(char* errorMessage);

char* convert_readable(char* message);

void client_left(ClientList* clientList, char* name);

/*
 * A function which initialises a singular empty client list (ie holds no 
 * clients and has not recieved any commands of any type) and returns the 
 * corresponding data structure.
 */
ClientList* create_client_list() {
    ClientList* clientList = (ClientList *) malloc(sizeof(ClientList));
    clientList->count = 0;
    clientList->head = NULL;
    clientList->tail = NULL;
    pthread_mutex_init(&(clientList->mutex), NULL);
    clientList->auth = 0;
    clientList->name = 0;
    clientList->say = 0;
    clientList->kick = 0;
    clientList->list = 0;
    clientList->leave = 0;
    return clientList;
}

/*
 * Function that allocates memory and initiliss StatsiticsData structure.
 * Parameters:
 * set - set of signals to wait for
 * clientList - list of clients connected to the server.
 */
StatisticsData* create_statistics_data(sigset_t* set, ClientList* clientList) {
    StatisticsData* statisticsData = (StatisticsData *) 
            malloc(sizeof(StatisticsData));
    statisticsData->clientList = clientList;
    statisticsData->set = set;
    return statisticsData;
}

/*
 * Function the initilisies that allocates memory and initilisaes the
 * ClientData strucutre.
 * Parameters:
 * clientList - list of clients connected to the server.
 * Return:
 * ClientData* - created ClientData strucutre.
 */
ClientData* create_client(ClientList* clientList) {
    ClientData* data = (ClientData *) malloc(sizeof(ClientList));
    data->clientList = clientList;
    return data;
}

/*
 * Function which adds a client to the client list in its lexogrpahcial 
 * position based upon the servers decided upon name. Returns the data 
 * strucuture representing the client that has been added.
 * Paramters:
 * clientList - list of clients connected to the server
 * name - name of client to be added
 * to - file to send infromaiton to client
 * from - file to recieve informaiton from client
 * Return:
 * Client* - client that has been added to the server.
 *
 */
Client* add_client(ClientList* clientList, char* name, FILE* to, FILE* from) {
    Client* client;
    pthread_mutex_lock(&(clientList->mutex));
    client = (Client*) malloc(sizeof(Client));
    client->name = name;
    client->to = to;
    client->from = from;
    client->say = 0;
    client->list = 0;
    client->kick = 0;
    // alphabetically addiing the client to the list based on name and number
    // of clients already in the list.
    if (clientList->count == 0) {
        clientList->head = client;
        clientList->tail = client;
    } else if (strcmp(name, clientList->head->name) < 0) {
        clientList->head->previous = client;
        client->next = clientList->head;
        clientList->head = client;
    } else if (clientList->count == 1) {
        clientList->head->next = client;
        client->previous = clientList->head;
        clientList->tail = client;
    } else {
        Client* before = clientList->head;
        Client* after = clientList->head->next;
        while (before != NULL) {
            if (strcmp(after->name, name) > 0) {
                client->previous = before;
                before->next = client;
                client->next = after;
                after->previous = client;
                break;
            }
            before = before->next;
            after = after->next;
            if (after == NULL) {
                client->previous = before;
                before->next = client;
                clientList->tail = client;
                break;
            }
        }
    } 
    clientList->count++;
    pthread_mutex_unlock(&(clientList->mutex));
    return client;
}

/*
 * Function which removes a client from the client list and frees assoicated
 * memory space.
 * Paramters:
 * clientList - list of clients connected to the server
 * name - name of client to be removed
 */
void remove_client(ClientList* clientList, char* name) {
    pthread_mutex_lock(&(clientList->mutex));
    Client* client = clientList->head;
    //searching list of clients and removing client. Different prodedure to 
    //remove client is required depending on number of clients in list.
    while (client != NULL) {
        if (strcmp(client->name, name) == 0) {
            if (clientList->count == 1) {
                clientList->head = NULL;
                clientList->tail = NULL;
            } else if (client->previous == NULL) {
                clientList->head = client->next;
                clientList->head->previous = NULL;
            } else if (client->next == NULL) {
                clientList->tail = client->previous;
                clientList->tail->next = NULL;
            } else {
                client->previous->next = client->next;
                client->next->previous = client->previous;
            }
            clientList->count--;
            fclose(client->to);
            fclose(client->from);
            free(client);
            break;
        }
        client = client->next;
    }
    pthread_mutex_unlock(&(clientList->mutex));
}

/*
 * Function which sends the names of all clients connected to the server (ie
 * LIST:name1,name2,...) to a particular client.
 * Paramaters:
 * clientList - list of clients currently connected to the server.
 * to - file to send informaiton to the client.
 */
void list_client_names(ClientList* clientList, FILE* to) {
    Client* client;
    pthread_mutex_lock(&(clientList->mutex));
    client = clientList->head;
    fprintf(to, "LIST:");
    while (client != NULL) {
        // all clients have commer inbetween name except for last
        if (client->next == NULL) {
            fprintf(to, "%s", client->name);
        } else {
            fprintf(to, "%s,", client->name);
        }
        client = client->next;
    }
    fprintf(to, "\n");
    fflush(to);
    pthread_mutex_unlock(&(clientList->mutex));
}

/*
 * Funcction which can broadcast a given string to all clients connected to
 * the server.
 * Paramaters:
 * clientList - current clients connected to the server
 * message - string to be broadcast
 */
void broadcast(ClientList* clientList, char* message) {
    Client* client;
    pthread_mutex_lock(&(clientList->mutex));
    client = clientList->head;
    while (client != NULL) {
        fprintf(client->to, "%s\n", message);
        fflush(client->to);
        client = client->next;
    }
    pthread_mutex_unlock(&(clientList->mutex));
}

/*
 * Function which creates a socket for the server and begins listening
 * for client connections. The function returns the file descirptor which
 * is being listened on.
 * Paramter:
 * port - port which the server is to listen on.
 * Return:
 * int - file descriptor that the server is listening on.
 *
 * (addapted from CSSE2310 lecture code)
 */
int open_listen(const char* port) {
    struct addrinfo* addressInfo = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;   // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // client listen on all IP addresses


    if (getaddrinfo(NULL, port, &hints, &addressInfo)) {
        freeaddrinfo(addressInfo);
        communications_error();
    }
    // Create a socket and bind it to a port
    int listenDiscriptor = socket(AF_INET, SOCK_STREAM, 0); 
    // Allow address (port number) to be reused immediately
    int optVal = 1;
    if (setsockopt(listenDiscriptor, SOL_SOCKET, SO_REUSEADDR, 
            &optVal, sizeof(int)) < 0) {
        communications_error();
    }

    if (bind(listenDiscriptor, (struct sockaddr*)addressInfo->ai_addr, 
            sizeof(struct sockaddr)) < 0) {
        communications_error();
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    
    if (getsockname(listenDiscriptor, (struct sockaddr*)&address, &len)) {
        communications_error();
    }
    
    if (listen(listenDiscriptor, SOMAXCONN) < 0) { 
        communications_error();
    }

    //print listening socket
    fprintf(stderr, "%u\n", ntohs(address.sin_port));
    return listenDiscriptor;
}

/*
 * Function which waits for attempted connection from a client, and when one 
 * is recieved, generates a thread for the client to run on.
 * Paramaters:
 * fdServer - file descriptor for the server-client connection
 * serverAuth - the auth string rpovided to the server.
 *  clientList - list of clients connected to the server.
 *
 * (Adapted from CSSE2310 - lecture code)
 */
void process_connections(int fdServer, char* serverAuth, 
        ClientList* clientList) {
    int serverDiscriptor;
    struct sockaddr_in fromAddress;
    socklen_t fromAddressSize;
    while (1) {
        fromAddressSize = sizeof(struct sockaddr_in);
	// Block, waiting for a new connection. (fromAddress will be populated
	// with address of client)
        serverDiscriptor = accept(fdServer, (struct sockaddr*)&fromAddress, 
                &fromAddressSize);
        if (serverDiscriptor < 0) {
            communications_error();
        }

        ClientData* data = create_client(clientList);
        data->serverDiscriptor = serverDiscriptor;
        data->serverAuth = serverAuth;

	pthread_t threadId;
	pthread_create(&threadId, NULL, client_thread, data);
	pthread_detach(threadId);
    }
}

/*
 * Function which checks if a client has disconnected from the server and if 
 * so ends that client thread.
 * Parameters:
 * from - file to recieve infromaiton form client.
 * to - file top send informaiton to client.
 */
void check_client_disconnect(FILE* from, FILE* to) {
    if (feof(from) || ferror(from)) {
        fclose(to);
        fclose(from);
        pthread_exit(NULL);
    }
}

/*
 * Function which waits for a response from a client with the specified command
 * type. The response is then returned.
 * Paramaters:
 * clientList - list of clients connected to the server
 * from - file to recieve infromation from client
 * to - file to send infromation to client.
 * Return:
 * char* - response from client.
 */
char* wait_for_response(ClientList* clientList, FILE* from, FILE* to, 
        char* responseCommand) {
    char* clientResponse;
    char commandWord[MAX_COMMAND_LENGTH];
    // contintue reading from client until the desired type of message is 
    // recieved
    do {
        usleep(100000);
        clientResponse = read_file_line(from);
        check_client_disconnect(from, to);
        memcpy(commandWord, clientResponse, MAX_COMMAND_LENGTH - 1);
        commandWord[MAX_COMMAND_LENGTH - 1] = '\0';
    } while (strcmp(commandWord, responseCommand) != 0);
    return clientResponse;
}

/*
 * Function which checks if the auth string provided by the client matches
 * the auth string supplied to the server. If it does an OK: is sent to the
 * client and if it dosetn the server disconnects from the client.
 * Paramters:
 * serverAuth - auth string supplied to the server.
 * clientAuth - auth string supplied to the client.
 * to - file to send informaiton to client
 * from - file to recieve information from client.
 */
void check_auth(char* serverAuth, char* clientAuth, FILE* to, FILE* from) {
    if (strcmp(serverAuth, clientAuth) != 0) {
        fclose(to);
        fclose(from);
        pthread_exit(NULL);
    } else {
        fprintf(to, "OK:\n");
        fflush(to);
    }
}

/*
 * Function which broadcasts a message to all connected clients when a 
 * client leaves the server.
 * Paramaters:
 * clientList - clients currently connected to the server
 * name - name of client who left the server
 */
void client_left(ClientList* clientList, char* name) {
    remove_client(clientList, name);
    int leaveLength = 6;
    char* leave = malloc(leaveLength + strlen(name) + 1);
    sprintf(leave, "LEAVE:%s", name);
    printf("(%s has left the chat)\n", name);
    fflush(stdout);
    broadcast(clientList, convert_readable(leave));
    free(leave);
    pthread_exit(NULL);
}

/*
 * Function which broadcasts a MSG: command to all clients connected to 
 * the server when a specific client sends a message.
 * Paramaters:
 * clietnList - list of clients connected to the server
 * name - name of client who sent the message
 * message - message to be broadcast
 */
void broadcast_message(ClientList* clientList, char* name, 
        char* clientMessage) {
    int commandLength = 5;
    char* message = (char*) malloc(commandLength + strlen(name) + 
            strlen(clientMessage + 1));
    sprintf(message, "MSG:%s:%s", name, clientMessage);
    broadcast(clientList, convert_readable(message));
    printf("%s: %s\n", name, convert_readable(clientMessage));
    fflush(stdout);
    free(message);
}

/*
 * Function which broadcastts an ENTER: message to all clients connected to
 * the server when a new client connects.
 * Paramters:
 * clientList - list of clients connected to the server
 * name - name of client which connected to server
 */
void client_enter(ClientList* clientList, char* name) {
    int enterLength = 6;
    char* join = malloc(enterLength + strlen(name) + 1);
    sprintf(join, "ENTER:%s", name);
    printf("(%s has entered the chat)\n", name);
    fflush(stdout);
    broadcast(clientList, convert_readable(join));
    free(join);
}

/*
 * Function which converts any unwriteable characters in a string to '?'
 * Parameters:
 * message - string which is to be converted
 * Return:
 * char* - converted message.
 */
char* convert_readable(char* message) {
    for (int i = 0; i < strlen(message); i++) {
        if (message[i] < VALID_CHARACTERS) {
            message[i] = '?';
        }
    }
    return message;
}

/*
 * Function which sends a command to a given client.
 * Parameters:
 * toClient - file that sends informaiton to client
 * message - message/command to be sent to client.
 *
 */
void send_to_client(FILE* toClient, char* message) {
    fprintf(toClient, message);
    fflush(toClient);
}

/*
 * Function which searches the list of clients connected to server to find
 * a client with a specified name. If this client exsists than the data
 * strucutre for that client is returned, otherwise null is returned.
 * Paramters:
 * clientList - list of clients connected to the server.
 * name - name of client to search for.
 * Reutnr
 * Client* - client with given name, null if no client was found.
 *
 */
Client* find_client(ClientList* clientList, char* name) {
    Client* client;
    Client* foundClient = NULL;
    pthread_mutex_lock(&(clientList->mutex));
    client = clientList->head;
    while (client != NULL) {
        if (strcmp(client->name, name) == 0) {
            foundClient = client;  
        }
        client = client->next;
    }
    pthread_mutex_unlock(&(clientList->mutex));
    return foundClient;
}

/*
 * Function which completes name negotitation with a particular client 
 * and returns the name that has been accepted by the server.
 * Parameters:
 * clientList - clients currently connected to the server
 * toClient - file used to send information toClient
 * fromClient - file used to recieve informaiton from client.
 * Return:
 * char* - name of client accpeted by server.
 */
char* name_negotiation(ClientList* clientList, FILE* toClient, 
        FILE* fromClient) {
    char* name;
    char* clientResponse;
    Client* foundClient;
    // contintue asking client for its name until a unqiue non-empty 
    // name is given
    do {
        foundClient = NULL;
        send_to_client(toClient, "WHO:\n");
        clientResponse = wait_for_response(clientList, fromClient, 
                toClient, "NAME:");
        clientList->name++;
        strtok_r(clientResponse, ":", &name);
        foundClient = find_client(clientList, name);
        if (foundClient != NULL || name[0] == '\0') {
            send_to_client(toClient, "NAME_TAKEN:\n");
        }
    } while (foundClient != NULL || name[0] == '\0'); 
    send_to_client(toClient, "OK:\n");
    return name;
}

/*
 * Function which (after name negotiation is complete) determine which command 
 * has been sent by a clients and generates the approraite response. 
 * Function will repetely listen for client commands until client leaves.
 * Parameters:
 * clientList - list of clients connected to the server.
 * client - specific client that the server is listening to.
 * toClient - file which is used to send informaiotn to client.
 * fromeClient - file which is used to recived informaiton from client.
 */
void client_chatting(ClientList* clientList, Client* client, FILE* toClient,
        FILE* fromClient) {
    char* clientResponse;
    char* clientCommand;
    char* rest;
    Client* kickedClient;
    // conintue reading from client until for whatever reason it leaves the
    // server.
    do {
        usleep(100000);
        clientResponse = read_file_line(fromClient);
        if (feof(fromClient) || ferror(fromClient) || ferror(toClient)) {
            client_left(clientList, client->name);
        } else if (strcmp(clientResponse, "LEAVE:") == 0) {
            clientList->leave++;
            client_left(clientList, client->name);
        } else if (strcmp(clientResponse, "LIST:") == 0) {
            client->list++;
            clientList->list++;
            list_client_names(clientList, toClient);
        } else if (clientResponse[0] != '\0') {
            clientCommand = strtok_r(clientResponse, ":", &rest);
            if (strcmp(clientCommand, "SAY") == 0) {
                clientList->say++;
                client->say++;
                broadcast_message(clientList, convert_readable(client->name),
                        convert_readable(rest));
            } else if (strcmp(clientCommand, "KICK") == 0 && rest != NULL) {
                clientList->kick++;
                client->kick++;
                kickedClient = find_client(clientList, rest);
                if (kickedClient != NULL) {
                    Client* kickedClient = find_client(clientList, rest);
                    send_to_client(kickedClient->to, "KICK:\n");    
                }
            }           
        }
    } while (1);
}

/*
 * Thread function that handles the communication between the client and the 
 * server including authentication, name negotiation and chatting
 * Paramaters:
 * passedData - data passed to client thread including the file for 
 * communicaiton between server and client, the server authenticatio code
 * and the list of clients already connected to the server.
 */
void* client_thread(void* passedData) {
    char* clientResponse;
    char* name;
    char* clientAuth;

    //retrieving passed data
    ClientData* data = passedData;
    int toClientDiscriptor = data->serverDiscriptor;
    char* serverAuth = data->serverAuth;
    ClientList* clientList = data->clientList;
    
    //initiating file commucation
    int fromClientDescriptor = dup(toClientDiscriptor);
    FILE* toClient = fdopen(toClientDiscriptor, "w");
    FILE* fromClient = fdopen(fromClientDescriptor, "r");
    
    //authentication
    send_to_client(toClient, "AUTH:\n");
    clientResponse = wait_for_response(clientList, fromClient, toClient,
            "AUTH:");
    clientList->auth++;
    strtok_r(clientResponse, ":", &clientAuth);
    check_auth(serverAuth, clientAuth, toClient, fromClient);

    //name negotiation
    name = name_negotiation(clientList, toClient, fromClient);
    Client* client = add_client(clientList, name, toClient, fromClient);
    client_enter(clientList, name);
    
    //client chatting
    client_chatting(clientList, client, toClient, fromClient);
    pthread_exit(NULL);
}

/*
 * Thread function which waits for a SIGHUP to be recieved and prints to stdout
 * stats regarding how many of each command has been sent by clients/ the
 * server.
 * Paramaters:
 * data - data to be passed to thread including informaiotn regaridng how many
 * commands have been sent and what signals to wait for.
 */
void* statistics_thread(void* data) {
    StatisticsData* statisticsData = data;
    ClientList* clientList = statisticsData->clientList;
    sigset_t* set = statisticsData->set;
    int signal;
    // wait for signal forever and then when one is recieved print stats
    for (;;) {
        sigwait(set, &signal);
        fprintf(stderr, "@CLIENTS@\n");
        Client* client = clientList->head;
        while (client != NULL) {
            fprintf(stderr, "%s:SAY:%d:KICK:%d:LIST:%d\n", client->name, 
                    client->say, client->kick, client->list);
            client = client->next;
        }
        fprintf(stderr, "@SERVER@\nserver:AUTH:%d:NAME:%d:SAY:%d:KICK:%d:"
                "LIST:%d:LEAVE:%d\n", clientList->auth, clientList->name, 
                clientList->say, clientList->kick, clientList->list, 
                clientList->leave);
    }
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN); //ignore SIGPIPE
    
    int serverDiscriptor;
    pthread_t thread;
    sigset_t set;

    ClientList* clientList = create_client_list();
    
    // creating statstics thread
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    StatisticsData* statisticsData = create_statistics_data(&set, clientList);
    pthread_create(&thread, NULL, &statistics_thread, statisticsData);
    
    if (argc != 2 && argc != 3) {
        usage_error("Usage: server authfile [port]\n");
    }
    
    // opening authfile
    int authfileDiscriptor = open(argv[1], O_RDONLY);
    FILE* authfile = fdopen(authfileDiscriptor, "r");
    check_file(authfile, "Usage: server authfile [port]\n");
    char* serverAuth = read_file_line(authfile);
    
    const char* port = set_port_number(argv[2], argc);
    serverDiscriptor = open_listen(port);
    process_connections(serverDiscriptor, serverAuth, clientList);
    exit(NORMAL_EXIT);
}

/*
 * Function which returns the port number which will be used for client
 * connection. If  a port number was specified when first running the server 
 * program then this will be returned, otherwise "0" is returned indicating 
 * the use of a empherical port.
 * Paramters:
 * portInput - port number inputed by user when server run
 * argc - number of arguments inputed by user.
 * Return:
 * char* - port number which should be used to generate connections with
 * client.
 */
char* set_port_number(char* portInput, int argc) {
    if (argc == 3) {
        return portInput;
    } else {
        return "0";
    }
}

