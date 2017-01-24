/*
 * LSP Kursa projekts
 * Serveris
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
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>

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
#define MIN_PLAYERS 2
#define TICK_FREQUENCY 50                   // Time between ticks in miliseconds
#define GHOST_RATIO 1                         // Ratio of ghosts per one pacman
#define PACMAN_RATIO 2                        // Ratio of Pacmans per one ghost
#define SPAWNPOINT_TRAVERSAL_RANGE 5          // Nearby blocks to be checked for enemies when spawning
#define TICK_MOVEMENT 0.5f                    // Player movement per each tick
#define DOT_POINTS 10                         // Points given for encountering DOT tole
#define SCORE_POINTS 100                      // Points given for encountering SCORE tile
#define POWERUP_PowerPellet_TICKS 120         // Ticks before PowerPellet expires
#define POWERUP_Invincibility_TICKS 120       // Ticks before Invincibility expires
#define POWERUP_START_Invincibility_TICKS 60  // Ticks count of Invincibility upon player spawn
#define SCORE_GHOST_KILL 1                    // Score Ghost gets for killing Pacman
#define SCORE_PACMAN_KILL 1                   // Score Pacman gets for killing Ghost
#define POWERUP_PowerPellet_SPAWN_TICKS 500      // Amount of ticks between spawning powerPellet
#define POWERUP_Invincibility_SPAWN_TICKS 250    // Amount of ticks between spawning Invincibility

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
    NORMAL, DEAD, powerupPowerPellet = 3, powerupInvincibility = 4
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

void *handle_connection(void *);

void processArgs(int argc, char *argv[]);

void *safeMalloc(size_t);

int startServer();

void initPacket(char *, ssize_t *);

void sendMassPacket(char *, ssize_t, clientInfo_t *);

clientInfo_t *initClientData(int, struct in_addr);

void sendPlayerDisconnect(clientInfo_t *);

void *gameController(void *a);

void prepareStartPacket(char *, clientInfo_t *);

void sleep_ms(int);

unsigned int getPlayerCount();

void stripSpecialCharacters(int *, char *);

void sendMessage(int, int, char *);

void processQuit(clientInfo_t *);

void processTick(unsigned long int *);

bool sameTile(clientInfo_t *, clientInfo_t *);

void threadErrorHandler(char errormsg[], int, clientInfo_t *);

void debugPacket(char *, const char *, const char *);

void initVariables();

void initMaps();

void addMap(FILE *, char name[256]);

void sendStartPackets();

void processNewPlayer(clientInfo_t *);

void *playerSender(void *);

void *playerReceiver(void *);

/*
 * Structs
 */

typedef struct clientInfo {                 // Holds client specific data
    int sock;                               // Client TCP socket
    int id;                                 // Client ID
    struct in_addr ip;                      // Client IP address
    char name[20];                          // Client name
    enum playerType_t playerType;           // Client type (initialized if active=true)
    enum playerState_t playerState;         // Client state (initialized if active=true)
    enum clientMovement_t clientMovement;   // Client movement (UP/DOWN/LEFT/RIGHT)
    unsigned int powerupTick;               // Ticks before client powerup expires
    float x;                                // x coordinates
    float y;                                // x coordinates
    int score;                              // Player score
    bool active;                            // Tells if client type, state has been initialized
    pthread_t connection_handler_thread_id; // Thread ID of connection handler
    pthread_t packet_sndr_thread_id;        // Thread ID of packet sender
    pthread_t packet_rcv_thread_id;         // Thread ID of packet receiver
} clientInfo_t;


typedef struct mapList {                            //Contains list of loaded maps, populated by initMaps
    char filename[FILENAME_MAX];                    //Map filename
    int width;                                      //x
    int height;                                     //y
    char map[MAX_MAP_WIDTH][MAX_MAP_HEIGHT];        //Map during game, might change during gameplay
    char mapDefault[MAX_MAP_WIDTH][MAX_MAP_HEIGHT]; //Map which was loded from file
    struct mapList *next;
} mapList_t;


