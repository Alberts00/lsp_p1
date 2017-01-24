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
#include <ncurses.h>
#include <math.h>


/**
 * Definitions
 */
#define WORLD_WIDTH 101
#define WORLD_HEIGHT 101
#define CONNECTION_WIDTH 40
#define CONNECTION_HEIGHT 20
#define ERROR_WIDTH 40
#define ERROR_HEIGHT 10
#define MAX_PACKET_SIZE 15000
#define NOTIFICATION_HEIGHT 30
#define NOTIFICATION_WIDTH 40
#define NOTIFICATION_OFFSET 3
#define SCORE_HEIGHT 20
#define SCORE_WIDTH 40
#define GREEN_PAIR 1
#define RED_PAIR 2
#define YELLOW_PAIR 3
#define BLUE_PAIR 4
#define WHITE_PAIR 5

/**
 * GLOBAL VARIABLES
 */
int sock;
WINDOW *mainWindow;
WINDOW *scoreBoardWindow;
WINDOW *notificationWindow;
WINDOW *connectionWindow;
WINDOW *errorWindow;
WINDOW *sendMessageWindow;
int mapW;
int mapH;
int notificationCounter;
int myId;
char oldPlayerLocations[MAX_PACKET_SIZE];
char playerList[256][21];
char myName[21] = {0};

/**
 * ENUMS
 */
// Packet type enumerations
enum packet_t {
    JOIN, ACK, START, END, MAP, PLAYERS, SCORES, MOVE, MESSAGE, QUIT, JOINED, PLAYER_DISCONNECTED
};

enum connectionError_t {
    ERROR_NAME_IN_USE = -1, ERROR_SERVER_FULL = -2, ERROR_OTHER = -3
};

//Map object enumerations
enum mapObjecT_t {
    None, Dot, Wall, PowerPellet, Invincibility, Score
};

// Player state enumerations
enum playerState_t {
    PS_NORMAL, PS_DEAD, PS_PowerPellet, PS_Invincibility, PS_Other
};

// Player type enumerations
enum playerType_t {
    Pacman, Ghost
};

// Client movement enumerations
enum clientMovement_t {
    UP, DOWN, RIGHT, LEFT
};

/**
 * METHOD DECLARATIONS
 */
void *listenToServer(void*);
void *listenToInput(void*);
void sendJoinRequest();
void receiveJoinResponse();
void sendTextNotification(char[]);
void initCurses();
void deleteAllWindows();
void connectionDialog(char*, char*);
void writeToWindow(WINDOW*, int, int, char[]);
void windowDeleteAction(WINDOW*);
void waitForStartPacket(int*, int*);
void drawMap(char*);
void drawPlayers(char*);
void drawScoreTable(char*);
void handleMessage(char*);
void playerJoinedEvent(char*);
void playerDisconnectedEvent(char*);
void createNotificationWindow();
void createScoreBoardWindow();
void epicDebug(char*);
void drawPlayersLoop();
void sendChatMessage();

void *safe_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { //Ja alokācija neizdevās, izdrukājam iemeslu, kurš iegūts no errno
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}

void exitWithMessage(char error[]) {
    deleteAllWindows();
    refresh();
    noecho();
    curs_set(FALSE);
    errorWindow = newwin(ERROR_HEIGHT, ERROR_WIDTH, (LINES - ERROR_HEIGHT) / 2, (COLS - ERROR_WIDTH) / 2);
    box(errorWindow, 0, 0);
    mvwprintw(errorWindow, ERROR_HEIGHT / 2 - 1, 3, error);
    mvwprintw(errorWindow, (ERROR_HEIGHT / 2) + 1, 3, "Exiting in 5 seconds...");
    wrefresh(errorWindow);
    refresh();
    sleep(5);
    windowDeleteAction(errorWindow);
    windowDeleteAction(mainWindow);
    endwin();
    exit(EXIT_FAILURE);
}

void deleteAllWindows() {
    windowDeleteAction(scoreBoardWindow);
    windowDeleteAction(notificationWindow);
    windowDeleteAction(connectionWindow);
}

void windowDeleteAction(WINDOW *w) {
    wborder(w, ' ', ' ', ' ',' ',' ',' ',' ',' ');
    werase(w);
    wrefresh(w);
    delwin(w);
}

