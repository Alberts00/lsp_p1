/*
 * LSP Kursa projekts
 * Alberts Saulitis
 * Viesturs Ružāns
 *
 * 23.12.2016
 */

/**
 * INCLUDES
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ncurses.h>
#include <math.h>


/**
 * DEFINITIONS
 */
#define WORLD_WIDTH 101         // Game map maximum sizes
#define WORLD_HEIGHT 101
#define CONNECTION_WIDTH 40     // Connection window maximum sizes
#define CONNECTION_HEIGHT 20
#define ERROR_WIDTH 40          // Error window maximum sizes
#define ERROR_HEIGHT 10
#define MAX_PACKET_SIZE 15000   // Maximum packet size that we can expect from the server
#define NOTIFICATION_HEIGHT 30  // Notification/chat window maximum sizes
#define NOTIFICATION_WIDTH 40
#define NOTIFICATION_OFFSET 3   // Notification/chat window offset from the side
#define SCORE_HEIGHT 20         // Scoreboard window maximum sizes
#define SCORE_WIDTH 40
#define GREEN_PAIR 1            // Color pair numeric definitions
#define RED_PAIR 2
#define YELLOW_PAIR 3
#define BLUE_PAIR 4
#define WHITE_PAIR 5

/**
 * GLOBAL VARIABLES
 */
int sock;                       // Global socket
WINDOW *mainWindow;             // Main game map window
WINDOW *scoreBoardWindow;       // Scoreboard window on the right
WINDOW *notificationWindow;     // Notification/chat window on the left
WINDOW *connectionWindow;       // Connection parameter (ip, port, name) window
WINDOW *errorWindow;            // Error message window
WINDOW *sendMessageWindow;      // Message writing window
int mapW;                       // Game map width
int mapH;                       // Game map height
int notificationCounter;        // Count how many lines of notifications/chat have been written
int myId;                       // Current client ID
char playerList[256][21];       // List of all players that have JOINED packet sent about them
char myName[21] = {0};          // Current client name

/**
 * ENUMS
 */
// Packet type enumerations
enum packet_t {
    JOIN, ACK, START, END, MAP, PLAYERS, SCORES, MOVE, MESSAGE, QUIT, JOINED, PLAYER_DISCONNECTED
};

// Connection error type enumerations
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
void exitWithMessage(char[]);
void drawPlayers(char*);
void drawScoreTable(char*);
void handleMessage(char*);
void playerJoinedEvent(char*);
void playerDisconnectedEvent(char*);
void createNotificationWindow();
void createScoreBoardWindow();
void sendChatMessage();
void endGame();
void exitGame();


/* ======================================================================================
 *                                        CLIENT LOGIC
 * ====================================================================================== */

/**
 * Entry point. Handle the main game flow, transitions and thread creation
 *
 * @return
 */
int main() {
    // Initialize variables
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

    // Initialize ncurses and create the main game window
    initCurses();

    // Initialize connection dialog and handle the input
    connectionDialog(serverAddress, serverPort);

    // Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1) {
        exitWithMessage("Could not create socket");
    }

    // Set up connection parameters
    server.sin_addr.s_addr = inet_addr(serverAddress);
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(serverPort));

    writeToWindow(connectionWindow, 7, 4, "Connecting to the server...");

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0) {
        exitWithMessage("Connection failed");
    }

    // Send the join request packet
    sendJoinRequest();

    // Receive confirmation/denial of joining
    receiveJoinResponse();

    // Continuous waiting for the START packet
    waitForStartPacket(&startX, &startY);

    // Create lefthand notification/chat window
    createNotificationWindow();

    // Create righthand scoreboard window
    createScoreBoardWindow();

    // Start a new thread to listen to the server
    pthread_t serverThreadId;
    if (pthread_create(&serverThreadId, NULL, listenToServer, (void *) &sock) < 0) {
        exitWithMessage("Error: Could not create a thread");
    }

    // Start a new thread to listen to the user input
    pthread_t inputThreadId;
    if (pthread_create(&inputThreadId, NULL, listenToInput, (void *) &sock) < 0) {
        exitWithMessage("Error: Could not create a thread");
    }

    // Join the threads so that our main thread waits for them to be over
    pthread_join(serverThreadId, NULL);
    pthread_join(inputThreadId, NULL);

    close(sock);

    return 0;
}

/**
 * Exit the game with the provided message. It will appear in a popup-like window.
 *
 * @param msg
 */
