#include<sys/socket.h>
#include<sys/wait.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<unistd.h>
#include<string.h>
#include<ctype.h>
#include<pthread.h>
#include<errno.h>
#include<time.h>
#include<stdlib.h>
#include<stdio.h>
#include<stdbool.h>
#include<time.h>

const int _bufferLength = 1024;
const char *_terminateChar = "@exit";

char fileName[1024];

int clientCountInside = 0;
int clientCountOutside = 0;
pthread_mutex_t mptr_clientCount = PTHREAD_MUTEX_INITIALIZER;

void *handleRequest(void *arg) {
    int connClientSocket = *((int *) arg);
    free(arg);
    pthread_detach(pthread_self());

    // Receive file name from client and send file back
    char buffer[_bufferLength];

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
        fclose(file);
        printf("Sent file successfully!\n");
        pthread_mutex_lock(&mptr_clientCount);
        clientCountInside++;
        printf("Total Clients: %d\n", clientCountInside);
        pthread_mutex_unlock(&mptr_clientCount);
    }
    close(connClientSocket);
    return NULL;
}

int main() {
    const int _family = AF_INET;	// IPv4
    const int _type = SOCK_STREAM;	// TCP
    const int _protocol = 0;
    const int _listenQueue = 1024;
    const int _port = 9090;

    int serverSocket;
    int serverSocket2;
    int serverSocket3;
    int *connClientSocket;
    struct sockaddr_in serverAddr;
    struct sockaddr_in serverAddr2, connClientAddr;
    struct sockaddr_in serverAddr3;

    int downloadType;
    char receiveBuffer[_bufferLength];

    int nbytes = 0;

    char* serverIpAddr = (char *) malloc(100 * sizeof(char *));
    int serverPort = 8080;

    serverSocket = socket(_family, _type, _protocol);
    if (serverSocket < 0) {
        perror("Server socket error");
        return 1;
    }

    bzero(&serverAddr, sizeof(serverAddr));

    printf("Server IP Address: ");
    scanf("%s", serverIpAddr);

    serverAddr.sin_addr.s_addr = inet_addr(serverIpAddr);
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_family = _family;

    // Connect to server by using serverSocket
    int connCheck = connect(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
    if (connCheck < 0) {
        perror("Connect error");
        return 1;
    }

    printf("Connected to server.\n");

    while (1) {
        bzero(fileName, sizeof(fileName));
        //printf("Still here\n");
        //nbytes =
        read(serverSocket, fileName, sizeof(fileName));
        //printf("nbytes: %d\n", nbytes);
        if (strcmp(fileName, _terminateChar) == 0) {
            break;
        }

        printf("Filename: %s\n", fileName);

        read (serverSocket, &downloadType, sizeof(downloadType));
        printf("DownloadType: %d\n", downloadType);
        if (downloadType == 1) {
            // Receive file size
            unsigned int fileSize = 0;
            unsigned int receivedSize = 0;
            nbytes = read(serverSocket, &fileSize, sizeof(fileSize));
            if (nbytes < 0) {
                perror("Read error");
                return 1;
            }
            fileSize = ntohl(fileSize);
            // Handle error
            if (fileSize == -1) {
                printf("Cannot download file '%s'!\n", fileName);
                continue;
            }
            // Receive file
            else {
                FILE *file = fopen(fileName, "wb");
                printf("Receiving file '%s'\n", fileName);
                printf("File size: %d\n", fileSize);
                while (receivedSize < fileSize) {
                    int currRcvSize = read(serverSocket, receiveBuffer, _bufferLength);
                    receivedSize += currRcvSize;
                    fwrite(receiveBuffer, 1, currRcvSize, file);
                    //printf ("currRcvSize: %d\n", currRcvSize);
                }
                //fseek(file, 0, SEEK_END);
                //fileSize = ftell(file);
                //(file, 0, SEEK_SET);
                printf("Received size: %d\n", receivedSize);
                //printf("File size: %d\n", fileSize);
                fclose(file);
                printf("Received file successfully!\n");
                int done = 1;
                write (serverSocket, &done, sizeof(done));
            }

            printf("\nAct like server now\n");

            serverSocket2 = socket(_family, _type, _protocol);
            if (serverSocket2 < 0) {
                perror("Server socket error");
                return 1;
            }

            int option = 1;
            setsockopt(serverSocket2, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
            bzero(&serverAddr, sizeof(serverAddr));

            serverAddr2.sin_addr.s_addr = htonl(INADDR_ANY);
            serverAddr2.sin_port = htons(_port);
            serverAddr2.sin_family = _family;

            int bindCheck = bind(serverSocket2, (struct sockaddr *) &serverAddr2, sizeof(serverAddr2));
            if (bindCheck < 0) {
                perror("Bind error");
                return 1;
            }

            int listenCheck = listen(serverSocket2, _listenQueue);
            if (listenCheck < 0) {
                perror("Listen error");
                return 1;
            }

            printf("Server is listening at port %d. Waiting for connection...\n", _port);
            unsigned int addrLength = sizeof(connClientAddr);

            while (1) {
                if (clientCountOutside == 2)
                {
                    while (1)
                    {
                        //DO NOTHING
                        if (clientCountInside == 2) break;
                    }
                    break;
                }
                connClientSocket = malloc(sizeof(int));
                *connClientSocket = accept(serverSocket2, (struct sockaddr *) &connClientAddr, &addrLength);
                if (connClientSocket < 0) {
                    if (errno == EINTR) continue;
                    else {
                        perror("Accept error");
                        return 1;
                    }
                }
                // Get client info
                char *clientIpAddr = inet_ntoa(connClientAddr.sin_addr);
                int clientPort = ntohs(connClientAddr.sin_port);

                printf("Client address: %s:%d\n", clientIpAddr, clientPort);

                // Create a thread to handle requests of client
                pthread_t tid;
                pthread_create(&tid, NULL, &handleRequest, (void *) connClientSocket);
                clientCountOutside++;
            }
            clientCountInside = 0;
            clientCountOutside = 0;
            close(serverSocket2);
            printf("Closed\n\n");
        }
        else if (downloadType == 2) {
            read (serverSocket, receiveBuffer, sizeof(receiveBuffer));

            printf("Download from %s\n", receiveBuffer);

            serverSocket3 = socket(_family, _type, _protocol);
            if (serverSocket3 < 0) {
                perror("Server socket error");
                return 1;
            }
            serverAddr3.sin_addr.s_addr = inet_addr(receiveBuffer);
            serverAddr3.sin_port = htons(_port);
            serverAddr3.sin_family = _family;

            // Connect to server by using serverSocket
            int connCheck = -1;
            while (connCheck < 0) {
                connCheck = connect(serverSocket3, (struct sockaddr *) &serverAddr3, sizeof(serverAddr3));
            }
            if (connCheck < 0) {
                perror("Connect error");
                return 1;
            }

            printf("Connected to server.\n");

            // Receive file size
            unsigned int fileSize = 0;
            unsigned int receivedSize = 0;
            nbytes = read(serverSocket3, &fileSize, sizeof(fileSize));
            if (nbytes < 0) {
                perror("Read error");
                return 1;
            }
            fileSize = ntohl(fileSize);
            // Handle error
            if (fileSize == -1) {
                printf("Cannot download file '%s'!\n", fileName);
            }
            // Receive file
            else {
                FILE *file = fopen(fileName, "wb");
                printf("Receiving file '%s'\n", fileName);
                printf("File size: %d\n", fileSize);
                while (receivedSize < fileSize) {
                    int currRcvSize = read(serverSocket3, receiveBuffer, _bufferLength);
                    receivedSize += currRcvSize;
                    fwrite(receiveBuffer, 1, currRcvSize, file);
                }
                fclose(file);
                printf("Received file successfully!\n");
            }
            close(serverSocket3);
            printf("Closed\n\n");
            int done = 1;
            write (serverSocket, &done, sizeof(done));
        }

    }

    close(serverSocket);
    printf("Closed connection.\n");

    return 0;
}

