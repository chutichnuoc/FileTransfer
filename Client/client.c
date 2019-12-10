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
#include<sys/time.h>

const int _bufferLength = 1024;
const char* _quitCommand = "@quit";

// Status
char fileName[1024];
int clientCount = 0;
int receivedClient = 0;
int gobalSize = -1;
bool received = false;

pthread_mutex_t mptr_clientCount = PTHREAD_MUTEX_INITIALIZER;

// Show current time
void showTime() {
	struct timeval now;
	gettimeofday(&now, NULL);
	double timeInMs = (now.tv_sec + (now.tv_usec / 1000000.0)) * 1000.0;
	time_t timeInS = timeInMs / 1000;
	struct tm* timeInHMS = localtime(&timeInS);
	int milli = ((timeInMs / 1000) - ((int)(timeInMs / 1000))) * 1000;
	printf("Current time: %d:%d:%d:%d\n", timeInHMS->tm_hour, timeInHMS->tm_min, timeInHMS->tm_sec, milli);
}

// Called when need to reset client's status
void resetStatus() {
	receivedClient = 0;
	received = false;
}

// Multi thread
void* handleRequest(void* arg) {
	int connClientSocket = *((int*)arg);
	free(arg);
	pthread_detach(pthread_self());

	char buffer[_bufferLength];
	// Wait until received first peice of file from server
	while (1) {
		if (received) break;
	}

	FILE* file = fopen(fileName, "rb+");
	int size;
	// Handle error
	if (file == NULL) {
		size = -1;
		printf("Cannot find file '%s'!\n", fileName);
		size = htonl(size);
		write(connClientSocket, (void*)&size, sizeof(size));
		pthread_mutex_lock(&mptr_clientCount);
		receivedClient++;
		pthread_mutex_unlock(&mptr_clientCount);
	}
	else {
		printf("Sending file '%s'\n", fileName);
		size = gobalSize;
		fseek(file, 0, SEEK_SET);
		printf("File size: %d\n", size);
		size = htonl(size);
		write(connClientSocket, (void*)&size, sizeof(size));
		int sendSize = 0;
		// Send file
		while (sendSize < gobalSize) {
			// Read file to buffer
			int readSize = fread(buffer, 1, sizeof(buffer) - 1, file);
			write(connClientSocket, buffer, readSize);
			sendSize += readSize;
			// Zero out buffer after writing
			bzero(buffer, sizeof(buffer));
		}
		fclose(file);
		pthread_mutex_lock(&mptr_clientCount);
		receivedClient++; // Increase number of clients that received file
		pthread_mutex_unlock(&mptr_clientCount);
	}
	close(connClientSocket);
	return NULL;
}

