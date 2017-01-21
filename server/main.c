/*
 * LSP Kursa projekts
 * Alberts Saulitis
 * Viesturs Ružāns
 *
 * 23.12.2016
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <dirent.h>

#define MAX_PLAYERS 16
#define MAX_PACKET_SIZE 1472
#define PACKET_TYPE_SIZE 1
#define MAX_NICK_SIZE 20
#define MAX_MAP_HEIGHT 100
#define MAX_MAP_WIDTH 100


typedef struct clientInfo clientInfo_t;

void exitWithMessage(char error[]);

void *handle_connection(void *socket_desc);

void processArgs(int argc, char *argv[]);

void *safe_malloc(size_t);

int startServer();

void initPacket(char *buffer, ssize_t *bufferPointer);

void sendMassPacket(char *buffer, ssize_t bufferPointer);

clientInfo_t *initClientData(int, struct in_addr);

void sendPlayerDisconnect(clientInfo_t *client);

/*
 * Enumerations
 * http://en.cppreference.com/w/c/language/enum
 */
enum connectionError_t {
    ERROR_NAME_IN_USE = -1, ERROR_SERVER_FULL = -2
};

// Packet type enumerations
enum packet_t {
    JOIN, ACK, START, END, MAP, PLAYERS, SCORE, MOVE, MESSAGE, QUIT, JOINED, PLAYER_DISCONNECTED
};

//Map object enumerations
enum mapObjecT_t {
    None, Dot, Wall, PowerPellet, Invincibility, Score
};

// Client movement enumerations
enum clientMovement_t {
    UP, DOWN, RIGHT, LEFT
};

// Player state enumerations
enum playerState_t {
    NORMAL, DEAD
};
// Player type enumerations
enum playerType_t {
    Pacman, Ghost
};

/*
 * Structs
 */

typedef struct clientInfo {
    int sock;
    int id;
    struct in_addr ip;
    char name[20];
} clientInfo_t;


typedef struct mapList {
    char filename[FILENAME_MAX];
    int width;     //x
    int height;    //y
    bool active;
    char map[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];
    struct mapList *next;
} mapList_t;


/*
 * Globals
 */
clientInfo_t *clientArr[MAX_PLAYERS];
int PORT;
char MAPDIR[FILENAME_MAX];
mapList_t *MAP_HEAD;


void *safe_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { //Ja alokācija neizdevās, izdrukājam iemeslu, kurš iegūts no errno
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}


void exitWithMessage(char error[]) {
    printf("Error %s\n", error);
    exit(EXIT_FAILURE);
}


void processArgs(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            i++;
            PORT = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "-m") == 0) {
            i++;
            strcpy(MAPDIR, argv[i]);
        }
        else if (strcmp(argv[i], "-h") == 0) {
            exitWithMessage("-p [PORT] if not specified 8888"
                                    "-m [DIRECTORY] Directory name containing maps, default maps/");
            exit(0);
        }
    }
}

// Define the function to be called when ctrl-c (SIGINT) signal is sent to process
void signal_callback_handler(int signum) {
    printf("Caught signal %d\n", signum);
    // Cleanup and close up stuff here

    // Terminate program
    exit(signum);
}

/*
 * Returns client pointer to array or NULL if no free spots
 */
clientInfo_t *findClientSpot(clientInfo_t *client) {

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] == NULL) {
            clientArr[i] = client;
            return client;
        }
    }
    return NULL;
}

/*
 * Returns true if the name is in use, false otherwise
 */
bool isNameUsed(char *name) {

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] != NULL) {
            if (strcmp(clientArr[i]->name, name) == 0) return true;
        }
    }
    return false;
}

/*
 * Returns initialized client struct
 */
clientInfo_t *initClientData(int sock, struct in_addr ip) {
    static unsigned int CLIENT_ID_ITERATOR = 1;

    clientInfo_t *client = safe_malloc(sizeof(clientInfo_t));
    client->id = CLIENT_ID_ITERATOR++;
    client->sock = sock;
    client->ip = ip;
    return client;
}


