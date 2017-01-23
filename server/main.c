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
#include <ctype.h>

#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L

#include <time.h>   // for nanosleep

#else
#include <unistd.h> // for usleep
#endif

#define MAX_PLAYERS 16
#define MAX_PACKET_SIZE 1472
#define PACKET_TYPE_SIZE 1
#define MAX_NICK_SIZE 20
#define MAX_MAP_HEIGHT 100
#define MAX_MAP_WIDTH 100
#define MIN_PLAYERS 1
#define TICK_FREQUENCY 1000             // Time between ticks in miliseconds
#define GHOST_RATIO 1
#define PACMAN_RATIO 1
#define SPAWNPOINT_TRAVERSAL_RANGE 3    // Nearby blocks to be checked for enemies when spawning
#define TICK_MOVEMENT 0.2f              // Player movement per each tick
#define DOT_POINTS 10                   //Points given for encountering DOT tole
#define SCORE_POINTS 100                //Points given for encountering SCORE tile
#define POWERUP_PowerPellet 120         //Ticks before PowerPellet expires
#define POWERUP_Invincibility 120       //Ticks before Invincibility expires

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

//Debug level enumerations
enum debugLevel_t {
    INFO, VERBOSE, DEBUG
};

/*
 * Declarations
 */
typedef struct clientInfo clientInfo_t;

void exitWithMessage(char error[]);

void *handle_connection(void *socket_desc);

void processArgs(int argc, char *argv[]);

void *safe_malloc(size_t);

int startServer();

void initPacket(char *buffer, ssize_t *bufferPointer);

void sendMassPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client);

clientInfo_t *initClientData(int, struct in_addr);

void sendPlayerDisconnect(clientInfo_t *client);

void *gameController(void *a);

void prepareStartPacket(char *buffer, clientInfo_t *client);

void sleep_ms(int milliseconds);

unsigned int getPlayerCount();

void stripSpecialCharacters(int *messageLength, char *message);

void processMove(clientInfo_t *client, enum clientMovement_t move);

void sendMessage(int playerId, int messageLength, char *message);

void processQuit(clientInfo_t *client);

void processTick(unsigned long int TICK);

/*
 * Structs
 */

typedef struct clientInfo {
    int sock;
    int id;
    struct in_addr ip;
    char name[20];
    enum playerType_t playerType;
    enum playerState_t playerState;
    enum clientMovement_t clientMovement;
    float x;
    float y;
    int score;
    bool active;
} clientInfo_t;


typedef struct mapList {
    char filename[FILENAME_MAX];
    int width;     //x
    int height;    //y
    bool active;
    char map[MAX_MAP_WIDTH][MAX_MAP_HEIGHT];
    struct mapList *next;
} mapList_t;


/*
 * Globals
 */
clientInfo_t *clientArr[MAX_PLAYERS];   // Array holding all player data
int PORT;                               // Server port (-p)
char MAPDIR[FILENAME_MAX];              // Directory containing maps (-m)
mapList_t *MAP_HEAD;                    // Pointer to the first MAP
bool gameStarted;                       // True if the game is in progress
mapList_t *MAP_CURRENT;                 // Pointer to the current loaded MAP
enum debugLevel_t debugLevel;           // Holds debugging level of the server (-v/-vv)



void *safe_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { //Ja alokācija neizdevās, izdrukājam iemeslu, kurš iegūts no errno
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}


void exitWithMessage(char error[]) {
    printf("%s\n", error);
    exit(EXIT_FAILURE);
}