/*
 * Globals
 */
clientInfo_t *clientArr[MAX_PLAYERS];   // Array holding all player data
pthread_mutex_t clientArrLock;          // Mutex locking clientArr
int PORT;                               // Server port (-p)
char MAPDIR[FILENAME_MAX];              // Directory containing maps (-m)
mapList_t *MAP_HEAD;                    // Pointer to the first MAP
bool gameStarted;                       // True if the game is in progress
pthread_mutex_t gameStartedock;         // Mutex locking gameStarted
mapList_t *MAP_CURRENT;                 // Pointer to the current loaded MAP
enum debugLevel_t debugLevel;           // Holds debugging level of the server (-v/-vv)


/*
 * Helper functions
 */

/**
 * Safe malloc implementation
 */
void *safeMalloc(size_t size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}

/**
 * Main thread failure function, called when an fatal error occurs
 */
void exitWithMessage(char error[]) {
    printf("%s\n", error);
    exit(EXIT_FAILURE);
}

/**
 * Returns initialized client struct
 */
clientInfo_t *initClientData(int sock, struct in_addr ip) {
    static unsigned int CLIENT_ID_ITERATOR = 1;
    pthread_mutex_lock(&clientArrLock);
    clientInfo_t *client = safeMalloc(sizeof(clientInfo_t));
    client->id = CLIENT_ID_ITERATOR++;  // Player ID
    client->sock = sock;                // Player TCP socket
    client->ip = ip;                    // Player IP address
    client->active = false;             // Active will be set to true only when game starts and player is sent STARt packet
    client->packet_rcv_thread_id = 0;     // Client packet receiver thread
    client->packet_sndr_thread_id = 0;    // Client packet sender thread
    pthread_mutex_unlock(&clientArrLock);
    return client;
}

/**
 * Returns client pointer to array or NULL if no free spots
 */
clientInfo_t *findClientSpot(clientInfo_t *client) {
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] == NULL) {
            clientArr[i] = client;
            pthread_mutex_unlock(&clientArrLock);
            return client;
        }
    }
    pthread_mutex_unlock(&clientArrLock);
    return NULL;
}

/**
 * Returns true if the name is in use, false otherwise
 */
bool isNameUsed(char *name) {
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] != NULL) {
            if (strcmp(clientArr[i]->name, name) == 0) {
                pthread_mutex_unlock(&clientArrLock);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&clientArrLock);
    return false;
}

/**
 * Initializes empty packet and bufferPointer
 */
void initPacket(char *buffer, ssize_t *bufferPointer) {
    *bufferPointer = 0;
    memset(buffer, 0, MAX_PACKET_SIZE);
}

/**
 * Receives data from buffer, received data is stored in buffer
 */
void receivePacket(char *buffer, ssize_t *bufferPointer, clientInfo_t *client) {
    initPacket(buffer, bufferPointer);
    *bufferPointer = recv(client->sock, buffer, MAX_PACKET_SIZE, 0);
    if (*bufferPointer <= 0) threadErrorHandler("Lost connection with player", 10, client);
    if (debugLevel >= DEBUG) {
        debugPacket(buffer, __func__, strerror(errno));
    }
}

/**
 * Sends the buffer to socket
 */
void sendPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client) {

    write(client->sock, buffer, (size_t) bufferPointer);
    if (debugLevel >= DEBUG) {
        debugPacket(buffer, __func__, strerror(errno));
    }
}


/**
 * Debugging function which is called when packet is sent/received prints out packet type and errno
 */

