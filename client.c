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
#include <signal.h>
#include <time.h>

#define MAX_TEXT_SIZE 1024
#define MAX_CHAT_HISTORY 512

void chatloop(char *name, int socketFd);
void buildMessage(char *result, char *name, char *msg);
void setupAndConnect(struct sockaddr_in *serverAddr, struct hostent *host, int socketFd, long port);
void setNonBlock(int fd);
void interruptHandler(int sig);
int validate_credentials(char *username, char *password);

void printChatHistory();

static int socketFd;
char *chatHistory[MAX_CHAT_HISTORY];
unsigned int currentMessageCount = 0;

int main(int argc, char *argv[])
{
    char *name;
    struct sockaddr_in serverAddr;
    struct hostent *host;
    long port;

    if(argc != 4)
    {
        fprintf(stderr, "./client [username] [password] [port]\n");
        exit(1);
    }
    name = argv[1];
    if((host = gethostbyname("localhost")) == NULL)
    {
        fprintf(stderr, "Couldn't get host name\n");
        exit(1);
    }
    port = strtol(argv[3], NULL, 0);
    if((socketFd = socket(AF_INET, SOCK_STREAM, 0))== -1)
    {
        fprintf(stderr, "Couldn't create socket\n");
        exit(1);
    }

    if (!valid_credentials(argv[1], argv[2]))
    {
        printf("Username or password is incorrect, please try again\n");
        exit(1);
    }
    
    setupAndConnect(&serverAddr, host, socketFd, port);
    setNonBlock(socketFd);
    setNonBlock(0);

    //Set a handler for the interrupt signal
    signal(SIGINT, interruptHandler);

    chatloop(name, socketFd);
}

//Main loop to take in chat input and display output
void chatloop(char *name, int socketFd)
{
    fd_set clientFds;
    char chatMsg[MAX_TEXT_SIZE];
    char chatBuffer[MAX_TEXT_SIZE], msgBuffer[MAX_TEXT_SIZE];

    while(1)
    {
        //Reset the fd set each time since select() modifies it
        FD_ZERO(&clientFds);
        FD_SET(socketFd, &clientFds);
        FD_SET(0, &clientFds);
        if(select(FD_SETSIZE, &clientFds, NULL, NULL, NULL) != -1) //wait for an available fd
        {
            for(int fd = 0; fd < FD_SETSIZE; fd++)
            {
                if(FD_ISSET(fd, &clientFds))
                {
                    if(fd == socketFd) //receive data from server
                    {
		      system("clear");
                        int numBytesRead = read(socketFd, msgBuffer, MAX_TEXT_SIZE - 1);
                        msgBuffer[numBytesRead] = '\0';
			chatHistory[currentMessageCount] = malloc(sizeof(msgBuffer));
			strncpy(chatHistory[currentMessageCount], msgBuffer, sizeof(msgBuffer));
			currentMessageCount++;
			//printf("%s", msgBuffer);
			printChatHistory();
                        memset(&msgBuffer, 0, sizeof(msgBuffer));
			printf("_________________________________\n");
                    }
                    else if(fd == 0) //read from keyboard (stdin) and send to server
                    {
                        fgets(chatBuffer, MAX_TEXT_SIZE - 1, stdin);
                        if(strcmp(chatBuffer, "/exit\n") == 0)
                            interruptHandler(-1); //Reuse the interruptHandler function to disconnect the client
                        else
                        {
                            buildMessage(chatMsg, name, chatBuffer);
                            if(write(socketFd, chatMsg, MAX_TEXT_SIZE - 1) == -1) perror("write failed: ");
                            //printf("%s", chatMsg);
                            memset(&chatBuffer, 0, sizeof(chatBuffer));
                        }
                    }
                }
            }
        }
    }
}

//Concatenates the name with the message and puts it into result
void buildMessage(char *result, char *name, char *msg)
{
    time_t t = time(NULL);
    struct tm *tmp = gmtime(&t);
    char *timestamp = malloc(12);
    //printf("hours is %d\n", tmp->tm_hour);
    sprintf(timestamp, "[%02d:%02d:%02d] ", tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
    memset(result, 0, MAX_TEXT_SIZE);
    strcpy(result, timestamp);
    strcat(result, name);
    strcat(result, ": ");
    strcat(result, msg);
}

//Sets up the socket and connects
void setupAndConnect(struct sockaddr_in *serverAddr, struct hostent *host, int socketFd, long port)
{
    memset(serverAddr, 0, sizeof(serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_addr = *((struct in_addr *)host->h_addr_list[0]);
    serverAddr->sin_port = htons(port);
    if(connect(socketFd, (struct sockaddr *) serverAddr, sizeof(struct sockaddr)) < 0)
    {
        perror("Couldn't connect to server");
        exit(1);
    }
}

//Sets the fd to nonblocking
void setNonBlock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if(flags < 0)
        perror("fcntl failed");

    flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
}

//Notify the server when the client exits by sending "/exit"
void interruptHandler(int sig_unused)
{
    if(write(socketFd, "/exit\n", MAX_TEXT_SIZE - 1) == -1)
        perror("write failed: ");

    close(socketFd);
    exit(1);
}

void printChatHistory(){
  int i;
  if(currentMessageCount > 511){
    for(i = 0; i < currentMessageCount - 100; i++)
      strcpy(chatHistory[i], chatHistory[i + 100]);
    currentMessageCount -= 100;
  }
  for(i = 0; i < currentMessageCount; i++){
    puts(chatHistory[i]);
  }
}

int valid_credentials(char *username, char *password)
{
    FILE *fp;
	char temp[50];
	
	if((fp = fopen("Users.txt", "r")) == NULL) {
	    fprintf(stderr, "Error accessing user list\n");
		return 0;
	}
	
	char *to_find = malloc(strlen(username)+strlen(password) + 2);
	sprintf(to_find, "%s %s", username, password);

	while(fgets(temp, 50, fp) != NULL) {
		if((strstr(temp, to_find)) != NULL) {
			return 1;
		}
	}
	
	if(fp) {
		fclose(fp);
	}
   	return 0;
}