void processArgs(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            i++;
            PORT = atoi(argv[i]);
        } else if (strcmp(argv[i], "-m") == 0) {
            i++;
            strcpy(MAPDIR, argv[i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            debugLevel = VERBOSE;
        } else if (strcmp(argv[i], "-vv") == 0) {
            debugLevel = DEBUG;
        } else if (strcmp(argv[i], "-h") == 0) {
            exitWithMessage("-p [PORT] if not specified 8888\n"
                                    "-m [DIRECTORY] Directory name containing maps, default maps\n"
                                    "-v Verbose logging\n"
                                    "-vv VERY verbose logging (including packets)\n");
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
    client->active = false;
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
        exitWithMessage("Unable to create a socket");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    //Binds TCP
    if (bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) < 0) {
        exitWithMessage("Unable to bind server");
        return 1;
    }

    //Listen to incoming connections
    listen(socket_desc, 3);


    //Accept and incoming connection
    if (debugLevel >= INFO) printf("INFO:\tWaiting for incoming connections on port %d\n", PORT);
    c = sizeof(struct sockaddr_in);
    pthread_t thread_id;

    /* Launch game controller thread */
    if (pthread_create(&thread_id, NULL, gameController, NULL) < 0) {
        perror("could not create thread");
        return 1;
    }


    while ((client_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t *) &c))) {
        printf("INFO:\tConnection accepted from %s \n", inet_ntoa(client.sin_addr));

        clientInfo_t *currentClient = initClientData(client_sock, client.sin_addr);

        //Launch player connection controller thread
        if (pthread_create(&thread_id, NULL, handle_connection, (void *) currentClient) < 0) {
            perror("could not create thread");
            return 1;
        }

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( thread_id , NULL);
    }

    if (client_sock < 0) {
        perror("accept failed");
        return 1;
    }

    return 0;

}

/**
 * Debugging function which is called when packet is sent/received prints out packet type and errno
 */
void debugPacket(enum packet_t packetType, const char *caller, const char *errorno) {
    switch (packetType) {
        case JOIN:
            printf("DEBUG:\t%s with type JOIN %s\n", caller, errorno);
            break;
        case ACK:
            printf("DEBUG:\t%s with type ACK %s\n", caller, errorno);
            break;
        case START:
            printf("DEBUG:\t%s with type START %s\n", caller, errorno);
            break;
        case END:
            printf("DEBUG:\t%s with type END %s\n", caller, errorno);
            break;
        case MAP:
            printf("DEBUG:\t%s with type MAP %s\n", caller, errorno);
            break;
        case MOVE:
            printf("DEBUG:\t%s with type MOVE %s\n", caller, errorno);
            break;
        case SCORE:
            printf("DEBUG:\t%s with type SCORE %s\n", caller, errorno);
            break;
        case MESSAGE:
            printf("DEBUG:\t%s with type MESSAGE %s\n", caller, errorno);
            break;
        case PLAYERS:
            printf("DEBUG:\t%s with type PLAYERS %s\n", caller, errorno);
            break;
        case QUIT:
            printf("DEBUG:\t%s with type QUIT %s\n", caller, errorno);
            break;
        case JOINED:
            printf("DEBUG:\t%s with type JOINED %s\n", caller, errorno);
            break;
        case PLAYER_DISCONNECTED:
            printf("DEBUG:\t%s with type PLAYER_DISCONNECTED %s\n", caller, errorno);
            break;
        default:
            printf("DEBUG:\t%s UNKNOWN PACKET %s\n", caller, errorno);
    }

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
    sendMassPacket(buffer, PACKET_TYPE_SIZE + sizeof(int), client);
}

void receivePacket(char *buffer, ssize_t *bufferPointer, int sock) {
    initPacket(buffer, bufferPointer);
    *bufferPointer = recv(sock, buffer, MAX_PACKET_SIZE, 0);
    if (debugLevel >= DEBUG) {
        debugPacket((enum packet_t) buffer[0], __func__, strerror(errno));
    }
}

/**
 * Sends the buffer to socket
 */
void sendPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client) {

    write(client->sock, buffer, (size_t) bufferPointer);
    if (debugLevel >= DEBUG) {
        debugPacket((enum packet_t) buffer[0], __func__, strerror(errno));
    }
}

/**
 * Initializes empty packet and bufferPointer
 */
void initPacket(char *buffer, ssize_t *bufferPointer) {
    *bufferPointer = 0;
    memset(buffer, 0, MAX_PACKET_SIZE);
}

/*
 * Send the passed packet to everyone except the passed client
 */
void sendMassPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] && clientArr[i] != client) {
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
        threadErrorHandler("INFO:\tUnauthenticated client disconnected", 1, clientInfo);
    } else if (bufferPointer == -1) {
        threadErrorHandler("INFO:\tRecv failed", 2, clientInfo);
    }
    if ((int) buffer[0] == JOIN) {
        memcpy(clientInfo->name, buffer + PACKET_TYPE_SIZE, MAX_NICK_SIZE);
        int nameSize = 20;
        stripSpecialCharacters(&nameSize, clientInfo->name);
        if (isNameUsed(clientInfo->name)) {
            initPacket(buffer, &bufferPointer);
            buffer[0] = ACK;
            int errval = ERROR_NAME_IN_USE;
            memcpy(buffer + PACKET_TYPE_SIZE, &errval, sizeof(int));
            sendPacket(buffer, 5, clientInfo);

            threadErrorHandler("INFO:\tName is in use", 5, clientInfo);
        }
        if (findClientSpot(clientInfo) == NULL) {
            initPacket(buffer, &bufferPointer);
            buffer[0] = ACK;
            int errval = ERROR_SERVER_FULL;
            memcpy(buffer + PACKET_TYPE_SIZE, &errval, sizeof(int));
            sendPacket(buffer, 5, clientInfo);
            threadErrorHandler("INFO:\tServer is full", 4, clientInfo);
        }

        // Everything OK, sending user ID
        initPacket(buffer, &bufferPointer);
        buffer[0] = ACK;
        memcpy(buffer + PACKET_TYPE_SIZE, &clientInfo->id, sizeof(int));
        sendPacket(buffer, sizeof(int) + PACKET_TYPE_SIZE, clientInfo);

        //Sending JOINED packet to everyone except current client
        initPacket(buffer, &bufferPointer);
        buffer[0] = JOINED;
        memcpy(buffer + PACKET_TYPE_SIZE, &clientInfo->id, sizeof(int)); //Player ID
        memcpy(buffer + PACKET_TYPE_SIZE + sizeof(int), &clientInfo->name, MAX_NICK_SIZE); //Player name
        sendMassPacket(buffer, sizeof(int) + PACKET_TYPE_SIZE + MAX_NICK_SIZE, clientInfo);


        printf("INFO:\tNew player %s(%d) from %s\n", clientInfo->name, clientInfo->id, inet_ntoa(clientInfo->ip));


    } else {
        threadErrorHandler("Incorrect command received", 3, clientInfo);
    }


}


/**
 * Function (in a seperate thread) which updates the client with game data (MAP/PLAYERS/SCORE)
 */
void *playerSender(void *clientP) {
    clientInfo_t *client = (clientInfo_t *) clientP;
    while (true) {
        while (gameStarted) {
            // Prepare MAP packet
            /*
             * 0 - PACKET TYPE
             * 1- height*width+1 Map data
             * Map data doesn't require map size since it is previously sent in the START packet
             */
            char buffer[MAX_MAP_HEIGHT * MAX_MAP_WIDTH + PACKET_TYPE_SIZE];
            buffer[0] = MAP;
            int bufferPointer = 1;
            for (int i = 0; i < MAP_CURRENT->height; i++) {
                for (int j = 0; j < MAP_CURRENT->width; j++) {
                    buffer[bufferPointer] = MAP_CURRENT->map[i][j];
                    bufferPointer++;
                }
            }
            sendPacket(buffer, bufferPointer, client);
            // Prepare PLAYERS packet
            /*
             * 0 - PACKET TYPE
             * 1-4 OBJECT COUNT
             * 5-19..19-33... Player information for each player 14 bytes(int(4)+float(4)+float(4)+PlayerState(1)+PlayerType(1))
             */

            memset(buffer, 0, MAX_PACKET_SIZE);
            buffer[0] = PLAYERS;
            int objectCount = 0; //Amount of player objects
            bufferPointer = 5; //Start of the player information in buffer
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (clientArr[i] && clientArr[i]->active) {
                    memcpy(buffer + bufferPointer, &clientArr[i]->id, sizeof(int)); //Player ID
                    bufferPointer += sizeof(int);
                    memcpy(buffer + bufferPointer, &clientArr[i]->x, sizeof(float)); //Player x coordinates
                    bufferPointer += sizeof(float);
                    memcpy(buffer + bufferPointer, &clientArr[i]->y, sizeof(float)); //Player y coordinates
                    bufferPointer += sizeof(float);
                    buffer[bufferPointer++] = clientArr[i]->playerState;
                    buffer[bufferPointer++] = clientArr[i]->playerType;
                    objectCount++;
                }
            }
            memcpy(buffer + PACKET_TYPE_SIZE, &objectCount, sizeof(int)); // Object count
            sendPacket(buffer, bufferPointer, client);

            // Prepare score packet
            /*
             * 0 - Packet type
             * 1 - 4 Object count
             * 5 - 8 Player score
             * 9 - 12 Player ID
             * Repeat player score and player ID for each player
             */
            objectCount = 0;
            memset(buffer, 0, MAX_PACKET_SIZE);
            buffer[0] = SCORE;
            bufferPointer = 5;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (clientArr[i] && clientArr[i]->active) {
                    memcpy(buffer + bufferPointer, &clientArr[i]->score, sizeof(int)); //Player score
                    bufferPointer += sizeof(int);
                    memcpy(buffer + bufferPointer, &clientArr[i]->id, sizeof(int)); //Player id
                    bufferPointer += sizeof(int);
                }
            }
            memcpy(buffer + 1, &objectCount, sizeof(int));
            sendPacket(buffer, bufferPointer, client);


            sleep_ms(TICK_FREQUENCY);

        }
        sleep_ms(TICK_FREQUENCY);
    }
    return 0;

}