void sendChatMessage() {
    char msg[MAX_PACKET_SIZE] = {0};

    sendMessageWindow = newwin(5, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT + 3, NOTIFICATION_OFFSET);
    box(sendMessageWindow, 0, 0);
    scrollok(sendMessageWindow, TRUE);
    writeToWindow(sendMessageWindow, 0, 0, "Write your message ");


    echo();
    curs_set(TRUE);
    cbreak();

    refresh();

    wmove(sendMessageWindow, 1, 1);
    wgetstr(sendMessageWindow, msg);
//    scanw(msg);

    int l = strlen(msg);

    char packet[1 + sizeof(int) + sizeof(int) + l];

    memset(packet, '\0', sizeof(packet));
    memset(packet, MESSAGE, 1);
    memcpy(packet+1, &myId, sizeof(int));
    memcpy(packet+1+sizeof(int), &l, sizeof(int));
    strcpy(packet+1+sizeof(int)+sizeof(int), msg);

    if(send(sock, packet, sizeof(packet) , 0) < 0) {
        exitWithMessage("Join request has failed. Please check your internet connection and try again.");
    }

    writeToWindow(mainWindow, 28, 1, msg);

    cbreak();
    noecho();
    curs_set(FALSE);
    windowDeleteAction(sendMessageWindow);
}

/**
 * Sends notification to the user
 */
void sendTextNotification(char text[]) {
    // @TODO: implement some nice notifications
    puts(text);
}

void writeToWindow(WINDOW* window, int y, int x, char text[]) {
    if(notificationCounter > NOTIFICATION_HEIGHT - 1) {
        werase(notificationWindow);
        notificationCounter = 0;
        box(window, 0, 0);
        writeToWindow(notificationWindow, 0, 0, "Messages ");
    }

    mvwprintw(window, y, x, text);
    wrefresh(window);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    char serverAddress[16], serverPort[6];
    int startX, startY;
    mapW = 0;
    mapH = 0;
    notificationCounter = 0;
    myId = 0;

    for (int i = 0; i < 256; ++i) {
        for (int j = 0; j < 21; ++j) {
            playerList[i][j] = '\0';
        }
    }

    // Reset player locations variable
    memset(oldPlayerLocations, '\0', MAX_PACKET_SIZE);

    initCurses();

    connectionDialog(serverAddress, serverPort);

    //Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1) {
        exitWithMessage("Could not create socket");
    }

    server.sin_addr.s_addr = inet_addr(serverAddress);
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(serverPort));

    writeToWindow(connectionWindow, 7, 4, "Connecting to the server...");

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0) {
        exitWithMessage("Connection failed");
    }

    sendJoinRequest();
    receiveJoinResponse();
    waitForStartPacket(&startX, &startY);

    createNotificationWindow();
    createScoreBoardWindow();
    // Start packet received, we can start listening to the server and sending our data

    // Start a new thread to listen to the server
    pthread_t threadId;
    if (pthread_create(&threadId, NULL, listenToServer, (void *) &sock) < 0) {
        exitWithMessage("Error: Could not create a thread");
    }

    // Start a new thread to draw players
    pthread_t threadId2;
    if (pthread_create(&threadId2, NULL, listenToInput, (void *) &sock) < 0) {
        exitWithMessage("Error: Could not create a thread");
    }

    // Send commands to the server

    drawPlayersLoop();

    pthread_join(threadId, NULL);
    pthread_join(threadId2, NULL);

    close(sock);

    return 0;
}

/**
 * Shows the connection dialog and displays initial connection info input
 * @param adrress
 * @param port
 */