void exitWithMessage(char msg[]) {
    deleteAllWindows();
    refresh();
    noecho();
    curs_set(FALSE);
    errorWindow = newwin(ERROR_HEIGHT, ERROR_WIDTH, (LINES - ERROR_HEIGHT) / 2, (COLS - ERROR_WIDTH) / 2);
    box(errorWindow, 0, 0);

    // Write the message
    mvwprintw(errorWindow, ERROR_HEIGHT / 2 - 1, 3, msg);
    mvwprintw(errorWindow, (ERROR_HEIGHT / 2) + 1, 3, "Exiting in 5 seconds...");
    wrefresh(errorWindow);

    refresh();

    // Wait for 5 seconds and exit
    sleep(5);
    windowDeleteAction(errorWindow);
    windowDeleteAction(mainWindow);
    endwin();
    exit(1);
}

/**
 * Deletes all windows except for the main one
 */
void deleteAllWindows() {
    windowDeleteAction(scoreBoardWindow);
    windowDeleteAction(notificationWindow);
    windowDeleteAction(connectionWindow);
}

/**
 * Gracefully delete the window - remove its borders, erase the content and remove it from ncurses
 *
 * @param w
 */
void windowDeleteAction(WINDOW *w) {
    wborder(w, ' ', ' ', ' ',' ',' ',' ',' ',' ');
    werase(w);
    wrefresh(w);
    delwin(w);
}

/**
 * Opens up a chat input window and sends the message to the server
 */
void sendChatMessage() {
    char msg[MAX_PACKET_SIZE] = {0};

    // Create and prepare the chat input window
    sendMessageWindow = newwin(5, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT + 3, NOTIFICATION_OFFSET);
    box(sendMessageWindow, 0, 0);
    scrollok(sendMessageWindow, TRUE);
    writeToWindow(sendMessageWindow, 0, 0, "Write your message ");
    echo();
    curs_set(TRUE);
    cbreak();
    refresh();

    // Move the cursor in the window and listen for the message
    wmove(sendMessageWindow, 1, 1);
    wgetstr(sendMessageWindow, msg);

    int l = strlen(msg);

    // Prepare the message packet
    char packet[1 + sizeof(int) + sizeof(int) + l];
    memset(packet, '\0', sizeof(packet));
    memset(packet, MESSAGE, 1);
    memcpy(packet+1, &myId, sizeof(int));
    memcpy(packet+1+sizeof(int), &l, sizeof(int));
    strcpy(packet+1+sizeof(int)+sizeof(int), msg);

    // Send the message
    if(send(sock, packet, sizeof(packet) , 0) < 0) {
        exitWithMessage("Join request has failed. Please check your internet connection and try again.");
    }

    // Restore previous terminal state and delete chat input window
    cbreak();
    noecho();
    curs_set(FALSE);
    windowDeleteAction(sendMessageWindow);
}

/**
 * Writes message to the provided window with the provided coordinates
 *
 * @param window
 * @param y
 * @param x
 * @param text
 */
void writeToWindow(WINDOW* window, int y, int x, char text[]) {
    // Check if we are going out of bounds in the notifications and reset the window
    if(notificationCounter > NOTIFICATION_HEIGHT - 1) {
        werase(notificationWindow);
        notificationCounter = 0;
        box(window, 0, 0);
        writeToWindow(notificationWindow, 0, 0, "Messages ");
    }

    // Print the actual text
    mvwprintw(window, y, x, text);
    wrefresh(window);
}

/**
 * Shows the connection dialog and displays initial connection info input
 * @param address
 * @param port
 */
void connectionDialog(char *address, char *port) {
    int offsetX, offsetY;

    // Center the window
    offsetX = (COLS - CONNECTION_WIDTH) / 2;
    offsetY = (LINES - CONNECTION_HEIGHT) / 2;

    // Prepare the connection dialog
    echo();
    curs_set(TRUE);

    connectionWindow = newwin(CONNECTION_HEIGHT, CONNECTION_WIDTH, offsetY, offsetX);

    box(connectionWindow, 0, 0);

    // Get the input from the user - server IP and port

    writeToWindow(connectionWindow, 0, 0, "Connect to a server ");
    writeToWindow(connectionWindow, 2, 4, "Enter IP address to connect to: ");

    wmove(connectionWindow, 3, 6);
    wgetstr(connectionWindow, *&address);

    writeToWindow(connectionWindow, 5, 4, "Enter server port: ");
    wmove(connectionWindow, 6, 6);
    wgetstr(connectionWindow, *&port);
}

/**
 * Send join request packet to the server
 */
void sendJoinRequest() {
    writeToWindow(connectionWindow, 9, 4, "Sending request to join the game");
    char packet[21];

    memset(packet, '\0', sizeof(packet));
    memset(myName, '\0', sizeof(myName));

    // Get the name from the user
    writeToWindow(connectionWindow, 11, 4, "Enter your name: ");
    wmove(connectionWindow, 12, 6);
    wgetstr(connectionWindow, myName);

    // Prepare the request packet
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

    // Check the response that we received to JOIN packet
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

        // Save client's character ID and name
        myId = responseCode;
        strcpy(playerList[myId], myName);

        // Delete the connection popup window
        windowDeleteAction(connectionWindow);
        refresh();
    }
}

