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

/**
 * GLOBAL VARIABLES
 */
int sock;
WINDOW *mainWindow;
WINDOW *scoreWindow;
WINDOW *scoreBoardWindow;
WINDOW *notificationWindow;
WINDOW *connectionWindow;
WINDOW *errorWindow;
int mapW;
int mapH;
int notificationCounter;

/**
 * ENUMS
 */
// Packet type enumerations
enum packet_t {
    JOIN, ACK, START, END, MAP, PLAYERS, SCORE, MOVE, MESSAGE, QUIT, JOINED, PLAYER_DISCONNECTED
};

enum connectionError_t {
    ERROR_NAME_IN_USE = -1, ERROR_SERVER_FULL = -2, ERROR_OTHER = -3
};

//Map object enumerations
enum mapObjecT_t {
    None, Dot, Wall, PowerPellet, Invincibility, Score
};

/**
 * METHOD DECLARATIONS
 */
void *listenToServer(void*);
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
void createNotificationWindow();
void epicDebug(char*);


/**
 * Definitions
 */
#define WORLD_WIDTH 101
#define WORLD_HEIGHT 101
#define CONNECTION_WIDTH 40
#define CONNECTION_HEIGHT 20
#define ERROR_WIDTH 40
#define ERROR_HEIGHT 10
#define MAX_PACKET_SIZE 150000
#define NOTIFICATION_HEIGHT 30
#define NOTIFICATION_WIDTH 40
#define NOTIFICATION_OFFSET 3

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
    windowDeleteAction(scoreWindow);
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

/**
 * Sends notification to the user
 */
void sendTextNotification(char text[]) {
    // @TODO: implement some nice notifications
    puts(text);
}