/**
 * Function (in a seperate thread) which receives updates from the client (MOVE/MESSAGE/QUIT(PLAYER_DISCONNECTED))
 */
void *playerReceiver(void *client) {
    clientInfo_t *clientInfo = (clientInfo_t *) client;
    while (true) {
        char buffer[MAX_PACKET_SIZE];
        memset(buffer, 0, MAX_PACKET_SIZE);
        ssize_t bufferPointer = 0; //Stores received packet size
        int messageLength = 0;
        receivePacket(buffer, &bufferPointer, clientInfo->sock);
        switch (buffer[0]) {
            case MOVE:
                /*
                 * 1-4 Player ID (Not really required in stateful connection)
                 * 5 Player Move
                 */
                clientInfo->clientMovement = (enum clientMovement_t) buffer[5];
                break;
            case MESSAGE:
                /*
                 * 1-4 Player ID (Not really required in stateful connection)
                 * 5-8 Message length
                 * 9-... Message
                 */
                memcpy((void *) &messageLength, (void *) &buffer + 5, sizeof(int)); //Read message length
                if (bufferPointer - 9 > messageLength) {
                    if (debugLevel >= VERBOSE) {
                        printf("VERBOSE:\t%s is sending incorrect length messages (they are bigger than messageLength) DISCARDING\n",
                               clientInfo->name);
                    }
                } else {
                    sendMessage(clientInfo->id, messageLength, buffer + 9);
                }
                break;
            case QUIT:
                /*
                 * 1-4 Player ID (Not really required in stateful connection)
                 */
                processQuit(clientInfo);
                break;
        }

        sleep_ms(TICK_FREQUENCY);
    }
    return 0;
}


/**
 * Sends a special character stripped message to all players, playerId must be validated before sending
 */
void sendMessage(int playerId, int messageLength, char *message) {
    char buffer[MAX_PACKET_SIZE];
    if (MAX_PACKET_SIZE < messageLength) {
        if (debugLevel >= VERBOSE) {
            printf("VERBOSE:\t%d is sending too large messages\n", playerId);
        }
    }
    stripSpecialCharacters(&messageLength, message);

    //Prepare MESSAGE packet
    /*
     * 1-4 Player ID (Not really required in stateful connection)
     * 5-8 Message length
     * 9-... Message
     */
    buffer[0] = MESSAGE;
    memcpy(buffer + 1, &playerId, sizeof(int)); // Player ID
    memcpy(buffer + 5, &messageLength, sizeof(int)); // Message length
    for (int i = 0; i < messageLength; i++) {
        buffer[i + 9] = message[i];     // Message
    }
    int bufferPointer = PACKET_TYPE_SIZE + sizeof(int) + sizeof(int) + messageLength;


    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i]) {
            sendPacket(buffer, bufferPointer, clientArr[i]);
        }
    }

}

