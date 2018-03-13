#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_TEXT_SIZE 1024
#define MAX_CLIENT_NUM 10

/*
Queue implementation using a char array.
Contains a mutex for functions to lock on before modifying the array,
and condition variables for when it's not empty or full.
*/
typedef struct {
    char *buffer[MAX_TEXT_SIZE];
    int head, tail;
    int full, empty;
    pthread_mutex_t *mutex;
    pthread_cond_t *notFull, *notEmpty;
} queue;

/*
Struct containing important data for the server to work.
Namely the list of client sockets, that list's mutex,
the server's socket for new connections, and the message queue
*/
typedef struct {
    fd_set serverReadFds;
    int socketFd;
    int clientSockets[MAX_CLIENT_NUM];
    int numClients;
    pthread_mutex_t *clientListMutex;
    queue *queue;
} chatDataVars;

/*
Simple struct to hold the chatDataVars and the new client's socket fd.
Used only in the client handler thread.
*/
typedef struct {
    chatDataVars *data;
    int clientSocketFd;
} clientHandlerVars;


void *newClientHandler(void *data);
void *clientHandler(void *chv);
void *messageHandler(void *data);

void queueDestroy(queue *q);
queue* queueInit(void);
void queuePush(queue *q, char* msg);
char* queuePop(queue *q);

queue* queueInit(void)
{
    queue *q = (queue *)malloc(sizeof(queue));
    if(q == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }

    q->empty = 1;
    q->full = q->head = q->tail = 0;
    q->mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    if(q->mutex == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(q->mutex, NULL);

    q->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    if(q->notFull == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);   
    }
    pthread_cond_init(q->notFull, NULL);

    q->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    if(q->notEmpty == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }
    pthread_cond_init(q->notEmpty, NULL);

    return q;
}