void connectionDialog(char *address, char *port) {
    int offsetX, offsetY;

    // Center the window
    offsetX = (COLS - CONNECTION_WIDTH) / 2;
    offsetY = (LINES - CONNECTION_HEIGHT) / 2;

    echo();
    curs_set(TRUE);
//    cbreak();

    connectionWindow = newwin(CONNECTION_HEIGHT, CONNECTION_WIDTH, offsetY, offsetX);

    box(connectionWindow, 0, 0);


    // @TODO: this is fucking shit but I have no idea how to make it work with mvwscanw


    writeToWindow(connectionWindow, 0, 0, "Connect to a server ");
    writeToWindow(connectionWindow, 2, 4, "Enter IP address to connect to: ");
//    wmove(connectionWindow, 3, 6);
//    scanf("%s", address);


//    refresh();
    mvwscanw(connectionWindow, 4, 6, *&address);

    writeToWindow(connectionWindow, 5, 4, "Enter server port: ");
    refresh();
//    mvwscanw(connectionWindow, 6, 6, port);
    wmove(connectionWindow, 6, 6);
//    scanf("%s", port);

//    strcpy(address, "127.0.0.1");
//    strcpy(address, "95.68.71.51"); // Alberts
//    strcpy(address, "149.202.149.77"); // Alberts
//    strcpy(address, "81.198.119.38"); // Arnolds
    strcpy(address, "192.168.120.38"); // VM
//    strcpy(address, "192.168.120.182"); // SW
//    strcpy(address, "192.168.120.93"); // SW
    strcpy(port, "8888");
//    strcpy(port, "2017"); // Arnolds
//    strcpy(port, "3000"); // Arnolds
//    nocbreak();

    writeToWindow(mainWindow, 5, 4, address);
    writeToWindow(mainWindow, 6, 4, port);

}

/**
 * Send join request to the server
 */
void sendJoinRequest() {
    writeToWindow(connectionWindow, 9, 4, "Sending request to join the game");
    char packet[21];

    memset(packet, '\0', sizeof(packet));
    memset(myName, '\0', sizeof(myName));

    writeToWindow(connectionWindow, 11, 4, "Enter your name: ");
    wmove(connectionWindow, 12, 6);
    scanf("%s", myName);
    // @TODO: this is fucking shit but I have no idea how to make it work with mvwscanw
//    mvwscanw(connectionWindow, 12, 6, name);

    memset(packet, JOIN, 1);
    strcpy(packet+1, myName);

    // Send join request packet
    if(send(sock, packet, sizeof(packet) , 0) < 0) {
        exitWithMessage("Join request has failed. Please check your internet connection and try again.");
    }
}

/**
 * Receive join response from the server
 */
void receiveJoinResponse() {
    char response[sizeof(int) + 1] = {0};
    int responseCode;

    if(recv(sock, response, sizeof(int) + 1, 0)) {
        if(response[0] != ACK) {
            exitWithMessage("Server rejected the connection. Please try again.");
        }
        memcpy((void *) &responseCode, (void *) &response+1, sizeof(responseCode));

        if(responseCode == ERROR_NAME_IN_USE) {
            exitWithMessage("Name is in use! Disconnecting...");
        }

        if(responseCode == ERROR_SERVER_FULL) {
            exitWithMessage("Server is full! Please try again later.");
        }

        if(responseCode == ERROR_OTHER) {
            exitWithMessage("Something went wrong! Please try again later.");
        }

        // Save client's character ID
        myId = responseCode;
        strcpy(playerList[myId], myName);

        windowDeleteAction(connectionWindow);
        refresh();
    }
}

void waitForStartPacket(int *startX, int *startY) {
    noecho();
    curs_set(FALSE);
    writeToWindow(mainWindow, 2, 3, "Connected and waiting for the game to start...");

    ssize_t readSize;
    char startPacket[MAX_PACKET_SIZE];

    while ((readSize = recv(sock, startPacket, MAX_PACKET_SIZE, 0)) > 0) {
        waddch(mainWindow, '.');
        wrefresh(mainWindow);

        if(startPacket[0] == START) {
            mapW = (int)startPacket[1];
            mapH = (int)startPacket[2];
            // @TODO: FIX
//            *startX = (int)startPacket[3];
//            *startY = (int)startPacket[4];

            break;
        }

        //clear the message buffer
        memset(startPacket, 0, MAX_PACKET_SIZE);
    }

    werase(mainWindow);
    box(mainWindow, 0, 0);
    wrefresh(mainWindow);
    refresh();
}

void drawPlayersLoop() {
    int i = 0;
    while (1) {
        if((int)oldPlayerLocations[0] == PLAYERS) {
            i++;
//            writeToWindow(mainWindow, 20+i, 3, "players");
//            drawPlayers(oldPlayerLocations);
        }
    }
}

/**
 * Creates the window for notifications (chat/server messages)
 */
void createNotificationWindow() {
    notificationWindow = newwin(NOTIFICATION_HEIGHT, NOTIFICATION_WIDTH, 3, NOTIFICATION_OFFSET);
    box(notificationWindow, 0, 0);
    scrollok(notificationWindow, TRUE);
    writeToWindow(notificationWindow, 0, 0, "Messages ");
    refresh();
}