void processQuit(clientInfo_t *client) {
    //Prepare PLAYER_DISCONNECTED packet
    char buffer[5];
    buffer[0] = PLAYER_DISCONNECTED;
    memcpy(buffer + 1, &client->id, sizeof(int)); // Player ID
    sendMassPacket(buffer, 5, client);
    //TODO Clean up player data (pthread_kill);

}

/**
 * Removes special characters (i.e \n) which should never be sent to clients
 * Should be used for messages and player names
 * Resulting string stored in message with messageLength updated
 */
void stripSpecialCharacters(int *messageLength, char *message) {
    int newMessageLength = 0;
    char newMessage[MAX_PACKET_SIZE];
    for (int i = 0; i < *messageLength; i++) {
        if (message[i] >= 32 && message[i] <= 126) {
            newMessage[newMessageLength++] = message[i];
        }
    }
    newMessage[newMessageLength++] = '\0'; //Make sure that the message ends with null character
    *messageLength = newMessageLength;
    memset(message, 0, (size_t) *messageLength);
    memcpy(message, newMessage, (size_t) newMessageLength);
}

/**
 * Client connection thread which operates the open socket to the client
 */
void *handle_connection(void *conn) {
    // Get the socket descriptor
    clientInfo_t *clientInfo = (clientInfo_t *) conn;
    int sock = clientInfo->sock;

    // New client connection
    processNewPlayer(clientInfo);

    // If the game had already started and the player was not processed during start we have to also send the START packet
    if (gameStarted && !clientInfo->active) {
        char buffer[MAX_PACKET_SIZE] = {0};
        prepareStartPacket(buffer, clientInfo);
        if (debugLevel >= DEBUG) printf("DEBUG:\t%s joined late, also sending START packet\n", clientInfo->name);
        sendPacket(buffer, 5, clientInfo);
    }

    pthread_t thread_id;
    // Create seperate game handler threads for client
    // First thread - sends game data to the client (MAP/PLAYERS/SCORE)
    if (pthread_create(&thread_id, NULL, playerSender, (void *) clientInfo) < 0) {
        threadErrorHandler("Could not create playerSender thread", 7, clientInfo);
    }


    // Second thread for receiving messages from client (MOVE/MESSAGE/QUIT(PLAYER_DISCONNECTED))
    if (pthread_create(&thread_id, NULL, playerReceiver, (void *) clientInfo) < 0) {
        threadErrorHandler("Could not create playerReceiver thread", 8, clientInfo);
    }


    return 0;
}

void sleep_ms(int milliseconds) // cross-platform sleep function
{
#ifdef WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    usleep(milliseconds * 1000);
#endif
}

/**
 * Returns count of all connected players, including those which game specific variables HAVEN'T been initialized
 */
unsigned int getPlayerCount() {
    unsigned int players = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] != NULL) players++;
    }
    return players;
}

/**
 * Returns count of players which game specific variables have been initialized
 */
unsigned int getActivePlayerCount() {
    unsigned int players = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] != NULL && clientArr[i]->active) players++;
    }
    return players;
}

/*
 * Sends game start packet to all clients
 */
void sendStartPackets() {
    char buffer[MAX_PACKET_SIZE];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        memset(buffer, 0, MAX_PACKET_SIZE);
        if (clientArr[i] != NULL) {
            prepareStartPacket(buffer, clientArr[i]);
            if (debugLevel >= DEBUG) printf("DEBUG:\tSending START packet to %s\n", clientArr[i]->name);
            sendPacket(buffer, 5, clientArr[i]);
        }
    }
}

/**
 * Checks if there is anyone at the given coordinates, returns the client, else NULL
 */
clientInfo_t *isSomeoneThere(int x, int y) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] != NULL) {
            if (clientArr[i]->active && (int) clientArr[i]->x == x && (int) clientArr[i]->y == y) {
                return clientArr[i];
            }
        }
    }
    return NULL;
}


