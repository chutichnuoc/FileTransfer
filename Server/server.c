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
#include<time.h>
#include<sys/time.h>

const int _listenQueue = 1024;  // Backlog in listen()
const int _bufferLength = 1024;
const char* _quitCommand = "@quit";

// Status
char* lastClientAddr = NULL;
char* lastClientAddrTmp = NULL;
char fileName[1024] = "";
int connectedClient = 0;
int receivedNameClient = 0;
int receivedFileClient = 0;
bool isFileSent = false;
bool allReceived = true;

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

// Called when need to reset server's status
void resetStatus() {
	receivedNameClient = 0;
	receivedFileClient = 0;
	isFileSent = false;
	allReceived = true;
}

// Called when all client received file
void setFileName() {
	bzero(fileName, sizeof(fileName));
	printf("\nEnter file name to send: ");
	scanf("%s", fileName);
	showTime();
	resetStatus();
}

// Multi thread
void* handleRequest(void* arg) {
	int connClientSocket = *((int*)arg);
	free(arg);
	pthread_detach(pthread_self());

	char buffer[_bufferLength];
	int downloadType = 0;

	// Wait until 3 clients connect to server, then enter file's name, only run in thread of third client
	if (connectedClient == 3) {
		printf("\nEnter file name to send: ");
		scanf("%s", fileName);
		showTime();
	}
	// First and second client will wait here until third client connect and server enter file's name
	while (1) {
		if (connectedClient == 3 && strcmp(fileName, "") != 0) {
			break;
		}
	}
	// Send file to client
	while (1) {
		if (!allReceived) continue; // If a client received file, it must wait for two others
		write(connClientSocket, fileName, sizeof(fileName));
		pthread_mutex_lock(&mptr_clientCount);
		receivedNameClient++; // Increase number of clients that received file'name
		pthread_mutex_unlock(&mptr_clientCount);

		// If server enter quitCommand, break
		if (strcmp(fileName, _quitCommand) == 0) {
			break;
		}

		// If downloadType = 1, client will receive file and send to others
		if (lastClientAddr == NULL || downloadType == 1) {
			lastClientAddr = lastClientAddrTmp;
			downloadType = 1;
			write(connClientSocket, &downloadType, sizeof(downloadType));
			// Wait until all clients have file's name
			while (1) {
				if (receivedNameClient == 3) break;
			}
			FILE* file = fopen(fileName, "rb");
			int size;
			// Handle error
			if (file == NULL) {
				size = -1;
				printf("Cannot find file '%s'!\n", fileName);
				size = htonl(size);
				write(connClientSocket, (void*)&size, sizeof(size));
				isFileSent = true;
			}
			else {
				printf("Sending file '%s'\n", fileName);
				// Send file length first
				fseek(file, 0, SEEK_END);
				size = ftell(file);
				fseek(file, 0, SEEK_SET);
				printf("File size: %d\n", size);
				size = htonl(size);
				write(connClientSocket, (void*)&size, sizeof(size));
				// Send file
				while (!feof(file)) {
					// Read file to buffer
					int readSize = fread(buffer, 1, sizeof(buffer) - 1, file);
					write(connClientSocket, buffer, readSize);
					// Zero out buffer after writing
					bzero(buffer, sizeof(buffer));
					isFileSent = true;
				}
				fclose(file);
				printf("Sent file successfully!\n");
			}
		}
		// If downloadType = 2, client will receive IP of other client which downloaded file from server
		else {
			downloadType = 2;
			write(connClientSocket, &downloadType, sizeof(downloadType));
			write(connClientSocket, lastClientAddr, _bufferLength);
			// Wait until downloadType = 1 client receive first peice of file
			while (1) {
				if (isFileSent) break;
			}
		}

		if (receivedNameClient == 3) {
			allReceived = false;
		}

		int done = 0;
		read(connClientSocket, &done, sizeof(done));
		if (done == 1) {
			pthread_mutex_lock(&mptr_clientCount);
			receivedFileClient++; // Increase number of client that received file
			pthread_mutex_unlock(&mptr_clientCount);
		}

		// Enter file's name again
		if (receivedFileClient == 3) {
			setFileName();
		}
	}
	close(connClientSocket);
	return NULL;
}

int main() {
	const int _family = AF_INET;    // IPv4
	const int _type = SOCK_STREAM;  // TCP
	const int _protocol = 0;
	const int _port = 8080;

	int serverSocket;
	int* connClientSocket;
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

	int bindCheck = bind(serverSocket, (struct sockaddr*) & serverAddr, sizeof(serverAddr));
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
		if (connectedClient == 3) {
			while (1) {
				if (strcmp(fileName, _quitCommand) == 0 && receivedNameClient == 3) {
					break;
				}
			}
			break;
		}
		connClientSocket = malloc(sizeof(int));
		*connClientSocket = accept(serverSocket, (struct sockaddr*) & connClientAddr, &addrLength);
		if (connClientSocket < 0) {
			if (errno == EINTR) continue;
			else {
				perror("Accept error");
				return 1;
			}
		}
		// Get client info
		char* clientIpAddr = inet_ntoa(connClientAddr.sin_addr);
		lastClientAddrTmp = inet_ntoa(connClientAddr.sin_addr);
		int clientPort = ntohs(connClientAddr.sin_port);

		printf("Client address: %s:%d\n", clientIpAddr, clientPort);
		connectedClient++;

		// Create a thread to handle requests of client
		pthread_t tid;
		pthread_create(&tid, NULL, &handleRequest, (void*)connClientSocket);
	}

	close(serverSocket);
	return 0;
}