void debugPacket(char *buffer, const char *caller, const char *errorno) {
    switch (buffer[0]) {
        case JOIN:
            printf("DEBUG:\t%s with type JOIN %s\n", caller, errorno);
            break;
        case ACK:
            printf("DEBUG:\t%s with type ACK %s\n", caller, errorno);
            break;
        case START:
            printf("DEBUG:\t%s with type START MAP(%d:%d), POS(%d:%d) %s\n", caller, (int) buffer[1],
                   (int) buffer[2],
                   (int) buffer[3], (int) buffer[4], errorno);
            break;
        case END:
            printf("DEBUG:\t%s with type END %s\n", caller, errorno);
            break;
        case MAP:
            printf("DEBUG:\t%s with type MAP %s\n", caller, errorno);
            break;
        case MOVE:
            switch (buffer[5]) {
                case UP:
                    printf("DEBUG:\t%s with type MOVE (UP) %s\n", caller, errorno);
                    break;
                case DOWN:
                    printf("DEBUG:\t%s with type MOVE (DOWN) %s\n", caller, errorno);
                    break;
                case LEFT:
                    printf("DEBUG:\t%s with type MOVE (LEFT) %s\n", caller, errorno);
                    break;
                case RIGHT:
                    printf("DEBUG:\t%s with type MOVE (RIGHT) %s\n", caller, errorno);
                    break;
                default:
                    printf("DEBUG:\t%s with type MOVE (UNKNOWN!!) %s\n", caller, errorno);
            }
            break;
        case SCORE:
            printf("DEBUG:\t%s with type SCORE %s\n", caller, errorno);
            break;
        case MESSAGE:
            printf("DEBUG:\t%s with type MESSAGE %s\n", caller, errorno);
            break;
        case PLAYERS:
            printf("DEBUG:\t%s with type PLAYERS (%d object) %s\n", caller, (int) buffer[1], errorno);
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
 *  Exits packetSender and packetReceiver threads if they have been created
 *  Exits the main client connection thread
 */
void threadErrorHandler(char errormsg[], int retval, clientInfo_t *client) {
    printf("INFO: %s: %s\n", inet_ntoa(client->ip), errormsg);
    close(client->sock);
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (client == clientArr[i]) {
            clientArr[i] = NULL;
            sendPlayerDisconnect(client);
        }
    }
    if (client->packet_rcv_thread_id != 0) pthread_cancel(client->packet_rcv_thread_id);
    if (client->packet_sndr_thread_id != 0) pthread_cancel(client->packet_sndr_thread_id);
    free(client);
    pthread_mutex_unlock(&clientArrLock);
    pthread_exit(&retval);
}

/**
 * Removes special characters (i.e \n) which should never be sent to clients
 * Should be used for messages and player names
 * Resulting string stored in message with messageLength updated
 */
void stripSpecialCharacters(int *messageLength, char *message) {
    size_t newMessageLength = 0;
    char newMessage[MAX_PACKET_SIZE];
    for (int i = 0; i < *messageLength; i++) {
        if (message[i] >= 32 && message[i] <= 126) {
            newMessage[newMessageLength++] = message[i];
        }
    }
    newMessage[newMessageLength++] = '\0'; //Make sure that the message ends with null character
    memcpy(message, newMessage, newMessageLength);
}


/**
 * Starting point
 */
int main(int argc, char *argv[]) {

    initVariables();
    processArgs(argc, argv);
    initMaps();
    startServer();
    return 0;
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

/**
 * Reads maps from the MAPDIR directory into mapList struct list
 */
void initMaps() {

    DIR *desc; //Directory descriptor
    struct dirent *ent;
    FILE *open_file;


    //Open directory
    if (NULL == (desc = opendir(MAPDIR))) {
        exitWithMessage("Unable to open map directory");
    }
    //Iterate through directory files
    while ((ent = readdir(desc))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue; //We do not want to follow current and upper directory hard links
        // If the directory contains file
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
            //Pass to addMap function which tries to load file
            addMap(open_file, ent->d_name);
            fclose(open_file);
        }
    }
    if (MAP_HEAD == NULL) {
        exitWithMessage("ERROR: No maps loaded");
    }
}

void addMap(FILE *mapfile, char name[256]) {
    mapList_t *map = safeMalloc(sizeof(mapList_t));
    // Initialize map metadata
    map->next = NULL;
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
    map->height = ++y;
    memset(map->mapDefault, 0, MAX_MAP_HEIGHT * MAX_MAP_WIDTH);
    memcpy(map->mapDefault, map->map, MAX_MAP_HEIGHT * MAX_MAP_WIDTH);
    if (debugLevel >= VERBOSE)
        printf("VERBOSE:\tMap %s loaded, length x=%d, y=%d\n", name, map->width, map->height);

}

/**
 * Main server thread which listens to incoming connections
 * Creates a new gameController thread which controls the game process
 * If a new client connection is received a new thread (handle_connection) is created which authorizes the client and
 * creates client specific packet receiver (playerReceiver) and packet sender thread (playerSender) in which game data
 * is being sent
 */
int startServer() {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    // set SO_REUSEADDR on a socket to true (1):
    int optval = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval); //Allow reuse of addresses
    if (socket_desc == -1 || optval == -1) {
        exitWithMessage("Unable to create a socket");
    }
    optval = 1;
    setsockopt(socket_desc, IPPROTO_TCP, TCP_NODELAY, (char *) &optval,
               sizeof(optval)); //Make sure the packets aren't buffered
    if (socket_desc == -1 || optval == -1) {
        exitWithMessage("ERROR: Unable to create a socket");
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY; // Listen to all interfaces
    server.sin_port = htons((uint16_t) PORT);       // Listening port

    //Binds TCP
    if (bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) < 0) {
        exitWithMessage("ERROR:\tUnable to bind server");
        return 1;
    }

    //Listen to incoming connections
    listen(socket_desc, 3);


    //Accept and incoming connection
    if (debugLevel >= INFO) printf("INFO:\tWaiting for incoming connections on port %d\n", PORT);
    c = sizeof(struct sockaddr_in);
    pthread_t thread_id;

    // Launch game controller thread
    if (pthread_create(&thread_id, NULL, gameController, NULL) < 0) {
        perror("could not create thread");
        return 1;
    }


    while ((client_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t *) &c))) {
        printf("INFO:\tConnection accepted from %s \n", inet_ntoa(client.sin_addr));

        clientInfo_t *currentClient = initClientData(client_sock, client.sin_addr);

        //Launch player connection controller thread
        if (pthread_create(&currentClient->connection_handler_thread_id, NULL, handle_connection,
                           (void *) currentClient) < 0) {
            perror("could not create thread");
            return 1;
        }


        //Now join the thread , so that we dont terminate before the thread
    }
    //pthread_join(thread_id, NULL);

    if (client_sock < 0) {
        perror("accept failed");
        return 1;
    }

    return 0;

}