void findStartingPosition(clientInfo_t *client) {

    if (client->playerType == Pacman) { // If Pacman start search in the upper left corner
        int rows = MAP_CURRENT->height;
        int cols = MAP_CURRENT->width;
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                if (MAP_CURRENT->map[i][j] != Wall &&
                    MAP_CURRENT->map[i][j] != None) { //Try to find possible spawn point
                    // Traverse close blocks to see if there aren't any Ghosts
                    // We need to make sure that we do not check negative array values
                    bool enemyFound = false;
                    //Do not spawn on friendlies
                    if (isSomeoneThere(i, j) && isSomeoneThere(i, j)->playerType == Pacman) {
                        continue;
                    }

                    for (int ii = (i - SPAWNPOINT_TRAVERSAL_RANGE < 0) ? i : i - SPAWNPOINT_TRAVERSAL_RANGE;
                         ii < i + SPAWNPOINT_TRAVERSAL_RANGE && i + SPAWNPOINT_TRAVERSAL_RANGE <= rows; ii++) {
                        for (int jj = (j - SPAWNPOINT_TRAVERSAL_RANGE < 0) ? j : j - SPAWNPOINT_TRAVERSAL_RANGE;
                             jj < j + SPAWNPOINT_TRAVERSAL_RANGE && j + SPAWNPOINT_TRAVERSAL_RANGE <= cols; jj++) {
                            clientInfo_t *contender = isSomeoneThere(ii, jj);

                            //Make sure that player is not spawned near enemy
                            if (contender && contender->playerType == Ghost) {
                                enemyFound = true;
                                break;
                            }
                        }
                        if (enemyFound) break;
                    }
                    if (enemyFound == false) {
                        client->x = (float) i;
                        client->y = (float) j;
                        if (debugLevel >= VERBOSE)
                            printf("VERBOSE:\t%s will start at (%d:%d)\n", client->name, (int) client->x,
                                   (int) client->y);
                        return;
                    }
                }
            }
        }
    } else if (client->playerType == Ghost) { // If Ghost start search in lower right corner
        int rows = MAP_CURRENT->height;
        int cols = MAP_CURRENT->width;
        bool spotFound = false;
        for (int i = rows; i >= 0; i--) {
            for (int j = cols; j >= 0; j--) {
                if (MAP_CURRENT->map[i][j] != Wall &&
                    MAP_CURRENT->map[i][j] != None) { //Try to find possible spawn point
                    // Traverse close blocks to see if there aren't any Ghosts
                    // We need to make sure that we do not check negative array values
                    bool enemyFound = false;
                    //Do not spawn on friendlies
                    if (isSomeoneThere(i, j) && isSomeoneThere(i, j)->playerType == Ghost) {
                        continue;
                    }
                    for (int ii = (i - SPAWNPOINT_TRAVERSAL_RANGE < 0) ? i : i - SPAWNPOINT_TRAVERSAL_RANGE;
                         ii < i + SPAWNPOINT_TRAVERSAL_RANGE && i + SPAWNPOINT_TRAVERSAL_RANGE <= rows; ii++) {
                        for (int jj = (j - SPAWNPOINT_TRAVERSAL_RANGE < 0) ? j : j - SPAWNPOINT_TRAVERSAL_RANGE;
                             jj < j + SPAWNPOINT_TRAVERSAL_RANGE && j + SPAWNPOINT_TRAVERSAL_RANGE <= cols; jj++) {
                            clientInfo_t *contender = isSomeoneThere(ii, jj);

                            //Make sure that player is not spawned near enemy
                            if (contender && contender->playerType == Pacman) {
                                enemyFound = true;
                                break;
                            }
                        }
                        if (enemyFound) break;
                    }
                    if (enemyFound == false) {
                        client->x = (float) i;
                        client->y = (float) j;
                        if (debugLevel >= VERBOSE)
                            printf("VERBOSE:\t%s will start at (%d:%d)\n", client->name, (int) client->x,
                                   (int) client->y);
                        return;
                    }
                }
            }
        }
    }

}

/*
 * Decides if the player should be Pacman or Ghost
 */
