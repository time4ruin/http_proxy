#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>

#define BUFSIZE 100000

pthread_mutex_t mutex_lock;

void error(char *msg)
{
    perror(msg);
    exit(1);
}

void *threadfunc(void *arg);

void set_nonblock(int socket) {
    int flags;
    flags = fcntl(socket,F_GETFL,0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

struct sockaddr_in clientaddr[100];
int socket_id[100];
int socket_num = 0;
int b_option = 0;
int main(int argc, char **argv)
{
    int parentfd;
    int sockfd;
    int portno;
    int clientlen;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr_tmp;
    char buf[BUFSIZE];
    int optval;

    int th_id;
    pthread_t thread_t;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    if (argc != 2)
    {
        fprintf(stderr, "usage 1: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    parentfd = socket(AF_INET, SOCK_STREAM, 0);
    if (parentfd < 0)
        error("ERROR opening socket");

    optval = 1;
    setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    if (bind(parentfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    if (listen(parentfd, 5) < 0)
        error("ERROR on listen");

    clientlen = sizeof(struct sockaddr_in);
    pthread_mutex_init(&mutex_lock, NULL);
    while (1)
    {
        sockfd = accept(parentfd, (struct sockaddr *)&clientaddr_tmp, &clientlen);
        if (sockfd < 0)
            error("ERROR on accept");

        pthread_mutex_lock(&mutex_lock);  
        clientaddr[socket_num] = clientaddr_tmp;
        socket_id[socket_num] = sockfd;
        socket_num++;
        pthread_mutex_unlock(&mutex_lock);

        th_id = pthread_create(&thread_t, NULL, threadfunc, (void *)&sockfd);
        if (th_id != 0)
        {
            perror("Thread Create Error");
            return 1;
        }
        pthread_detach(thread_t);
    }
}

void *threadfunc(void *arg)
{
    int sockfd;
    int tmp_socket_num;
    int readn, writen;
    char buf[BUFSIZE];
    char tmp[BUFSIZE];
    struct hostent *hostp;
    char *hostaddrp;
    sockfd = *((int *)arg);
    int sockfd_index;
    
    struct hostent *server_p;
    int sockfd_p;
    struct sockaddr_in serveraddr_p;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    pthread_mutex_lock(&mutex_lock);   

    for (int i = 0; i < socket_num;i++){
        if (sockfd == socket_id[i]){
            sockfd_index = i; break;
        }
    }

    hostp = gethostbyaddr((const char *)&clientaddr[sockfd_index].sin_addr.s_addr,
                          sizeof(clientaddr[sockfd_index].sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
        error("ERROR on gethostbyaddr");
    hostaddrp = inet_ntoa(clientaddr[sockfd_index].sin_addr);
    if (hostaddrp == NULL)
        error("ERROR on inet_ntoa\n");

    pthread_mutex_unlock(&mutex_lock);   

    printf("Socket#%d: established connection with %s (%s)\n", sockfd, hostp->h_name, hostaddrp);
    //snprintf(buf, BUFSIZE, "Your ID: %d\n", sockfd);
    //n = write(sockfd, buf, strlen(buf));
    //set_nonblock(sockfd);//set non-blocking mode
    int n;
    while (1)
    {
        bzero(buf, BUFSIZE);
        n = read(sockfd, buf, BUFSIZE);
        if (n == 0){ //tcp rst, fin -> length 0
            break;
        } 

        printf("---------Proxy Request Received from %s (%s) Socket#%d---------\n", hostp->h_name, hostaddrp, sockfd);
        printf("%s", buf);
        
        int len = strlen(buf);
        char http_host[100];
        for (int i=0;i<len;i++){
            if (buf[i]=='H' && strncmp(buf+i, "Host: ", 6)==0){
                int j = i+6;
                int k = j;
                while(buf[j] != 0x0d || buf[j+1] != 0x0a){
                    http_host[j-k] = buf[j];
                    j++;
                }
                http_host[j]=0;
            }
        }
        //make new socket to connect to the server
        sockfd_p = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_p < 0)
            error("ERROR opening socket");
        server_p = gethostbyname(http_host);
        if (server_p == NULL)
        {
            fprintf(stderr, "ERROR, no such host as %s\n", http_host);
            exit(0);
        }

        bzero((char *)&serveraddr_p, sizeof(serveraddr_p));
        serveraddr_p.sin_family = AF_INET;
        bcopy((char *)server_p->h_addr, (char *)&serveraddr_p.sin_addr.s_addr, server_p->h_length);
        serveraddr_p.sin_port = htons(80); //http port:80

        if (connect(sockfd_p, &serveraddr_p, sizeof(serveraddr_p)) < 0)
            error("ERROR connecting");
        printf("Proxy Connecting to %s...\n", http_host);

        n = write(sockfd_p, buf, strlen(buf)); //relay http request
        if (n < 0)
            error("ERROR writing to socket");

        n = read(sockfd_p, buf, BUFSIZE); //get http reply

        n = write(sockfd, buf, strlen(buf)); //relay http request
        if (n < 0)
            error("ERROR writing to socket");
        printf("----------Proxy Reply Successfully Sent to Socket#%d----------\n", sockfd);
        
    }

    pthread_mutex_lock(&mutex_lock);
    for (int i=0;i<socket_num;i++){
        if (socket_id[i]==sockfd){
            tmp_socket_num=i;
            break;
        }
    }
    for (int j=tmp_socket_num;j<socket_num-1;j++){
        socket_id[j] = socket_id[j+1];
    }
    socket_id[socket_num--] = 0;
    pthread_mutex_unlock(&mutex_lock);
    
    printf("Socket#%d: server closed connection with %s (%s)\n", sockfd, hostp->h_name, hostaddrp);
    close(sockfd);
}