/**
 * Continuously for the START packet
 *
 * @param startX
 * @param startY
 */
void waitForStartPacket(int *startX, int *startY) {
    noecho();
    curs_set(FALSE);
    writeToWindow(mainWindow, 2, 3, "Connected and waiting for the game to start...");

    ssize_t readSize;
    char packet[MAX_PACKET_SIZE];

    while ((readSize = recv(sock, packet, MAX_PACKET_SIZE, 0)) > 0) {
        // If we are receiving packets from the server, add the loading dots
        waddch(mainWindow, '.');
        wrefresh(mainWindow);

        if(packet[0] == START) {
            // Start packet received, save the needed information and break the loop
            mapW = (int)packet[1];
            mapH = (int)packet[2];
            *startX = (int)packet[3];
            *startY = (int)packet[4];
            break;
        } else if (packet[0] == JOINED) {
            // We are in the "lobby" but received a JOINED packet. Save the joined player
            playerJoinedEvent(packet);
        }

        // Clear the message buffer
        memset(packet, 0, MAX_PACKET_SIZE);
    }

    // Exceptions
    if (readSize == 0) {
        exitWithMessage("Server went offline");
    } else if (readSize == -1) {
        exitWithMessage("Failed to communicate with the server!");
    }

    // Reset the main game window
    werase(mainWindow);
    box(mainWindow, 0, 0);
    wrefresh(mainWindow);
    refresh();
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

    // Wait for all incoming packets
    while ((readSize = recv(sock, message, MAX_PACKET_SIZE, 0)) > 0) {
        int packetType = (int)message[0];

        // Decide what to do based on the packet type
        switch (packetType) {
            case JOINED:
                playerJoinedEvent(message);
                break;
            case PLAYER_DISCONNECTED:
                playerDisconnectedEvent(message);
                break;
            case END:
                endGame();
                break;
            case MAP:
                drawMap(message);
                break;
            case PLAYERS:
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

        // Clear the message buffer
        memset(message, 0, MAX_PACKET_SIZE);
    }

    // Exceptions
    if (readSize == 0) {
        exitWithMessage("Server went offline");
    } else if (readSize == -1) {
        exitWithMessage("Failed to communicate with the server!");
    }

    return 0;
}

/**
 * Listen to the user input and send commands to the server
 *
 * @param conn
 * @return
 */
void *listenToInput(void *conn) {
    int ch, sendPacket;
    enum clientMovement_t direction;
    int i = 0;

    // Continuously listen to the keypresses
    while ((ch = getch())) {
        if(ch != ERR) {
            sendPacket = 1;
            direction = UP;

            // Change direction/act on special key presses
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
                    exitGame();
                    break;
                default:
                    sendPacket = 0;
                    break;
            }

            // Do not send the packet if it was a special keypress or anything unrecognized
            if(sendPacket) {
                // Prepare the direction change command packet
                char packet[1 + sizeof(int) + 1];

                memset(packet, '\0', sizeof(packet));
                memset(packet, MOVE, 1);
                memcpy(packet+1, &ch, sizeof(int));
                memset(packet+1+sizeof(int), direction, 1);

                // Send direction change
                if(send(sock, packet, sizeof(packet) , 0) < 0) {
                    exitWithMessage("Request has failed. Please check your internet connection and try again.");
                }
            }
        }
    }

    return 0;
}

/**
 * Send QUIT packet to the server and exit the game gracefully if the server allows us to
 */
void exitGame() {
    // Prepare QUIT packet
    char packet[1 + sizeof(int)];

    memset(packet, '\0', sizeof(packet));
    memset(packet, QUIT, 1);
    memcpy(packet+1, &myId, sizeof(int));

    // Send the QUIT packet
    if(send(sock, packet, sizeof(packet) , 0) < 0) {
        exitWithMessage("Join request has failed. Please check your internet connection and try again.");
    }

    // Exit the game
    exitWithMessage("Goodbye!");
}

/**
 * Simple end game event handling
 */
void endGame() {
    exitWithMessage("Game ended! Thank you for playing.");
}

/**
 * Initialize ncurses
 */
