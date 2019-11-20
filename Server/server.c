#include<sys/socket.h>
#include<sys/wait.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<unistd.h>
#include<string.h>
#include<ctype.h>
#include<pthread.h>
#include<errno.h>
#include<stdlib.h>
#include<stdio.h>
#include<stdbool.h>

const int _listenQueue = 1024;	// Backlog in listen()
const int _bufferLength = 1024;
const char *_terminateChar = "@logout";

int downloadType = 0;

char *firstClientAddr = NULL;
char *firstClientAddrTmp = NULL;
bool isFileSent = false;

int clientCount = 0;
pthread_mutex_t mptr_clientCount = PTHREAD_MUTEX_INITIALIZER;

void *handleRequest(void *arg) {
    int connClientSocket = *((int *) arg);
    free(arg);
    pthread_detach(pthread_self());

    // Receive file name from client and send file back
    char buffer[_bufferLength];
    char fileName[_bufferLength];

    while (read(connClientSocket, fileName, sizeof(fileName)) > 0) {
        if (strcmp(fileName, _terminateChar) == 0) {
            break;
        }
        if (firstClientAddr == NULL) {
            firstClientAddr = firstClientAddrTmp;
            printf("File name: %s\n", fileName);
            downloadType = 1;
            write(connClientSocket, &downloadType, sizeof(downloadType));

            FILE *file = fopen(fileName, "rb");
            int size;
            // Handle error
            if (file == NULL) {
                size = -1;
                printf("Cannot find file '%s'!\n", fileName);
                size = htonl(size);
                write(connClientSocket, (void *) &size, sizeof(size));
            }
            else {
                printf("Sending file '%s'\n", fileName);
                // Send file length first
                fseek(file, 0, SEEK_END);
                size = ftell(file);
                fseek(file, 0, SEEK_SET);
                printf("File size: %d\n", size);
                size = htonl(size);
                write(connClientSocket, (void *) &size, sizeof(size));
                // Send file
                while (!feof(file)) {
                    // Read file to buffer
                    int readSize = fread(buffer, 1, sizeof(buffer) - 1, file);
                    write(connClientSocket, buffer, readSize);
                    // Zero out buffer after writing
                    bzero(buffer, sizeof(buffer));
                }
                printf("Sent file successfully!\n");
            }
            isFileSent = true;
        }
        else {
            downloadType = 2;
            write(connClientSocket, &downloadType, sizeof(downloadType));
            while(true) {
                //DO NOTHING
                if (isFileSent) break;
            }
            write(connClientSocket, firstClientAddr, _bufferLength);
        }
        pthread_mutex_lock(&mptr_clientCount);
        clientCount++;
        printf("Total Clients: %d\n", clientCount);
        pthread_mutex_unlock(&mptr_clientCount);
        if (clientCount == 3) {
            printf("Reset now\n");
            firstClientAddr = NULL;
            isFileSent = false;
            clientCount = 0;

        }
    }
    close(connClientSocket);
    return NULL;
}

int main() {
    const int _family = AF_INET;	// IPv4
    const int _type = SOCK_STREAM;	// TCP
    const int _protocol = 0;
    const int _port = 8080;

    int serverSocket;
    int *connClientSocket;
    struct sockaddr_in serverAddr, connClientAddr;

    // Create a socket to listen
    serverSocket = socket(_family, _type, _protocol);
    if (serverSocket < 0) {
        perror("Server socket error");
        return 1;
    }

    int option = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));


    bzero(&serverAddr, sizeof(serverAddr));

    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(_port);
    serverAddr.sin_family = _family;

    int bindCheck = bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
    if (bindCheck < 0) {
        perror("Bind error");
        return 1;
    }

    int listenCheck = listen(serverSocket, _listenQueue);
    if (listenCheck < 0) {
        perror("Listen error");
        return 1;
    }

    printf("Server is listening at port %d. Waiting for connection...\n", _port);

    unsigned int addrLength = sizeof(connClientAddr);

    while (1) {
        connClientSocket = malloc(sizeof(int));
        *connClientSocket = accept(serverSocket, (struct sockaddr *) &connClientAddr, &addrLength);
        if (connClientSocket < 0) {
            if (errno == EINTR) continue;
            else {
                perror("Accept error");
                return 1;
            }
        }
        // Get client info
        char *clientIpAddr = inet_ntoa(connClientAddr.sin_addr);
        firstClientAddrTmp = inet_ntoa(connClientAddr.sin_addr);
        int clientPort = ntohs(connClientAddr.sin_port);

        printf("Client address: %s:%d\n", clientIpAddr, clientPort);

        // Create a thread to handle requests of client
        pthread_t tid;
        pthread_create(&tid, NULL, &handleRequest, (void *) connClientSocket);
    }

    close(serverSocket);
    return 0;
}