/**
 * Creates the window for scoreboard
 */
void createScoreBoardWindow() {
    int xOffset = NOTIFICATION_WIDTH + NOTIFICATION_OFFSET + WORLD_WIDTH + 3;
    scoreBoardWindow = newwin(SCORE_HEIGHT, SCORE_WIDTH, 3, xOffset);
    box(scoreBoardWindow, 0, 0);
    scrollok(scoreBoardWindow, TRUE);
    writeToWindow(scoreBoardWindow, 0, 0, "Scoreboard ");
    refresh();
}


/**
 * Continuously listen to the game server and act on packets
 *
 * @param conn
 * @return
 */
void *listenToServer(void *conn) {
    int sock = *(int *) conn;
    ssize_t readSize;
    char message[MAX_PACKET_SIZE];
//    writeToWindow(notificationWindow, ++notificationCounter, 1, "Started listening to the server");

    while ((readSize = recv(sock, message, MAX_PACKET_SIZE, 0)) > 0) {
        int packetType = (int)message[0];

        switch (packetType) {
            case JOINED:
                playerJoinedEvent(message); // @TODO Implement
                break;
            case PLAYER_DISCONNECTED:
                playerDisconnectedEvent(message); // @TODO Implement
                break;
            case END:
//                endGame(); @TODO Implement
                break;
            case MAP:
                drawMap(message);
                break;
            case PLAYERS:
//                memset(oldPlayerLocations, '\0', MAX_PACKET_SIZE);
                memcpy(oldPlayerLocations, &message, readSize);
                drawPlayers(message);
                break;
            case SCORES:
                drawScoreTable(message);
                break;
            case MESSAGE:
                handleMessage(message);
                break;
            default:
                break;
        }

        //clear the message buffer
        memset(message, 0, MAX_PACKET_SIZE);
    }

    if (readSize == 0) {
        exitWithMessage("Server went offline");
    } else if (readSize == -1) {
        exitWithMessage("Failed to communicate with the server!");
    }

    return 0;
}

void *listenToInput(void *conn) {
    int ch, sendPacket;
    enum clientMovement_t direction;
    int i = 0;

    while ((ch = getch())) {
        i++;
        char n[300] = {0};
        sprintf(n, "%d bljatj", i);
        writeToWindow(mainWindow, 25, 3, n);
        if(ch != ERR) {
            sendPacket = 1;
            direction = UP;

            switch(ch) {
                case (int)'w':
                    direction = UP;
                    break;
                case (int)'s':
                    direction = DOWN;
                    break;
                case (int)'d':
                    direction = RIGHT;
                    break;
                case (int)'a':
                    direction = LEFT;
                    break;
                case (int)'y':
                    sendPacket = 0;
                    sendChatMessage();
                    break;
                case (int)'q':
                    sendPacket = 0;
//                    exitGame(); // @TODO Implement
                    break;
                default:
                    sendPacket = 0;
                    break;
            }

            if(sendPacket) {
                // Send direction change command
                char packet[1 + sizeof(int) + 1];

                // Prepare the packet
                memset(packet, '\0', sizeof(packet));
                memset(packet, MOVE, 1);
                memcpy(packet+1, &ch, sizeof(int));
                memset(packet+1+sizeof(int), direction, 1);

                if(send(sock, packet, sizeof(packet) , 0) < 0) {
                    exitWithMessage("Request has failed. Please check your internet connection and try again.");
                }
            }
        }
    }

    return 0;
}

/**
 * Initialize curses
 */