void initCurses() {
    int offsetX, offsetY;

    // Initialize the window
    initscr();
    refresh();
    // Don't echo any keypresses
    noecho();
    // Don't display a cursor
    curs_set(FALSE);
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

    // Prepare and center the inner window
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
    // Move the map pointer by one to skip packet type
    map++;

    // Main loop for drawing the map
    for (int i = 0; i < mapH; ++i) {
        for (int j = 0; j < mapW; ++j) {
            // +1 to the positions is needed so that graphics do not overlap the world box
            // Get the block type to draw
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
                    mvwaddch(mainWindow, i+1, j+1, 'X');
                    break;
                case Score:
                    mvwaddch(mainWindow, i+1, j+1, ACS_PLUS);
                    break;
                default:
                    break;
            }
        }
    }

    // Refresh the window to display changes
    refresh();
    wrefresh(mainWindow);
}

/**
 * Handle player joined event - write a notification and save the player
 *
 * @param mapH
 * @param mapW
 * @param map
 */
void playerJoinedEvent(char *event) {
    // Move pointer beyond packet type
    event++;

    int playerId;
    // Save player ID and move pointer beyond it
    memcpy((void *) &playerId, (void *) *&event, sizeof(playerId));
    event += sizeof(playerId);

    // Save player name
    char name[21] = {0};
    memcpy(name, *&event, 20);

    // Notify the players of the recently joined one
    char msg[300] = {0};
    sprintf(msg, "Player %s joined the game!", name);

    strcpy(playerList[playerId], name);

    writeToWindow(notificationWindow, ++notificationCounter, 1, msg);
}

/**
 * Handle player disconnect event - write a notification
 *
 * @param mapH
 * @param mapW
 * @param map
 */
void playerDisconnectedEvent(char *event) {
    // Move pointer beyond packet type
    event++;

    int playerId;
    // Save player ID and move pointer beyond it
    memcpy((void *) &playerId, (void *) *&event, sizeof(playerId));
    event += sizeof(playerId);

    // Notify the players of the recently disconnected one
    char msg[300] = {0};
    sprintf(msg, "Player %s left the game!", playerList[playerId]);

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
    // Move pointer beyond packet type
    players++;

    int playerCount;
    // Save the player count to draw, move pointer beyond it
    memcpy((void *) &playerCount, (void *) *&players, sizeof(playerCount));
    players += sizeof(playerCount);

    int playerId;
    float playerX, playerY;
    enum playerState_t playerState;
    enum playerType_t playerType;

    // Get information about each player and draw the character
    for (int i = 0; i < playerCount; ++i) {
        // Save player ID, coordinates, state and type
        // Move pointer after each copy by the read byte count
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

        // Determine which color pair to use
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

        // Draw character if it is alive and set its color
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

    // Refresh the window to render the changes
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
    // Move pointer beyond packet type
    scores++;

    int scoreCount;
    // Save the amount of scores to display
    memcpy((void *) &scoreCount, (void *) *&scores, sizeof(scoreCount));
    scores += sizeof(scoreCount);

    int playerId, playerScore;

    // Get information about each player and draw the score
    for (int i = 0; i < scoreCount; ++i) {
        // Save player score and ID
        memcpy((void *) &playerScore, (void *) *&scores, sizeof(playerScore));
        scores += sizeof(playerScore);
        memcpy((void *) &playerId, (void *) *&scores, sizeof(playerId));
        scores += sizeof(playerId);

        // Prepare the formatted strings for output
        char printId[MAX_PACKET_SIZE] = {0};
        char printScore[MAX_PACKET_SIZE] = {0};
        // Get the player name from our saved list
        sprintf(printId, "%s", playerList[playerId]);
        sprintf(printScore, "%d", playerScore);

        // If it is the client's score, draw it in green
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
 */
void handleMessage(char *message) {
    // Move pointer beyond packet type
    message++;

    int senderId, messageLength;

    // Save the sender ID, message length and the message itself
    memcpy((void *) &senderId, (void *) *&message, sizeof(senderId));
    message += sizeof(senderId);
    memcpy((void *) &messageLength, (void *) *&message, sizeof(messageLength));
    message += sizeof(messageLength);

    char msg[messageLength];
    memcpy((void *) &msg, (void *) *&message, messageLength);

    // If the sender is the current client, draw the message in green
    if(senderId == myId) {
        wattron(notificationWindow, COLOR_PAIR(GREEN_PAIR));
    }

    // Format the message string to output
    char formattedMsg[MAX_PACKET_SIZE] = {0};
    sprintf(formattedMsg, "%s: %s", playerList[senderId], msg);

    int lineCount = (int)ceil(strlen(formattedMsg) / NOTIFICATION_WIDTH) + 1;
    notificationCounter += lineCount;
    writeToWindow(notificationWindow, notificationCounter, 1, formattedMsg);
    notificationCounter += lineCount;

    if(senderId == myId) {
        wattroff(notificationWindow, COLOR_PAIR(GREEN_PAIR));
    }

    refresh();
}