/**
 * Game controller thread which executes when game is started, keep care of TICK counter and makes sure
 * that all players receive Start packets
 */
void *gameController(void *a) {
    printf("INFO:\tGame controller started\n");
    MAP_CURRENT = MAP_HEAD;
    unsigned long int TICK = 0;
    gameStarted = false;
    while (true) {
        sleep_ms(TICK_FREQUENCY);
        if (getPlayerCount() >= MIN_PLAYERS || gameStarted) {
            if (TICK == 0) {
                pthread_mutex_lock(&gameStartedock);
                gameStarted = true;
                pthread_mutex_unlock(&gameStartedock);
                if (debugLevel >= DEBUG) printf("DEBUG:\tGame started, sending START packets\n");
                sendStartPackets();
            }
            TICK += 1;
            processTick(&TICK);

            /*
            * Check if game ending condition is met
            *  1) Only Ghosts left
            *  2) Only Pacmans left
            *  3) No more dots
            */
            int ghostCount = 0;
            int pacmanCount = 0;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (clientArr[i] && clientArr[i]->active) {
                    if (clientArr[i]->playerType == Ghost) ghostCount++; //Count ghosts
                    else if (clientArr[i]->playerType == Pacman) pacmanCount++; //Count pacmans
                }
            }
            //Check if there are any leftover dots
            bool dotFound = false;
            for (int i = 0; i < MAP_CURRENT->height; i++) {
                for (int j = 0; j < MAP_CURRENT->width; j++) {
                    enum mapObjecT_t mapObject = (enum mapObjecT_t) MAP_CURRENT->map[i][j];
                    if (mapObject == Dot) {
                        dotFound = true;
                        break;
                    }
                }
                if (dotFound) break;
            }

            // CHECK FOR END GAME
            bool gameEnd = false;
            if (ghostCount > 0 && pacmanCount == 0) {
                /*
                 * Handles games ending and new map initialization
                 * Receives winning player type and announces the winner via message
                 * Starts new game with the next map
                 */
                //Send message to all players
                sendMessage(0, 18, "Ghosts have won!");
                if (debugLevel >= VERBOSE) printf("VERBOSE:\tGAME END, Ghosts win\n");
                gameEnd = true;


            } // No more pacman ghosts win
            else if (pacmanCount > 0 && ghostCount == 0 || !dotFound) {
                sendMessage(0, 18, "Pacmans have won!");
                if (debugLevel >= VERBOSE) printf("VERBOSE:\tGAME END, Pacmans win\n");
                gameEnd = true;

            } // No more ghosts pacman win

            if (gameEnd && TICK > 3) {
                /*
                 * Prepare END packet
                */
                char buffer[PACKET_TYPE_SIZE];
                memset(buffer, 0, MAX_PACKET_SIZE);
                pthread_mutex_lock(&gameStartedock);
                pthread_mutex_lock(&clientArrLock);
                buffer[0] = END;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (clientArr[i] && clientArr[i]->active) {
                        sendPacket(buffer, PACKET_TYPE_SIZE,
                                   clientArr[i]); //Send END packet to all players which received START
                        clientArr[i]->active = false; //Deactivate player
                    }
                }
                pthread_mutex_unlock(&clientArrLock);
                gameStarted = false;
                pthread_mutex_unlock(&gameStartedock);

                //Reset ticks, map data
                TICK = 0;
                // Resets map tiles to default values since they have changed during game
                memcpy(MAP_CURRENT->map, MAP_CURRENT->mapDefault, MAX_MAP_HEIGHT * MAX_MAP_WIDTH);
                //Go to next map
                if (MAP_CURRENT->next) {
                    MAP_CURRENT = MAP_CURRENT->next;
                } else {
                    MAP_CURRENT = MAP_HEAD;
                }
            }


            if (debugLevel >= DEBUG) printf("DEBUG:\tTICK %lu\n", TICK);
        }
    }
}