void writeToWindow(WINDOW* window, int y, int x, char text[]) {
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

    initCurses();

    char map[15][31] =
            {
                    {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
                    {2,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,2},
                    {2,1,2,2,2,1,2,1,2,2,2,1,2,2,2,2,2,2,2,1,2,2,2,1,2,1,2,2,2,1,2},
                    {2,1,2,1,1,1,1,1,2,1,1,1,1,1,1,2,1,1,1,1,1,1,2,1,1,1,1,5,2,1,2},
                    {2,1,1,1,2,2,2,1,2,1,2,2,2,2,1,2,1,2,2,2,2,1,2,1,2,2,2,1,1,1,2},
                    {2,2,2,1,2,1,1,1,2,3,2,1,1,1,1,1,1,1,1,1,2,1,2,1,1,1,2,1,2,2,2},
                    {2,3,1,1,2,1,2,2,2,1,2,1,2,2,2,2,2,2,2,1,2,1,2,2,2,1,2,1,1,1,2},
                    {2,1,2,2,2,1,1,1,1,1,1,1,2,0,0,0,0,0,2,1,1,1,1,1,1,1,2,2,2,1,2},
                    {2,1,1,1,2,1,2,2,2,1,2,1,2,2,2,2,2,2,2,1,2,1,2,2,2,1,2,1,1,1,2},
                    {2,2,2,1,2,4,1,1,2,1,2,1,1,1,1,1,1,1,1,1,2,1,2,1,1,1,2,1,2,2,2},
                    {2,1,1,1,2,2,2,1,2,1,2,2,2,2,1,2,1,2,2,2,2,1,2,1,2,2,2,1,1,1,2},
                    {2,1,2,1,1,1,1,1,2,1,1,1,1,1,1,2,1,1,1,1,1,1,2,1,4,1,1,1,2,1,2},
                    {2,1,2,2,2,1,2,1,2,2,2,1,2,2,2,2,2,2,2,1,2,2,2,4,2,1,2,2,2,1,2},
                    {2,1,1,1,1,1,2,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,2,1,1,1,1,1,2},
                    {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
            };

//    drawMap(15, 31, map[0]);
//    sleep(100);


//    strcpy(serverAddress, "127.0.0.1");
//    strcpy(serverPort, "8888");
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
    // Start packet received, we can start listening to the server and sending our data

    // Start a new thread to listen to the server
    pthread_t threadId;
    if (pthread_create(&threadId, NULL, listenToServer, (void *) &sock) < 0) {
        exitWithMessage("Error: Could not create a thread");
    }
    pthread_join(threadId , NULL);

    // Keep communicating with server
//    while(1) {
//        scanf("%s" , message);
//
//        //Send some data
//        if( send(sock , message , strlen(message) , 0) < 0)
//        {
//            puts("Send failed");
//            return 1;
//        }
//
//        //Receive a reply from the server
//        if( recv(sock , serverReply , 2000 , 0) < 0)
//        {
//            puts("recv failed");
//            break;
//        }
//        puts(serverReply);
//    }

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

    connectionWindow = newwin(CONNECTION_HEIGHT, CONNECTION_WIDTH, offsetY, offsetX);

    box(connectionWindow, 0, 0);


    // @TODO: this is fucking shit but I have no idea how to make it work with mvwscanw


    writeToWindow(connectionWindow, 0, 0, "Connect to a server ");
    writeToWindow(connectionWindow, 2, 4, "Enter IP address to connect to: ");
    wmove(connectionWindow, 3, 6);
//    scanf("%s", address);


    refresh();
//    mvwscanw(connectionWindow, 3, 6, address);
    writeToWindow(connectionWindow, 5, 4, "Enter server port: ");
    refresh();
//    mvwscanw(connectionWindow, 6, 6, port);
    wmove(connectionWindow, 6, 6);
//    scanf("%s", port);

    strcpy(address, "127.0.0.1");
//    strcpy(address, "95.68.71.51");
//    strcpy(address, "81.198.119.38");
    strcpy(port, "8888");
}

/**
 * Send join request to the server
 */
void sendJoinRequest() {
    writeToWindow(connectionWindow, 9, 4, "Sending request to join the game");
    char name[20];
    char packet[21];

    memset(packet, '\0', sizeof(packet));
    memset(name, '\0', sizeof(name));

    writeToWindow(connectionWindow, 11, 4, "Enter your name: ");
    wmove(connectionWindow, 12, 6);
    scanf("%s", name);
    // @TODO: this is fucking shit but I have no idea how to make it work with mvwscanw
//    mvwscanw(connectionWindow, 12, 6, name);

    memset(packet, JOIN, 1);
    strcpy(packet+1, name);

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
        char zajebal[500] = {0};
        sprintf(zajebal, "rcvd: %d\n", (int)startPacket[0]);
        epicDebug(zajebal);


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
 * Continuously listen to the game server and act on packets
 *
 * @param conn
 * @return
 */
void *listenToServer(void *conn) {
    int sock = *(int *) conn;
    ssize_t readSize;
    char message[MAX_PACKET_SIZE];
    writeToWindow(notificationWindow, ++notificationCounter, 1, "Started listening to the server");
    FILE *f = fopen("file2.txt", "a");
    fprintf(f, "%s\n", "WAITING???");

    while ((readSize = recv(sock, message, MAX_PACKET_SIZE, 0)) > 0) {
//        fprintf(f, "%s\n", "Received some shit packet");
//        fprintf(f, "%i\n", (int)readSize);

        int packetType = (int)message[0];



        char type[500] = {0};

        sprintf(type, "received packet %d", packetType);

//        epicDebug(type);

        writeToWindow(notificationWindow, ++notificationCounter, 0, type);

        switch (packetType) {
            case JOINED:
//                playerJoinedEvent(message);
                break;
            case PLAYER_DISCONNECTED:
//                playerDisconnectedEvent(message);
                break;
            case END:
//                endGame();
                break;
            case MAP:
                drawMap(message);
            default:
                break;
        }

        //clear the message buffer
        memset(message, 0, MAX_PACKET_SIZE);
    }
    fclose(f);

    if (readSize == 0) {
        exitWithMessage("Server went offline");
    } else if (readSize == -1) {
        exitWithMessage("Failed to communicate with the server!");
    }

    return 0;

//    int sock = clientInfo.clientSock;
//    ssize_t read_size;
//    char *message, client_message[2000], *new_message;
//
//    //Send some messages to the client
//    message = "Greetings! I am your connection handler\n";
//    write(sock, message, strlen(message));
//
//    message = "Now type something and i shall repeat what you type \n";
//    write(sock, message, strlen(message));
//
//    //Receive a message from client
//    while ((read_size = recv(sock, client_message, 2000, 0)) > 0) {
//        //end of string marker
//        client_message[read_size] = '\0';
//        sscanf(client_message, "name: %s", clientInfo.name);
//
//        for (int i = 0; i < MAX_PLAYERS; i++) {
//            if (clientSockets[i] > 0) {
//                asprintf(&new_message,"%s (ID: %d) sent: %s\n", clientInfo.name, clientInfo.id, client_message);
//                write(clientSockets[i], new_message, strlen(new_message));
//            }
//        }
//
//        //clear the message buffer
//        memset(client_message, 0, 2000);
//    }
//
//    if (read_size == 0) {
//        puts("Client disconnected");
//        fflush(stdout);
//    }
//    else if (read_size == -1) {
//        perror("recv failed");
//    }
//
//    return 0;
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
    writeToWindow(notificationWindow, ++notificationCounter, 1, "Received map from the server");

    for (int i = 0; i < mapH; ++i) {
        for (int j = 0; j < mapW; ++j) {
            // +1 to the positions is needed so that graphics do not overlap the world box
            int blockType = (int)*((map+j)+i*mapW);

            switch (blockType) {
                case Wall:
                    mvwaddch(mainWindow, i+1, j+1, 97 | A_ALTCHARSET);
                    break;
                case Dot:
                    mvwaddch(mainWindow, i+1, j+1, '.');
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

    wrefresh(mainWindow);
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