void initCurses() {
    int offsetX, offsetY;

    initscr(); // Initialize the window
    refresh();
    noecho(); // Don't echo any keypresses
    curs_set(FALSE); // Don't display a cursor
    cbreak();
    timeout(100);

    start_color();

    // Initialize color levels and color pairs
    init_color(COLOR_GREEN, 0, 700, 0);
    init_color(COLOR_RED, 700, 0, 0);
    init_color(COLOR_YELLOW, 500, 500, 0);

    init_pair(GREEN_PAIR, COLOR_GREEN, COLOR_BLACK);
    init_pair(RED_PAIR, COLOR_RED, COLOR_BLACK);
    init_pair(YELLOW_PAIR, COLOR_YELLOW, COLOR_BLACK);
    init_pair(BLUE_PAIR, COLOR_BLUE, COLOR_BLACK);
    init_pair(WHITE_PAIR, COLOR_WHITE, COLOR_BLACK);

    // Center the inner window
    offsetX = (COLS - WORLD_WIDTH) / 2;
    offsetX = offsetX + (NOTIFICATION_WIDTH - offsetX) + NOTIFICATION_OFFSET;

    mainWindow = newwin(WORLD_HEIGHT, WORLD_WIDTH, 0, offsetX);

    scrollok(mainWindow, TRUE);

    box(mainWindow, 0, 0);

    wrefresh(mainWindow);
}

/**
 * Main method for static map object drawing
 * note: map[i][j] == *((map+j)+i*mapW)
 *
 * @param mapH
 * @param mapW
 * @param map
 */
void drawMap(char *map) {
//    writeToWindow(notificationWindow, ++notificationCounter, 1, "Received map from the server");

    char newmap[MAX_PACKET_SIZE] = {0};
    map++;
    memcpy(newmap, map, mapW * mapH);

    for (int i = 0; i < mapH; ++i) {
        for (int j = 0; j < mapW; ++j) {
            // +1 to the positions is needed so that graphics do not overlap the world box
            int blockType = (int)*((map+j)+i*mapW);

            switch (blockType) {
                case None:
                    mvwaddch(mainWindow, i+1, j+1, ' ');
                    break;
                case Wall:
                    mvwaddch(mainWindow, i+1, j+1, 97 | A_ALTCHARSET);
                    break;
                case Dot:
                    wattron(mainWindow, COLOR_PAIR(WHITE_PAIR)|A_BOLD);
                    mvwaddch(mainWindow, i+1, j+1, '.');
                    wattroff(mainWindow, COLOR_PAIR(WHITE_PAIR)|A_BOLD);
                    break;
                case PowerPellet:
                    mvwaddch(mainWindow, i+1, j+1, ACS_DIAMOND);
                    break;
                case Invincibility:
                    mvwaddch(mainWindow, i+1, j+1, 164 | A_ALTCHARSET);
                    break;
                case Score:
                    mvwaddch(mainWindow, i+1, j+1, ACS_PLUS);
                    break;
                default:
                    break;
            }
        }
    }

    refresh();
    wrefresh(mainWindow);
}

/**
 * @param mapH
 * @param mapW
 * @param map
 */
void playerJoinedEvent(char *event) {
    event++;

    int playerId;
    memcpy((void *) &playerId, (void *) *&event, sizeof(playerId));
    event += sizeof(playerId);

    char name[21] = {0};
    memcpy(name, *&event, 20);

    char msg[300] = {0};
    sprintf(msg, "Player %s joined the game!", name);

    strcpy(playerList[playerId], name);

    writeToWindow(notificationWindow, ++notificationCounter, 1, msg);
}

/**
 * @param mapH
 * @param mapW
 * @param map
 */
void playerDisconnectedEvent(char *event) {
    event++;

    int playerId;
    memcpy((void *) &playerId, (void *) *&event, sizeof(playerId));
    event += sizeof(playerId);

    char msg[300] = {0};
    sprintf(msg, "Player %s left the game!", playerList[playerId]);

//    strcpy(playerList[playerId], '\0');
//    memset(playerList[playerId][0], '\0', 21);

    writeToWindow(notificationWindow, ++notificationCounter, 1, msg);
}


/**
 * Main method for player drawing
 *
 * @param mapH
 * @param mapW
 * @param map
 */