int startServer() {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    // set SO_REUSEADDR on a socket to true (1):
    int optval = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
    if (socket_desc == -1 || optval == -1) {
        printf("Unable to create a socket");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    //Binds TCP
    if (bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) < 0) {
        printf("Unable to bind");
        return 1;
    }

    //Listen to incoming connections
    listen(socket_desc, 3);


    //Accept and incoming connection
    printf("Waiting for incoming connections on port %d\n", PORT);
    c = sizeof(struct sockaddr_in);
    pthread_t thread_id;


    while ((client_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t *) &c))) {
        printf("Connection accepted from %s \n", inet_ntoa(client.sin_addr));

        clientInfo_t *currentClient = initClientData(client_sock, client.sin_addr);


        if (pthread_create(&thread_id, NULL, handle_connection, (void *) currentClient) < 0) {
            perror("could not create thread");
            return 1;
        }

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( thread_id , NULL);
        puts("Handler assigned");
    }

    if (client_sock < 0) {
        perror("accept failed");
        return 1;
    }

    return 0;

}

/**
 * Thread fatal error handler to exit gracefully
 *  Prints the error message
 *  Closes the client socket
 *  Removes the client from client list array and cleans up the memory
 *  Exits the thread
 */
void threadErrorHandler(char errormsg[], int retval, clientInfo_t *client) {
    printf("%s: %s\n", inet_ntoa(client->ip), errormsg);
    close(client->sock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (client == clientArr[i]) {
            clientArr[i] = NULL;
            sendPlayerDisconnect(client);
        }
    }
    free(client);
    pthread_exit(&retval);
}

void sendPlayerDisconnect(clientInfo_t *client) {
    char buffer[MAX_PACKET_SIZE] = {0};
    buffer[0] = PLAYER_DISCONNECTED;
    memcpy(buffer + PACKET_TYPE_SIZE, &client->id, sizeof(int));
    sendMassPacket(buffer, PACKET_TYPE_SIZE + sizeof(int));
}

void receivePacket(char *buffer, ssize_t *bufferPointer, int sock) {
    initPacket(buffer, bufferPointer);
    *bufferPointer = recv(sock, buffer, MAX_PACKET_SIZE, 0);

}

/**
 * Sends the buffer to socket
 */
void sendPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client) {
    write(client->sock, buffer, (size_t) bufferPointer);
}

/**
 * Initializes empty packet and bufferPointer
 */
void initPacket(char *buffer, ssize_t *bufferPointer) {
    *bufferPointer = 0;
    memset(buffer, 0, MAX_PACKET_SIZE);
}

/*
 * Send the passed packet to all users
 */
void sendMassPacket(char *buffer, ssize_t bufferPointer) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i]) {
            sendPacket(buffer, bufferPointer, clientArr[i]);
        }
    }
}


/**
 * Function which processes new players (TCP)
 *      Receives JOIN, registers player nickname if possible
 *      Sends ACK to acknowledge the new nickname or ACK with negative ID indicating that player cannot join
 *      Sends JOINED to inform other players that a new player has joined.
 */
void processNewPlayer(clientInfo_t *clientInfo) {
    char buffer[MAX_PACKET_SIZE] = {0};
    ssize_t bufferPointer = 0;
    /*
     *  0 - Packet type
     *  1-21 - Nickname
     */
    receivePacket(buffer, &bufferPointer, clientInfo->sock);
    if (bufferPointer == 0) {
        threadErrorHandler("Unauthenticated client disconnected", 1, clientInfo);
    }
    else if (bufferPointer == -1) {
        threadErrorHandler("Recv failed", 2, clientInfo);
    }
    if ((int) buffer[0] == JOIN) {
        memcpy(clientInfo->name, buffer + PACKET_TYPE_SIZE, MAX_NICK_SIZE);
        if (isNameUsed(clientInfo->name)) {
            initPacket(buffer, &bufferPointer);
            buffer[0] = ACK;
            int errval = ERROR_NAME_IN_USE;
            memcpy(buffer + PACKET_TYPE_SIZE, &errval, sizeof(int));
            sendPacket(buffer, 5, clientInfo);

            threadErrorHandler("Name is in use", 5, clientInfo);
        }
        if (findClientSpot(clientInfo) == NULL) {
            initPacket(buffer, &bufferPointer);
            buffer[0] = ACK;
            int errval = ERROR_SERVER_FULL;
            memcpy(buffer + PACKET_TYPE_SIZE, &errval, sizeof(int));
            sendPacket(buffer, 5, clientInfo);
            threadErrorHandler("Server is full", 4, clientInfo);
        }

        // Everything OK, sending user ID
        initPacket(buffer, &bufferPointer);
        buffer[0] = ACK;
        memcpy(buffer + PACKET_TYPE_SIZE, &clientInfo->id, sizeof(int));
        sendPacket(buffer, sizeof(int) + PACKET_TYPE_SIZE, clientInfo);

        //Sending JOINED packet to everyone
        initPacket(buffer, &bufferPointer);
        buffer[0] = JOINED;
        memcpy(buffer + PACKET_TYPE_SIZE, &clientInfo->id, sizeof(int)); //Player ID
        memcpy(buffer + PACKET_TYPE_SIZE + sizeof(int), &clientInfo->name, MAX_NICK_SIZE); //Player name
        sendMassPacket(buffer, sizeof(int) + PACKET_TYPE_SIZE + MAX_NICK_SIZE);


        printf("New player %s(%d) from %s\n", clientInfo->name, clientInfo->id, inet_ntoa(clientInfo->ip));


    }
    else {
        threadErrorHandler("Incorrect command received", 3, clientInfo);
    }


}