int main() {
	const int _family = AF_INET;    // IPv4
	const int _type = SOCK_STREAM;  // TCP
	const int _protocol = 0;
	const int _listenQueue = 1024;
	const int _port = 9090;

	int serverSocket; // Connect to server
	int shareFileSocket; // Act like server with other clients
	int otherClientSocket; // Connect to other client
	int* connClientSocket;
	struct sockaddr_in serverAddr;
	struct sockaddr_in shareFileAddr, connClientAddr;
	struct sockaddr_in otherClientAddr;

	int downloadType;
	char receiveBuffer[_bufferLength];

	int nbytes = 0;

	char* serverIpAddr = (char*)malloc(100 * sizeof(char*));
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
	int connCheck = connect(serverSocket, (struct sockaddr*) & serverAddr, sizeof(serverAddr));
	if (connCheck < 0) {
		perror("Connect error");
		return 1;
	}

	printf("Connected to server.\n");

	while (1) {
		bzero(fileName, sizeof(fileName));
		read(serverSocket, fileName, sizeof(fileName));
		printf("fileName: %s\n", fileName);
		if (strcmp(fileName, _quitCommand) == 0) {
			break;
		}

		printf("Filename: %s\n", fileName);

		read(serverSocket, &downloadType, sizeof(downloadType));
		printf("DownloadType: %d\n", downloadType);

		// Receive file from server and send file to other clients
		if (downloadType == 1) {
			printf("\nAct like server now\n");
			shareFileSocket = socket(_family, _type, _protocol);
			if (shareFileSocket < 0) {
				perror("Server socket error");
				return 1;
			}

			int option = 1;
			setsockopt(shareFileSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
			bzero(&serverAddr, sizeof(serverAddr));

			shareFileAddr.sin_addr.s_addr = htonl(INADDR_ANY);
			shareFileAddr.sin_port = htons(_port);
			shareFileAddr.sin_family = _family;

			int bindCheck = bind(shareFileSocket, (struct sockaddr*) & shareFileAddr, sizeof(shareFileAddr));
			if (bindCheck < 0) {
				perror("Bind error");
				return 1;
			}

			int listenCheck = listen(shareFileSocket, _listenQueue);
			if (listenCheck < 0) {
				perror("Listen error");
				return 1;
			}

			printf("Server is listening at port %d. Waiting for connection...\n", _port);
			unsigned int addrLength = sizeof(connClientAddr);

			// Wait for other clients to connect
			while (clientCount != 2) {
				connClientSocket = malloc(sizeof(int));
				*connClientSocket = accept(shareFileSocket, (struct sockaddr*) & connClientAddr, &addrLength);
				if (connClientSocket < 0) {
					if (errno == EINTR) continue;
					else {
						perror("Accept error");
						return 1;
					}
				}
				// Get client info
				char* clientIpAddr = inet_ntoa(connClientAddr.sin_addr);
				int clientPort = ntohs(connClientAddr.sin_port);

				printf("Client address: %s:%d\n", clientIpAddr, clientPort);

				// Create a thread to handle requests of client
				pthread_t tid;
				pthread_create(&tid, NULL, &handleRequest, (void*)connClientSocket);
				clientCount++;
			}
			clientCount = 0;
			close(shareFileSocket);

			// Receive file size
			unsigned int fileSize = 0;
			unsigned int receivedSize = 0;
			nbytes = read(serverSocket, &fileSize, sizeof(fileSize));
			if (nbytes < 0) {
				perror("Read error");
				return 1;
			}
			fileSize = ntohl(fileSize);
			gobalSize = fileSize;
			// Handle error
			if (fileSize == -1) {
				printf("Cannot download file '%s'!\n", fileName);
				received = true;
				int done = 1;
				write(serverSocket, &done, sizeof(done));
			}
			// Receive file
			else {
				FILE* file = fopen(fileName, "wb+");
				printf("Receiving file '%s'\n", fileName);
				printf("File size: %d\n", fileSize);
				received = true;
				while (receivedSize < fileSize) {
					int currRcvSize = read(serverSocket, receiveBuffer, _bufferLength);
					receivedSize += currRcvSize;
					fwrite(receiveBuffer, 1, currRcvSize, file);
				}
				fclose(file);
				printf("Received file successfully!\n");
				showTime();
				int done = 1;
				write(serverSocket, &done, sizeof(done));
			}
			// Wait until other clients received file
			while (1) {
				if (receivedClient == 2) break;
			}
			resetStatus();
			printf("Closed\n\n");
		}
		// Received IP of client which have file, then download from that client
		else if (downloadType == 2) {
			read(serverSocket, receiveBuffer, sizeof(receiveBuffer));

			printf("Download from %s\n", receiveBuffer);

			otherClientSocket = socket(_family, _type, _protocol);
			if (otherClientSocket < 0) {
				perror("Server socket error");
				return 1;
			}
			otherClientAddr.sin_addr.s_addr = inet_addr(receiveBuffer);
			otherClientAddr.sin_port = htons(_port);
			otherClientAddr.sin_family = _family;

			// Connect to server by using serverSocket
			int connCheck = -1;
			while (connCheck < 0) {
				connCheck = connect(otherClientSocket, (struct sockaddr*) & otherClientAddr, sizeof(otherClientAddr));
			}
			if (connCheck < 0) {
				perror("Connect error");
				return 1;
			}

			printf("Connected to server.\n");

			// Receive file size
			unsigned int fileSize = 0;
			unsigned int receivedSize = 0;
			nbytes = read(otherClientSocket, &fileSize, sizeof(fileSize));
			if (nbytes < 0) {
				perror("Read error");
				return 1;
			}
			fileSize = ntohl(fileSize);
			// Handle error
			if (fileSize == -1) {
				printf("Cannot download file '%s'!\n", fileName);
				//continue;
			}
			// Receive file
			else {
				FILE* file = fopen(fileName, "wb");
				printf("Receiving file '%s'\n", fileName);
				printf("File size: %d\n", fileSize);
				while (receivedSize < fileSize) {
					int currRcvSize = read(otherClientSocket, receiveBuffer, _bufferLength);
					receivedSize += currRcvSize;
					fwrite(receiveBuffer, 1, currRcvSize, file);
				}
				fclose(file);
				printf("Received file successfully!\n");
				showTime();
			}
			close(otherClientSocket);
			printf("Closed\n\n");
			int done = 1;
			write(serverSocket, &done, sizeof(done));
		}
	}

	close(serverSocket);
	printf("Closed connection.\n");

	return 0;
}