void drawPlayers(char *players) {
//    writeToWindow(notificationWindow, ++notificationCounter, 1, "Received players from the server");

    players++;

    int playerCount;
    memcpy((void *) &playerCount, (void *) *&players, sizeof(playerCount));
    players += sizeof(playerCount);

    int playerId;
    float playerX, playerY;
    enum playerState_t playerState;
    enum playerType_t playerType;

    // Get information about each player and draw the character
    for (int i = 0; i < playerCount; ++i) {
        memcpy((void *) &playerId, (void *) *&players, sizeof(playerId));
        players += sizeof(playerId);
        memcpy((void *) &playerX, (void *) *&players, sizeof(playerX));
        players += sizeof(playerX);
        memcpy((void *) &playerY, (void *) *&players, sizeof(playerY));
        players += sizeof(playerY);
        memcpy((void *) &playerState, (void *) *&players, 1);
        players++;
        memcpy((void *) &playerType, (void *) *&players, 1);
        players++;

        // Convert to integers as sadly we can not represent floats in ncurses
        int integerPosX = (int)playerX;
        int integerPosY = (int)playerY;

        // Default color (pacman)
        int usePair = GREEN_PAIR;

        // Determine which pair to use
        if(playerType == Ghost) {
            usePair = RED_PAIR;
        }

        if(playerId == myId) {
            usePair = YELLOW_PAIR;
        }

        if(playerState == PS_Invincibility) {
            usePair = WHITE_PAIR;
        } else if (playerState == PS_PowerPellet) {
            usePair = BLUE_PAIR;
        }

        // Draw player and set its color
        wattron(mainWindow, COLOR_PAIR(usePair));
        if(playerState != PS_DEAD) {
            if(playerType == Ghost) {
                mvwaddch(mainWindow, integerPosY+1, integerPosX+1, '&');
            } else {
                mvwaddch(mainWindow, integerPosY+1, integerPosX+1, '@');
            }
        }
        wattroff(mainWindow, COLOR_PAIR(usePair));
    }

    refresh();
    wrefresh(mainWindow);
}

/**
 * Main method for scoreboard drawing
 *
 * @param mapH
 * @param mapW
 * @param map
 */
void drawScoreTable(char *scores) {
//    writeToWindow(notificationWindow, ++notificationCounter, 1, "Received scores from the server");

    scores++;

    int scoreCount;
    memcpy((void *) &scoreCount, (void *) *&scores, sizeof(scoreCount));
    scores += sizeof(scoreCount);

    int playerId, playerScore;

    // Get information about each player and draw the scores
    for (int i = 0; i < scoreCount; ++i) {
        memcpy((void *) &playerScore, (void *) *&scores, sizeof(playerScore));
        scores += sizeof(playerScore);
        memcpy((void *) &playerId, (void *) *&scores, sizeof(playerId));
        scores += sizeof(playerId);

        char printId[MAX_PACKET_SIZE] = {0};
        char printScore[MAX_PACKET_SIZE] = {0};
        sprintf(printId, "%d", playerId);
        sprintf(printScore, "%d", playerScore);

        if(playerId == myId) {
            wattron(scoreBoardWindow, COLOR_PAIR(GREEN_PAIR));
        }

        writeToWindow(scoreBoardWindow, i+1, 1, printId);
        writeToWindow(scoreBoardWindow, i+1, 25, printScore);

        if(playerId == myId) {
            wattroff(scoreBoardWindow, COLOR_PAIR(GREEN_PAIR));
        }
    }

    refresh();
}

/**
 * Main method for reading and displaying user/server messages
 *
 * @param mapH
 * @param mapW
 * @param map
 *
 * @TODO finish this after no more debug messages are needed
 */
void handleMessage(char *message) {
//    writeToWindow(notificationWindow, ++notificationCounter, 1, "Received scores from the server");

    message++;

    int senderId, messageLength;

    memcpy((void *) &senderId, (void *) *&message, sizeof(senderId));
    message += sizeof(senderId);
    memcpy((void *) &messageLength, (void *) *&message, sizeof(messageLength));
    message += sizeof(messageLength);

    char msg[messageLength];
    memcpy((void *) &msg, (void *) *&message, messageLength);


    if(senderId == myId) {
        wattron(notificationWindow, COLOR_PAIR(GREEN_PAIR));
    }

    char formattedMsg[MAX_PACKET_SIZE] = {0};

    sprintf(formattedMsg, "%s: %s", playerList[senderId], msg);

    writeToWindow(notificationWindow, ++notificationCounter, 1, formattedMsg);

    if(senderId == myId) {
        wattroff(notificationWindow, COLOR_PAIR(GREEN_PAIR));
    }

    refresh();
}

/**
 * Fuck ncurses
 */
void epicDebug(char *message) {
    FILE *f = fopen("file2.txt", "a");

    if (f == NULL)
    {
        printf("Error opening file!\n");
        exitWithMessage("Fuck you, basically.");
    }

    fprintf(f, "%s\n", message);

    fclose(f);
}