void bindSocket(struct sockaddr_in *serverAddr, int socketFd, long port)
{
    memset(serverAddr, 0, sizeof(*serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr->sin_port = htons(port);

    if(bind(socketFd, (struct sockaddr *)serverAddr, sizeof(struct sockaddr_in)) == -1)
    {
        perror("Socket bind failed: ");
        exit(1);
    }
}

//Thread to handle new connections. Adds client's fd to list of client fds and spawns a new clientHandler thread for it
void *newClientHandler(void *data)
{
    chatDataVars *chatData = (chatDataVars *) data;
    while(1)
    {
        int clientSocketFd = accept(chatData->socketFd, NULL, NULL);
        if(clientSocketFd > 0)
        {
            fprintf(stderr, "Server accepted new client. Socket: %d\n", clientSocketFd);

            //Obtain lock on clients list and add new client in
            pthread_mutex_lock(chatData->clientListMutex);
            if(chatData->numClients < MAX_CLIENT_NUM)
            {
                //Add new client to list
                for(int i = 0; i < MAX_CLIENT_NUM; i++)
                {
                    if(!FD_ISSET(chatData->clientSockets[i], &(chatData->serverReadFds)))
                    {
                        chatData->clientSockets[i] = clientSocketFd;
                        break;
                    }
                }

                FD_SET(clientSocketFd, &(chatData->serverReadFds));

                //Spawn new thread to handle client's messages
                clientHandlerVars chv;
                chv.clientSocketFd = clientSocketFd;
                chv.data = chatData;

                pthread_t clientThread;
                if((pthread_create(&clientThread, NULL, (void *)&clientHandler, (void *)&chv)) == 0)
                {
                    chatData->numClients++;
                    fprintf(stderr, "Client has joined chat. Socket: %d\n", clientSocketFd);

		    for(int i = 0; i < chatData->numClients; i++)
		    {
		        int *clientSockets = chatData->clientSockets;
			int socket = clientSockets[i];
			char *connectionMsg = "A new user joined the chat. Say hello!\n";
			char *welcomeMsg = "Welcome to the chat room!\n";
			if(socket == clientSocketFd){
			  if(socket != 0 && write(socket, welcomeMsg, MAX_TEXT_SIZE - 1) == -1)
			    perror("Socket write failed: ");
			}else{
			  if(socket != 0 && write(socket, connectionMsg, MAX_TEXT_SIZE - 1) == -1)
			    perror("Socket write failed: ");
			}
		    }
                }
                else
                    close(clientSocketFd);
            }
            pthread_mutex_unlock(chatData->clientListMutex);
        }
    }
}

void startChat(int socketFd)
{
    chatDataVars data;
    data.numClients = 0;
    data.socketFd = socketFd;
    data.queue = queueInit();
    data.clientListMutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(data.clientListMutex, NULL);

    //Start thread to handle new client connections
    pthread_t connectionThread;
    if((pthread_create(&connectionThread, NULL, (void *)&newClientHandler, (void *)&data)) == 0)
    {
        fprintf(stderr, "Connection handler started\n");
    }

    FD_ZERO(&(data.serverReadFds));
    FD_SET(socketFd, &(data.serverReadFds));

    //Start thread to handle messages received
    pthread_t messagesThread;
    if((pthread_create(&messagesThread, NULL, (void *)&messageHandler, (void *)&data)) == 0)
    {
        fprintf(stderr, "Message handler started\n");
    }

    pthread_join(connectionThread, NULL);
    pthread_join(messagesThread, NULL);
    
    queueDestroy(data.queue);
    pthread_mutex_destroy(data.clientListMutex);
    free(data.clientListMutex);
}

void queueDestroy(queue *q)
{
    pthread_mutex_destroy(q->mutex);
    pthread_cond_destroy(q->notFull);
    pthread_cond_destroy(q->notEmpty);
    free(q->mutex);
    free(q->notFull);
    free(q->notEmpty);
    free(q);
}

//Push to end of queue
void queuePush(queue *q, char* msg)
{
    q->buffer[q->tail] = msg;
    q->tail++;
    if(q->tail == MAX_TEXT_SIZE)
        q->tail = 0;
    if(q->tail == q->head)
        q->full = 1;
    q->empty = 0;
}

//Pop front of queue
char* queuePop(queue *q)
{
    char* msg = q->buffer[q->head];
    q->head++;
    if(q->head == MAX_TEXT_SIZE)
        q->head = 0;
    if(q->head == q->tail)
        q->empty = 1;
    q->full = 0;

    return msg;
}

void removeClient(chatDataVars *data, int clientSocketFd)
{
    pthread_mutex_lock(data->clientListMutex);
    for(int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        if(data->clientSockets[i] == clientSocketFd)
        {
            data->clientSockets[i] = 0;
            close(clientSocketFd);
            data->numClients--;
            break;
        }
    }
    pthread_mutex_unlock(data->clientListMutex);
}

//The "producer" -- Listens for messages from client to add to message queue
void *clientHandler(void *chv)
{
    clientHandlerVars *vars = (clientHandlerVars *)chv;
    chatDataVars *chatData = (chatDataVars *)vars->data;

    queue *q = chatData->queue;
    int clientSocketFd = vars->clientSocketFd;

    char msgBuffer[MAX_TEXT_SIZE];
    while(1)
    {
        int numBytesRead = read(clientSocketFd, msgBuffer, MAX_TEXT_SIZE - 1);
        msgBuffer[numBytesRead] = '\0';

        //If the client sent /exit\n, remove them from the client list and close their socket
        if(strcmp(msgBuffer, "/exit\n") == 0)
        {
            fprintf(stderr, "Client on socket %d has disconnected.\n", clientSocketFd);

	    for(int i = 0; i < chatData->numClients; i++)
		    {
		        int *clientSockets = chatData->clientSockets;
			int socket = clientSockets[i];
			char *leaveMsg = "A user has left the chat!\n";
			char *exitMsg = "You have left the chat!\n";
			if(socket == clientSocketFd){
			  if(socket != 0 && write(socket, exitMsg, MAX_TEXT_SIZE - 1) == -1)
			    perror("Socket write failed: ");
			}else{
			  if(socket != 0 && write(socket, leaveMsg, MAX_TEXT_SIZE - 1) == -1)
			    perror("Socket write failed: ");
			}
		    }
	    
            removeClient(chatData, clientSocketFd);
            return NULL;
        }
        else
        {
            //Wait for queue to not be full before pushing message
            while(q->full)
            {
                pthread_cond_wait(q->notFull, q->mutex);
            }

            //Obtain lock, push message to queue, unlock, set condition variable
            pthread_mutex_lock(q->mutex);
            queuePush(q, msgBuffer);
            pthread_mutex_unlock(q->mutex);
            pthread_cond_signal(q->notEmpty);
        }
    }
}

//The "consumer" -- waits for the queue to have messages then takes them out and broadcasts to clients
void *messageHandler(void *data)
{
    chatDataVars *chatData = (chatDataVars *)data;
    queue *q = chatData->queue;
    int *clientSockets = chatData->clientSockets;

    while(1)
    {     
        //Obtain lock and pop message from queue when not empty
        pthread_mutex_lock(q->mutex);
        while(q->empty)
        {
            pthread_cond_wait(q->notEmpty, q->mutex);
        }
        char* msg = queuePop(q);
        pthread_mutex_unlock(q->mutex);
        pthread_cond_signal(q->notFull);

        //Broadcast message to all connected clients
        for(int i = 0; i < chatData->numClients; i++)
        {
            int socket = clientSockets[i];
            if(socket != 0 && write(socket, msg, MAX_TEXT_SIZE - 1) == -1)
                perror("Socket write failed: ");
        }

    }
}

int main(int argc, char* argv[]){
  if(argc != 2){
    printf("Usage: ./server [PORT]\n");
    exit(1);
  }

  long port = strtol(argv[1], NULL, 0);
  int socketFd;
  struct sockaddr_in serverAddr;
  
  if((socketFd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
    printf("Socket creation failed! Exitting...\n");
    exit(2);
  }

  bindSocket(&serverAddr, socketFd, port);

  if(listen(socketFd, 1) == -1){
    printf("Socket listen failed\n");
    exit(3);
  }

  startChat(socketFd);

  close(socketFd);
  
  return 0;
}
