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

#define MAX_PLAYERS 16

void exitWithMessage(char error[]);

void *handle_connection(void *socket_desc);

void processArgs(int argc, char *argv[]);

void *safe_malloc(size_t);

int startServer();

/*
 * Enumerations
 * http://en.cppreference.com/w/c/language/enum
 */

// Packet type enumerations
enum packet_t {
    JOIN, ACK, START, END, MAP, PLAYERS, SCORE, MOVE ,MESSAGE, QUIT, JOINED, PLAYER_DISCONNECTED
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
    int clientSock;
    unsigned int id;
    in_addr_t ip;
    char name[20];
} clientInfo_t;


/*
 * Globals
 */
unsigned int CLIENT_ID_ITERATOR = 1;
int clientSockets[MAX_PLAYERS] = {0};


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

// Define the function to be called when ctrl-c (SIGINT) signal is sent to process
void signal_callback_handler(int signum) {
    printf("Caught signal %d\n", signum);
    // Cleanup and close up stuff here

    // Terminate program
    exit(signum);
}

int startServer() {
    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Unable to create a socket");
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8888);

    //Bind
    if (bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) < 0) {
        printf("Unable to bind");
        return 1;
    }

    //Listen
    listen(socket_desc, 3);

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);


    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    pthread_t thread_id;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        clientSockets[i] = -1;
    }


    while ((client_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t *) &c))) {
        puts("Connection accepted");
        clientInfo_t currentClient;
        currentClient.id = CLIENT_ID_ITERATOR++;
        currentClient.clientSock = client_sock;
        currentClient.ip = client.sin_addr.s_addr;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clientSockets[i] == -1) {
                clientSockets[i] = client_sock;
                break;
            }
        }

        if (pthread_create(&thread_id, NULL, handle_connection, (void *) &currentClient) < 0) {
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

void *handle_connection(void *conn) {
    //Get the socket descriptor

    clientInfo_t clientInfo = *(clientInfo_t *) conn;
    int sock = clientInfo.clientSock;
    ssize_t read_size;
    char *message, client_message[2000];

    //Send some messages to the client
    message = "Greetings! I am your connection handler\n";
    write(sock, message, strlen(message));

    message = "Now type something and i shall repeat what you type \n";
    write(sock, message, strlen(message));

    //Receive a message from client
    while ((read_size = recv(sock, client_message, 2000, 0)) > 0) {
        //end of string marker
        client_message[read_size] = '\0';
        sscanf(client_message, "name: %s", clientInfo.name);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clientSockets[i] > 0) {
                snprintf(client_message,2000,"%s (ID: %d) sent: %s\n", clientInfo.name, clientInfo.id, client_message);
                write(clientSockets[i], client_message, strlen(client_message));
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
    }

    return 0;
}


int main(int argc, char *argv[]) {
    signal(SIGINT, signal_callback_handler);

    startServer();


    return 0;
}
