#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h> 
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

typedef struct{
    char filename [512];
    mode_t mode;
    char content [4096];
    int readSize;
    int flag;   //0 normal 1 last part of file 2 client cycle done
    time_t lastModification;
} entry;

void handler(int signo);    //signal handler
void latency(double);       //delay function
void sendClientDir(int,char*);  //sends client directory
void sendClientFiles(int,char*);    //sends each client file in clint directory
void createClientDir(int fd, char * name); //create clien directory in server side

int doneFlag = 0;
int clientFD;                       //client socket
int baseIndex;                      //base index of client path

int main(int argc, char **argv){
    struct sockaddr_in serverAddress;   //server address
    socklen_t serverLen;                //server address lenght
    char  clientPath[512],clientBase[512];

    if(argc != 4){
        printf("Invalid usage! It must be %s [clientDirPath] [server ip] [portnumber]\n",argv[0] );
        exit(EXIT_FAILURE);
    }
    int portnumber = atoi(argv[3]);
    DIR *pDir = opendir (argv[1]);
    if(portnumber>2000 && pDir != NULL){    //port must greater 2000 for linux
        closedir(pDir);
    }
    else{
        printf("Invalid usage! It must be %s [serverDirPath] [threadPoolSize] [portnumber]\n",argv[0] );
        exit(EXIT_FAILURE);
    }

    signal(SIGINT,handler);
    signal(SIGTERM,handler);

    //path name parse
    strcpy(clientBase,argv[1]);     
    strcpy(clientPath,dirname(argv[1]));
    if(strcmp(clientPath,".") == 0)
        strcpy(clientPath,"");
    else
        strcat(clientPath,"/");

    strcat(clientPath,basename(clientBase));
    baseIndex=strlen(argv[1]);
    ++baseIndex;

    clientFD = socket(AF_INET,SOCK_STREAM,0);   //IPv4 stream socket
    if(clientFD == -1){ 
        perror("socket : ");
        exit(EXIT_FAILURE);
    }

    serverLen = sizeof(serverAddress);
    memset(&serverAddress,0,serverLen);

    serverAddress.sin_family = AF_INET;                   //IPv4
    serverAddress.sin_addr.s_addr = inet_addr(argv[2]);   //IP
    serverAddress.sin_port = htons(portnumber);           //portnumber

    if (connect(clientFD, (struct sockaddr*)&serverAddress,serverLen) == -1) { 
        perror("connect : "); 
        close(clientFD);
        exit(EXIT_FAILURE); 
    } 

    createClientDir(clientFD,clientPath);
    while(doneFlag == 0){
        sendClientDir(clientFD,clientPath);
        latency(1000);  //send data every 1 second
    }
    
    close(clientFD);
    return 0;
}

void createClientDir(int fd, char * name){
    char message[10];
    struct stat fileStat;
    lstat(name,&fileStat);
    char tempName[512];
    strcpy(tempName,name);
    entry each;
    memset(&each, 0, sizeof each);  //for valgrind uninitialized value
    strcpy(each.filename,basename(tempName));
    each.mode = fileStat.st_mode;
    each.flag = 0;

    write(fd,&each,sizeof(entry));
    read(fd, message, sizeof(message));     //response message from server

    if(strcmp(message,"error")==0){
        printf("More than one clients which have same name cannot ");
        printf("connect to the server at the same time!\n");
        close(fd);
        close(clientFD);
        exit(EXIT_FAILURE);
    }
    if(strcmp(message,"shutdown")==0){
        printf("Server shutted down.\n");
        close(fd);
        close(clientFD);
        exit(EXIT_FAILURE);
    }
}

void sendClientDir(int fd ,char* name){
    char message[10];
    sendClientFiles(fd,name);
    entry each;
    memset(&each, 0, sizeof each);
    each.flag = 2;
    write(fd,&each,sizeof(entry));
    read(fd, message, sizeof(message));     //response message from server
}

void sendClientFiles(int fd , char* param){
    
    struct dirent *pDirent;
    DIR *pDir;
    struct stat fileStat;

    char name [512], message[10]; 
    int size, rSize; 

    pDir = opendir (param);
    if (pDir != NULL){
        while ((pDirent = readdir(pDir)) != NULL){
            if(0!=strcmp(pDirent->d_name,".") && 0!=strcmp(pDirent->d_name,"..")){
                strcpy(name,param);
                strcat(name,"/");
                strcat(name,pDirent->d_name);
  
                lstat(name,&fileStat);
                if((S_ISDIR(fileStat.st_mode))){
                    entry each;
                    memset(&each, 0, sizeof each);
                    memset(each.filename, '\0', sizeof(each.filename));
                    memcpy(each.filename, &name[baseIndex], strlen(name)-baseIndex);
                    each.mode = fileStat.st_mode;

                    write(fd,&each,sizeof(entry));
                    read(fd, message, sizeof(message));     //response message from server
                    if(strcmp(message,"shutdown")==0){
                        printf("Server shutted down.\n");
                        closedir(pDir);
                        close(fd);
                        exit(EXIT_FAILURE);
                    }
                    strcpy(message,"");   //clear message
                    sendClientFiles(fd,name);
                }

                else {
                    int rfd;
                    if(S_ISREG(fileStat.st_mode)){
                        rfd = open(name, O_RDONLY);
                    }
                    else if(S_ISFIFO(fileStat.st_mode)){
                        rfd = open(name, O_RDONLY | O_NONBLOCK);
                    }
                    else {
                        continue;
                    }
                    if(rfd == -1){
                        continue;
                    }

                    entry each;
                    memset(&each, 0, sizeof each);
                    size = (int)fileStat.st_size;
                    memset(each.filename, '\0', sizeof(each.filename));
                    memcpy(each.filename, &name[baseIndex], strlen(name)-baseIndex);
                    each.mode = fileStat.st_mode;
                    each.flag = 0;
                    each.lastModification = fileStat.st_mtime;

                    do{
                        rSize = read(rfd,each.content,sizeof(each.content));
                        if(rSize<0)
                            break;
                        size -= sizeof(each.content);
                        if(size <= 0)
                            each.flag = 1;      //this file done

                        each.readSize = rSize;
                        write(fd,&each,sizeof(entry));
                        read(fd, message, sizeof(message));     //respons message from server
                        strcpy(each.content,"");    //clear content

                        if(strcmp(message,"shutdown")==0){
                            printf("Server shutted down.\n");
                            closedir(pDir);
                            close(fd);
                            exit(EXIT_FAILURE);
                        }
                        strcpy(message,"");   //clear message
                    }while(rSize > 0);
                    close(rfd);
                }
            }
        }
    }

    closedir(pDir);
}

void latency(double msec){
    double dif = 0.0;
    struct timeval start, end;

    gettimeofday(&start, NULL);
    while(dif <= msec){
        gettimeofday(&end, NULL);
        dif = (double) (end.tv_usec - start.tv_usec) / 1000 + (double) (end.tv_sec - start.tv_sec)*1000;
    } 
}


void handler(int signo){
    if(signo == SIGINT)
        printf("SIGINT handled.\n");
    else if(signo == SIGTERM)
        printf("SIGTERM handled.\n");
    
    doneFlag=1;
}