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
WINDOW *chatWindow;
WINDOW *connectionWindow;
WINDOW *errorWindow;

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


/**
 * Definitions
 */
#define WORLD_WIDTH 100
#define WORLD_HEIGHT 40
#define CONNECTION_WIDTH 40
#define CONNECTION_HEIGHT 20
#define ERROR_WIDTH 40
#define ERROR_HEIGHT 10


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
    windowDeleteAction(chatWindow);
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
    char message[1000], serverReply[2000], serverAddress[16], serverPort[6];

    initCurses();
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

    int currentClient;
    currentClient = 1;

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, listenToServer, (void *) &sock) < 0) {
        exitWithMessage("could not create thread");
    }
    pthread_join(thread_id , NULL);

    //keep communicating with server
    while(1) {
        scanf("%s" , message);

        //Send some data
        if( send(sock , message , strlen(message) , 0) < 0)
        {
            puts("Send failed");
            return 1;
        }

        //Receive a reply from the server
        if( recv(sock , serverReply , 2000 , 0) < 0)
        {
            puts("recv failed");
            break;
        }
        puts(serverReply);
    }

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
    scanf("%s", address);


    refresh();
//    mvwscanw(connectionWindow, 3, 6, address);
    writeToWindow(connectionWindow, 5, 4, "Enter server port: ");
    refresh();
//    mvwscanw(connectionWindow, 6, 6, port);
    wmove(connectionWindow, 6, 6);
    scanf("%s", port);
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

void *listenToServer(void *conn) {
    //Get the socket descriptor

    int sock = *(int *) conn;
    ssize_t read_size;
    char *message, client_message[2000], *new_message;

    while ((read_size = recv(sock, client_message, 2000, 0)) > 0) {
        //end of string marker
        client_message[read_size] = '\0';

        puts(client_message);

        //clear the message buffer
        memset(client_message, 0, 2000);
    }

    if (read_size == 0) {
        puts("Server is gone :(");
        fflush(stdout);
    }
    else if (read_size == -1) {
        perror("recv failed");
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
    offsetY = (LINES - WORLD_HEIGHT) / 2;

    mainWindow = newwin(WORLD_HEIGHT, WORLD_WIDTH, offsetY, offsetX);

    box(mainWindow, 0, 0);

    wrefresh(mainWindow);
}
