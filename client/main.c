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

/**
 * GLOBAL VARIABLES
 */
int sock;


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

void *safe_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) { //Ja alokācija neizdevās, izdrukājam iemeslu, kurš iegūts no errno
        fprintf(stderr, "%s\n", strerror(errno));

        exit(EXIT_FAILURE);
    }
    return p;
}

/*
 * Paligfunkcija kura palidz programmai iziet kludas gadijuma
 */
void exitWithMessage(char error[]) {
    printf("Error %s\n", error);
    exit(EXIT_FAILURE);
}

void processArgs(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            //TODO
        }
        else if (strcmp(argv[i], "-s") == 0) {
            //TODO
        }
        else if (strcmp(argv[i], "-h") == 0) {
            //TODO
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    char message[1000], serverReply[2000], serverAddress[16], serverPort[6];

    //Create socket
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }

    strcpy(serverAddress, "127.0.0.1");
    strcpy(serverPort, "8888");

//    printf("Enter IP address to connect to: ");
//    scanf("%s" , serverAddress);
//    printf("Enter server port: ");
//    scanf("%s" , serverPort);

    server.sin_addr.s_addr = inet_addr(serverAddress);
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(serverPort));

    puts("Connecting to server...");

    //Connect to remote server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        perror("Connection failed");
        return 1;
    }

    puts("Connected to the server!");

    sendJoinRequest();
    receiveJoinResponse();

    int currentClient;
    currentClient = 1;

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, listenToServer, (void *) &sock) < 0) {
        perror("could not create thread");
        return 1;
    }
    pthread_join(thread_id , NULL);

    //keep communicating with server
    while(1)
    {
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
 * Send join request to the server
 */
void sendJoinRequest() {
    puts("Sending request to join...\n");
    char name[20];
    char packet[21];

    memset(packet, '\0', sizeof(packet));
    memset(name, '\0', sizeof(name));

    printf("Enter your name: ");
    scanf("%s", name);

    memset(packet, JOIN, 1);
    strcpy(packet+1, name);

    // Send join request packet
    if(send(sock, packet, sizeof(packet) , 0) < 0)
    {
        exitWithMessage("Join request has failed. Please check your internet connection and try again.");
    }
}

/**
 * Receive join response from the server
 */
void receiveJoinResponse() {
    char response[sizeof(int) + 1] = {0};
    int responseCode;

    if(recv(sock, response, sizeof(int) + 1, 0))
    {
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