/**
 * Client connection thread which operates the open socket to the client
 * Passes the client to processNewPlayer to authorize client
 * If player was not sent START packet sends it (for late joins, when game is in progress)
 * Creates playerSender and playerReceiver threads which send and receive player data during game
 */
void *handle_connection(void *conn) {
    // Get the socket descriptor
    clientInfo_t *clientInfo = (clientInfo_t *) conn;

    // New client connection, authorize the client
    processNewPlayer(clientInfo);

    // If the game had already started and the player was not processed during start we have to also send the START packet
    pthread_mutex_lock(&gameStartedock);
    if (gameStarted && !clientInfo->active) {
        char buffer[MAX_PACKET_SIZE] = {0};
        prepareStartPacket(buffer, clientInfo);
        if (debugLevel >= DEBUG) printf("DEBUG:\t%s joined late, also sending START packet\n", clientInfo->name);
        sendPacket(buffer, 5, clientInfo);
    }
    pthread_mutex_unlock(&gameStartedock);

    // Create seperate game handler threads for client
    // First thread - sends game data to the client (MAP/PLAYERS/SCORE)
    if (pthread_create(&clientInfo->packet_sndr_thread_id, NULL, playerSender, (void *) clientInfo) < 0) {
        threadErrorHandler("Could not create playerSender thread", 7, clientInfo);
    }


    // Second thread for receiving messages from client (MOVE/MESSAGE/QUIT(PLAYER_DISCONNECTED))
    if (pthread_create(&clientInfo->packet_rcv_thread_id, NULL, playerReceiver, (void *) clientInfo) < 0) {
        threadErrorHandler("Could not create playerReceiver thread", 8, clientInfo);
    }


    return 0;
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
    receivePacket(buffer, &bufferPointer, clientInfo);
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
        int clientTicker = 0; //Used to send players only per X packets
        while (gameStarted) {
            pthread_mutex_lock(&gameStartedock);
            pthread_mutex_lock(&clientArrLock);
            int bufferPointer = 1;
            char buffer[MAX_MAP_HEIGHT * MAX_MAP_WIDTH + PACKET_TYPE_SIZE];
            int objectCount = 0;

            if (clientTicker % 5 == 0) {
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
                pthread_mutex_unlock(&clientArrLock);
                pthread_mutex_unlock(&gameStartedock);
                clientTicker++;
                sleep_ms(TICK_FREQUENCY);
                continue;

            }
            // Prepare MAP packet
            /*
             * 0 - PACKET TYPE
             * 1- height*width+1 Map data
             * Map data doesn't require map size since it is previously sent in the START packet
             */
            bufferPointer = 1;
            memset(buffer, 0, MAX_PACKET_SIZE);
            buffer[0] = MAP;
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
             * 5-19..20-33... Player information for each player 14 bytes(int(4)+float(4)+float(4)+PlayerState(1)+PlayerType(1))
             */

            memset(buffer, 0, MAX_PACKET_SIZE);
            buffer[0] = PLAYERS;
            objectCount = 0; //Amount of player objects
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


            clientTicker++;
            pthread_mutex_unlock(&clientArrLock);
            pthread_mutex_unlock(&gameStartedock);
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
        receivePacket(buffer, &bufferPointer, clientInfo);
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
            default:
                break;
        }

        sleep_ms(TICK_FREQUENCY);
    }
    return 0;
}