void pacmanOrGhost(clientInfo_t *client) {
    unsigned int state = getActivePlayerCount() % (GHOST_RATIO + PACMAN_RATIO);
    if (state < GHOST_RATIO) {
        client->playerType = Ghost;
        if (debugLevel >= VERBOSE) printf("VERBOSE:\t%s will be a GHOST \n", client->name);
    }
    if (state >= GHOST_RATIO) {
        client->playerType = Pacman;
        if (debugLevel >= VERBOSE) printf("VERBOSE:\t%s will be a PACMAN \n", client->name);
    }
}

/*
 * Prepares start packet for specific client
 */
void prepareStartPacket(char *buffer, clientInfo_t *client) {
    buffer[0] = START;
    // Prepares map data for client
    // Map width
    buffer[1] = (char) MAP_CURRENT->width;
    // Map height
    buffer[2] = (char) MAP_CURRENT->height;

    // Calculates if player should be Pacman or Ghost
    pacmanOrGhost(client);

    // Make sure that the player is alive at the start of the game
    client->playerState = NORMAL;
    client->active = true;

    // Finds suitable starting position for client
    findStartingPosition(client);

    buffer[3] = (char) client->x;
    buffer[4] = (char) client->y;
}


void *gameController(void *a) {
    printf("INFO:\tGame controller started\n");
    MAP_CURRENT = MAP_HEAD;
    unsigned long int TICK = 0;
    gameStarted = false;
    while (true) {
        sleep_ms(TICK_FREQUENCY);
        if (getPlayerCount() >= MIN_PLAYERS || gameStarted) {
            if (TICK == 0) {
                gameStarted = true;
                if (debugLevel >= DEBUG) printf("DEBUG:\tGame started, sending START packets\n");
                sendStartPackets();
            }
            processTick();
            TICK += 1;
            if (debugLevel >= DEBUG) printf("DEBUG:\tTICK %lu\n", TICK);
        }
    }
}

/**
 * Collision detection, powerup and player movement function
 */
void processTick(unsigned long int TICK) {
    //Check of player has any powerups and if there are decrease their tick

    //Find if a player is eligible for powerup by stepping on it


    //Check if player has a collision with something

    /*
     * Ghost -> Pacman
     * Pacman DEAD if he does not have invincibility
     */



    /* Pacman- -> Powerup
     * Check what kind of powerup it is
     * Same as existing - reset powerup ticks to default
     * If it is powerPellet override invincibility and adjust ticks
     * If it is invincibility adjust ticks
     * If it is SCORE increase players score
     * Remove powerup from tile
     */


    /* Ghost -> Powerup (do we even need this?)
     *  ??? maybe he can eat powerPellet for more speed
     */


    /* Pacman-> Dot
     *  If tile is a dot increase players score, remove dot
     */



    /* Ghost -> Dot (do we even need this?)
     * ???
     */

    /*
     * Pacman -> Ghost
     * If Pacman has PowerPellet and they are on the same tile GHOST=DEAD
     */



    /* Both -> Wall
     * If in the next tile of player MOVE is a wall do not change its position
     * else move the player by TICK_MOVEMENT
     */


    //Spawn a powerup in almost random position



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
    int y = 1;
    do {
        c = getc(mapfile);
        if (c == '\n') {
            y++;
            x = 0;
        } else if (c == EOF) {
            break;
        } else {
            c = c -
                '0'; //The data in file are char type, but the map data should have char value of 0/1/2... not 47/48/49...
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
    if (debugLevel >= VERBOSE) printf("VERBOSE:\tMap %s loaded, length x=%d, y=%d\n", name, map->width, map->height);

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
                fprintf(stderr, "INFO:\tFailed to open %s, skipping\n", ent->d_name);
                fclose(open_file);
                continue;
            }
            addMap(open_file, ent->d_name);
            fclose(open_file);
        }
    }
    if (MAP_HEAD == NULL) {
        exitWithMessage("No maps loaded");
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
    gameStarted = false;
}


int main(int argc, char *argv[]) {

    signal(SIGINT, signal_callback_handler);
    initVariables();
    processArgs(argc, argv);
    initMaps();
    startServer();


    return 0;
}