void *handle_connection(void *conn) {
    //Get the socket descriptor
    clientInfo_t *clientInfo = (clientInfo_t *) conn;
    int sock = clientInfo->sock;

    //New client connection
    processNewPlayer(clientInfo);



    //Send some messages to the client
    /*message = "Greetings! I am your connection handler\n";
    write(sock, message, strlen(message));

    //Receive a message from client
    while ((read_size = recv(sock, client_message, 2000, 0)) > 0) {
        //end of string marker
        client_message[read_size] = '\0';
        sscanf(client_message, "name: %s", clientInfo.name);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (sockets[i] > 0) {
                snprintf(client_message,2000,"%s (ID: %d) sent: %s\n", clientInfo.name, clientInfo.id, client_message);
                write(sockets[i], client_message, strlen(client_message));
            }
        }





        //clear the message buffer
        memset(client_message, 0, 2000);
    }

    if (read_size == 0) {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if (read_size == -1) {
        perror("recv failed");
    }*/

    return 0;
}

void addMap(FILE *mapfile, char name[256]) {
    mapList_t *map = safe_malloc(sizeof(mapList_t));
    // Initialize map metadata
    map->next = NULL;
    map->active = false;
    map->height = 0;
    map->width = 0;
    memset(map->map, 0, MAX_MAP_HEIGHT * MAX_MAP_WIDTH);
    strcpy(map->filename, name);


    if (MAP_HEAD == NULL) { // First map, point map head to it
        MAP_HEAD = map;

    } else { // Nth map, go to the end of list and append
        mapList_t *tmp = MAP_HEAD;
        while (tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = map;
    }
    // Reads the map data into 2D array
    char c;
    int x = 0;
    int y = 0;
    do {
        c = getc(mapfile);
        if (c == '\n') {
            y++;
            x = 0;
        }
        else if (c==EOF){
            break;
        }
        else {
            c = c-'0'; //The data in file are char type, but the map data should have char value of 0/1/2... not 47/48/49...
            if (c == None || c == Dot || c == Wall || c == PowerPellet || c == Invincibility || c == Score) {
                map->map[y][x] = c;
                x++;
            } else {
                char tmp[FILENAME_MAX];
                snprintf(tmp, FILENAME_MAX, "Incorrect character in %s (%d:%d)", map->filename, x, y);
                exitWithMessage(tmp);
            }
        }
    } while (c != EOF);
    map->width = x;
    map->height = y;

}

/**
 * Reads maps from the MAPDIR directory into mapList struct list
 */
void initMaps() {

    DIR *desc; //Directory descriptor
    struct dirent *ent;
    FILE *open_file;


    /* Scanning the in directory */
    if (NULL == (desc = opendir(MAPDIR))) {
        exitWithMessage("Unable to open map directory");
    }

    while ((ent = readdir(desc))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue; //We do not want to follow current and upper directory hard links

        if (ent->d_type == DT_REG) {
            char filepath[FILENAME_MAX];
            snprintf(filepath, FILENAME_MAX, "%s/%s", MAPDIR, ent->d_name);
            // Open file
            open_file = fopen(filepath, "r");
            if (open_file == NULL) {
                fprintf(stderr, "Failed to open %s, skipping\n", ent->d_name);
                fclose(open_file);
                continue;
            }
            addMap(open_file, ent->d_name);
            fclose(open_file);
        }


    }
}

void initVariables() {
    // Default port
    PORT = 8888;
    // Default map directory
    snprintf(MAPDIR, FILENAME_MAX, "maps/");
    // Initialize player array
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clientArr[i] = NULL;
    }
    MAP_HEAD = NULL;
}


int main(int argc, char *argv[]) {

    signal(SIGINT, signal_callback_handler);
    initVariables();
    processArgs(argc, argv);
    initMaps();
    startServer();


    return 0;
}