void sendPlayerDisconnect(clientInfo_t *client) {
    char buffer[MAX_PACKET_SIZE] = {0};
    buffer[0] = PLAYER_DISCONNECTED;
    memcpy(buffer + PACKET_TYPE_SIZE, &client->id, sizeof(int));
    sendMassPacket(buffer, PACKET_TYPE_SIZE + sizeof(int), client);
}


/*
 * Send the passed packet to everyone except the passed client
 */
void sendMassPacket(char *buffer, ssize_t bufferPointer, clientInfo_t *client) {
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i] && clientArr[i] != client) {
            sendPacket(buffer, bufferPointer, clientArr[i]);
        }
    }
    pthread_mutex_unlock(&clientArrLock);
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
    if (playerId != 0) stripSpecialCharacters(&messageLength, message);

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

    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientArr[i]) {
            sendPacket(buffer, bufferPointer, clientArr[i]);
        }
    }
    pthread_mutex_unlock(&clientArrLock);

}

void processQuit(clientInfo_t *client) {
    //Prepare PLAYER_DISCONNECTED packet
    char buffer[5];
    buffer[0] = PLAYER_DISCONNECTED;
    memcpy(buffer + 1, &client->id, sizeof(int)); // Player ID
    sendMassPacket(buffer, 5, client);

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

/**
 * Sends game start packet to all clients
 */
void sendStartPackets() {
    char buffer[MAX_PACKET_SIZE];
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        memset(buffer, 0, MAX_PACKET_SIZE);
        if (clientArr[i] != NULL) {
            prepareStartPacket(buffer, clientArr[i]);
            if (debugLevel >= DEBUG) printf("DEBUG:\tSending START packet to %s\n", clientArr[i]->name);
            sendPacket(buffer, 5, clientArr[i]);
        }
    }
    pthread_mutex_unlock(&clientArrLock);
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

/**
 * Looks for adequate player spawning position on the map
 */
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
                        client->x = (float) j;
                        client->y = (float) i;
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
                        client->x = (float) j;
                        client->y = (float) i;
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

/**
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

/**
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
    client->powerupTick = 0;

    // If player is a Pacman start with invincibility
    if (client->playerType == Pacman) {
        client->powerupTick = POWERUP_START_Invincibility_TICKS;
        client->playerState = powerupInvincibility;
    }

    // Finds suitable starting position for client
    findStartingPosition(client);

    buffer[3] = (char) client->x;
    buffer[4] = (char) client->y;
}




/**
 * Function which compares two player coordinates and returns true if both of them are to be
 * considered to be on the same tile
 */
bool sameTile(clientInfo_t *a, clientInfo_t *b) {
    return (int) a->x == (int) b->x && (int) a->y == (int) b->y;
}

/**
 * Receives client and returns the mapObject client is standing on
 */
enum mapObjecT_t whichMapObject(clientInfo_t *a) {
    return (enum mapObjecT_t) MAP_CURRENT->map[(int) a->y][(int) a->x];
}

/**
 * Resets map object to None on the tile which client is standing on
 */
void resetMapObject(clientInfo_t *a) {
    MAP_CURRENT->map[(int) a->y][(int) a->x] = None;
}


/**
 * Collision detection, powerup and player movement function executed once per tick
 */
void processTick(unsigned long int *TICK) {
    pthread_mutex_lock(&clientArrLock);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clientInfo_t *player = clientArr[i];
        if (player && player->active && player->playerState != DEAD) {

            // Check if player has any powerups and if there are decrease their tick
            if (player->playerState != NORMAL) {
                player->powerupTick--; //Decrease tick
                if (player->powerupTick == 0)
                    player->playerState = NORMAL; //If the tick is at 0 make sure that playerState is NORMAL
            }

            // Check if player has a collision with something


            /*
             * Ghost -> Pacman
             * Pacman DEAD if he does not have invincibility or powerpellet
             */
            if (player->playerType == Ghost) {
                for (int j = 0; j < MAX_PLAYERS; j++) {
                    if (clientArr[j] && clientArr[j]->active &&
                        clientArr[j]->playerType == Pacman) { //Find all Pacmans
                        clientInfo_t *pacman = clientArr[j];
                        if (pacman->playerState == NORMAL) { //Make sure that Pacman doesn't have any powerups
                            if (sameTile(pacman,
                                         player)) { //If both of them are on the same tile kill pacman and increase Ghost score
                                pacman->playerState = DEAD;
                                player->score += SCORE_GHOST_KILL;
                            }
                        }
                    }
                }
            }


            /* Pacman- -> mapObject
             * Check what kind of mapObject it is
             * If it is powerPellet override invincibility and adjust ticks
             * If it is invincibility adjust ticks
             * If it is SCORE increase players score
             * If it is DOT increase players score
             * Remove mapObject from tile -> (None)
             */
            if (player->playerType == Pacman) {
                if (whichMapObject(player) == PowerPellet) {
                    if (debugLevel >= DEBUG)
                        printf("DEBUG:\t%s ate powerPellet at (%d:%d)\n", player->name, (int) player->x,
                               (int) player->y);
                    player->playerState = powerupPowerPellet;
                    player->powerupTick = POWERUP_PowerPellet_TICKS;
                    resetMapObject(player);

                } else if (whichMapObject(player) == Invincibility) {
                    if (debugLevel >= DEBUG)
                        printf("DEBUG:\t%s ate Invincibility at (%d:%d)\n", player->name, (int) player->x,
                               (int) player->y);
                    player->playerState = powerupInvincibility;
                    player->powerupTick = POWERUP_Invincibility_TICKS;
                    resetMapObject(player);
                } else if (whichMapObject(player) == SCORE) {
                    if (debugLevel >= DEBUG)
                        printf("DEBUG:\t%s ate SCORE at (%d:%d)\n", player->name, (int) player->x, (int) player->y);
                    player->score += SCORE_POINTS;
                    resetMapObject(player);
                } else if (whichMapObject(player) == Dot) {
                    if (debugLevel >= DEBUG)
                        printf("DEBUG:\t%s ate Dot at (%d:%d)\n", player->name, (int) player->x, (int) player->y);
                    player->score += DOT_POINTS;
                    resetMapObject(player);
                }

            }


            /* Ghost -> mapObject (do we even need this?)
             *  ??? maybe he can eat powerPellet for more speed
             */


            /*
             * Pacman -> Ghost
             * If Pacman has PowerPellet and they are on the same tile GHOST=DEAD
             */
            if (player->playerType == Pacman) {
                if (player->playerState == powerupPowerPellet) {
                    for (int j = 0; j < MAX_PLAYERS; j++) {
                        if (clientArr[j] && clientArr[j]->active && clientArr[j]->playerType == Ghost) {
                            clientInfo_t *ghost = clientArr[j];
                            if (sameTile(player, ghost)) {
                                ghost->playerState = DEAD;
                                player->score += SCORE_PACMAN_KILL;
                            }
                        }
                    }
                }
            }


            /* Both -> Wall
             * Move the player by TICK_MOVEMENT
             * If the player is standing on a wall move him back
             */
            if (player->clientMovement == UP) {
                player->y -= TICK_MOVEMENT;
                if (whichMapObject(player) == Wall) {
                    player->y += TICK_MOVEMENT;
                }
            } else if (player->clientMovement == DOWN) {
                player->y += TICK_MOVEMENT;
                if (whichMapObject(player) == Wall) {
                    player->y -= TICK_MOVEMENT;
                }
            } else if (player->clientMovement == LEFT) {
                player->x -= TICK_MOVEMENT;
                if (whichMapObject(player) == Wall) {
                    player->x += TICK_MOVEMENT;
                }
            } else if (player->clientMovement == RIGHT) {
                player->x += TICK_MOVEMENT;
                if (whichMapObject(player) == Wall) {
                    player->x -= TICK_MOVEMENT;
                }
            }

        }
    }
    pthread_mutex_unlock(&clientArrLock);

    //Spawn a powerup in almost random position
    srand((unsigned int) time(0)); //Seed PRNG
    int x, y;
    enum mapObjecT_t mapObject;
    if ((*TICK % POWERUP_Invincibility_SPAWN_TICKS) == 0) {
        do {
            x = rand() % MAP_CURRENT->width;
            y = rand() % MAP_CURRENT->height;
            mapObject = (enum mapObjecT_t) MAP_CURRENT->map[y][x];
        } while (mapObject != Score && mapObject != None && mapObject != Dot);
        MAP_CURRENT->map[y][x] = Invincibility;
        if (debugLevel >= DEBUG) printf("DEBUG:\tSpawned Invincibility at (%d:%d)\n", x, y);
    }
    if ((*TICK % POWERUP_PowerPellet_SPAWN_TICKS) == 0) {
        do {
            x = rand() % MAP_CURRENT->width;
            y = rand() % MAP_CURRENT->height;
            mapObject = (enum mapObjecT_t) MAP_CURRENT->map[y][x];
        } while (mapObject != Score && mapObject != None && mapObject != Dot);
        MAP_CURRENT->map[y][x] = powerupPowerPellet;
        if (debugLevel >= DEBUG) printf("DEBUG:\tSpawned powerPellet at (%d:%d)\n", x, y);
    }

}

