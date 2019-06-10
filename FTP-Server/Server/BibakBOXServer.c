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
    int flag;	//0 normal 1 last part of file 2 client cycle done
    time_t lastModification;
}entry;

typedef struct {
	char fname[512];
} touched;


void latency(double msec);		//delay function
void timeInfo(char *buf);		//gets current time and date
void writeLog(char * logFile, char *type, char * status,char * name);	//writes log file
void removeRest(touched *list,int count, char* path , char* logFile); 	//removes rest files in server
int isInclude(char *name, touched *list, int count);	//check given name is in touched list
void * serve(void*);		//thread function
void threadErr(int,char*);	//thread error function
void handler(int);			//signal handler for SIGINT
	
int *clientFD;		//clients socket fd array (every thread know own index; no mutual exclusion)
pthread_t *serverThreads;  	// thread pool
int threadPoolSize;			// thread pool size
int serverFD;				//server socket
char *serverPath;			//well-known server path
touched * onlineList;		//current online clients' list
int onlineCount=0;			//online client count
pthread_mutex_t onlineMutex = PTHREAD_MUTEX_INITIALIZER; 	//onlineList mutex
pthread_t mainThread;		//main thread id


int main(int argc, char **argv){
	struct sockaddr_in serverAddress;	//server adress
	socklen_t serverLen;				//server adress lenght

	if(argc != 4){
		printf("Invalid usage! It must be %s [serverDirPath] [threadPoolSize] [portnumber]\n",argv[0] );
		exit(EXIT_FAILURE);
	}

	threadPoolSize = atoi(argv[2]);
	int portnumber = atoi(argv[3]);
	DIR *pDir = opendir (argv[1]);
	if(threadPoolSize>0 && portnumber>2000 && pDir != NULL){	//port number must greater then 2000 for linux standart
		closedir(pDir);
	}
	else{
		printf("Invalid usage! It must be %s [serverDirPath] [threadPoolSize] [portnumber]\n",argv[0] );
		exit(EXIT_FAILURE);
	}

	mainThread = pthread_self();

	signal(SIGINT,handler);
	signal(SIGTERM,handler);

	serverPath = (char*)malloc(512*sizeof(char)); 	
	char * serverBase = (char*)malloc(512*sizeof(char));

	strcpy(serverBase,argv[1]);		

	strcpy(serverPath,dirname(argv[1]));
	if(strcmp(serverPath,".") == 0)
		strcpy(serverPath,"");
	else
		strcat(serverPath,"/");

	strcat(serverPath,basename(serverBase));
	free(serverBase);

	memset(&serverAddress,0,sizeof(struct sockaddr_in));	// clear server adress
	serverAddress.sin_family = AF_INET;						// IPv4
	serverAddress.sin_port = htons(portnumber);				//portnumber

	serverLen = sizeof(serverAddress);

	if((serverFD = socket(AF_INET,SOCK_STREAM,0)) == -1){	//IPv4 stream socket
		perror("socket : ");
		exit(EXIT_FAILURE);
	}

	if(bind(serverFD,(struct sockaddr*)&serverAddress,serverLen) == -1){
		perror("bind : ");
		exit(EXIT_FAILURE);
	}

	if(listen(serverFD,0) == -1){	//queue size 0
		perror("listen : ");
		exit(EXIT_FAILURE);
	}

	onlineList = (touched*)malloc(4096*sizeof(touched));

	int *numbers = (int*)malloc(threadPoolSize*sizeof(int));
	for (int i = 0; i < threadPoolSize; ++i){
		numbers[i]=i;
	}

	clientFD = (int*)malloc(threadPoolSize*sizeof(int));
	serverThreads = (pthread_t *)malloc(threadPoolSize * sizeof(pthread_t)); 		//thread pool size
	for (int i = 0; i < threadPoolSize; ++i){
		clientFD[i] = -1;
		int err = pthread_create(&serverThreads[i], NULL, serve, (void*)&numbers[i]);	//with no argument
		if(err) threadErr(err,"consumer thread creation");
	}

	for (int i = 0; i < threadPoolSize; ++i){
    	int err = pthread_join (serverThreads[i], NULL);	
        if(err) threadErr(err,"consumer thread join");
    }

    free(numbers);
    free(clientFD);
    free(serverThreads);
    free(serverPath);
    free(onlineList);
    close(serverFD);	
	return 0;
}

void writeLog(char * logFile, char *type, char * status,char * name){
	int log;
	char buf[50];			
	timeInfo(buf);

	char * logInfo = (char*)malloc(512*sizeof(char));
	sprintf(logInfo, "%s\t %s\t %s \t\t %s\n", type, status ,buf ,name );	
	log = open(logFile, O_WRONLY | O_APPEND, 0666);

	write(log, logInfo, strlen(logInfo));
	close(log);
	free(logInfo);
}
void removeRest(touched *list,int count, char* path , char* logFile){
	struct dirent *pDirent;
    DIR *pDir;
    struct stat fileStat;
    char name [512];        

    pDir = opendir (path);
    if (pDir != NULL){ 
        while ((pDirent = readdir(pDir)) != NULL){
            if(0!=strcmp(pDirent->d_name,".") && 0!=strcmp(pDirent->d_name,"..")){
                strcpy(name,path);
                strcat(name,"/");
                strcat(name,pDirent->d_name);

                if(strcmp(name,logFile)==0)
                	continue;
                
                lstat(name,&fileStat);
	            if(S_ISDIR(fileStat.st_mode)){
	            	removeRest(list,count,name,logFile);
	            	
	            	if(!isInclude(name,list,count)){	//remove dir
	            		if(remove(name) != -1){
		    				writeLog(logFile,"Directory","REMOVED",name);	
	            		}
	            	}
	            }
	            else if(!isInclude(name,list,count)){
                	remove(name);	//remove file
                	writeLog(logFile,"File    ","REMOVED",name);
	            }
            }
        }
    }

    closedir(pDir);
}

int isInclude(char *name, touched *list, int count){
	for (int i = 0; i < count; ++i)
		if(strcmp(list[i].fname,name)==0)
			return 1;
	
	return 0;
}

int online(char * name){
	for (int i = 0; i < onlineCount; ++i){
		pthread_mutex_lock(&onlineMutex);
		if(strcmp(onlineList[i].fname,name) == 0){
			pthread_mutex_unlock(&onlineMutex);
			return 1;
		}
		pthread_mutex_unlock(&onlineMutex);
	}
	return 0;
}

void removeFromOnline(char * name){
	for (int i = 0; i < onlineCount; ++i){
		pthread_mutex_lock(&onlineMutex);
		if(strcmp(onlineList[i].fname,name) == 0)
			strcpy(onlineList[i].fname,"");
		pthread_mutex_unlock(&onlineMutex);
	}
}

void * serve(void* p){
	struct sockaddr_in clientAddress;	//client adress
	socklen_t clientLen;				//client adress length
	int clientFdIndex = *(int*) p;		//client socket index
	int wfd = -1 , log = -1;			//write fd and log fd
	struct stat fileStat;
	char logFileName[512],clientDirName[512],buf[50], message[10];;
	int touchedCount=0;
	char * logInfo;
	entry each;	

	memset(&clientAddress,0,sizeof(struct sockaddr_in));	// clear client adress
	clientLen = sizeof(clientAddress);

	for(;;){
		clientFD[clientFdIndex] = accept(serverFD, (struct sockaddr*)&clientAddress, &clientLen);
		if (clientFD[clientFdIndex] == -1) { 
			perror("accept : "); 
			exit(EXIT_FAILURE); 
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	//lock

	    read(clientFD[clientFdIndex], &each, sizeof(entry));		//read client dir name

    	if(online(each.filename)){
    		bzero(message,sizeof(message));
    		strcpy(message,"error");
    		write(clientFD[clientFdIndex], message, sizeof(message));	//response
    		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);	//unlock
    		continue;
    	}	
    
    	strcpy(onlineList[onlineCount++].fname,each.filename);	//add online list (array bound clear)

    	char str[clientLen];
		inet_ntop( AF_INET, &clientAddress, str, clientLen );
		printf("Client connected with ip: %s\n",str ); 

    	bzero(message,sizeof(message));
    	strcpy(message,"go on");
    	write(clientFD[clientFdIndex], message, sizeof(message));		//response

    	strcpy(clientDirName,serverPath);		
    	strcat(clientDirName,"/");
    	strcat(clientDirName,each.filename);		//server/client 	(client dir)
    	mkdir(clientDirName,0700);					//create client directory in server

    	strcpy(logFileName,clientDirName);			//server/client 	(log file)
    	strcat(logFileName,"/");		
    	strcat(logFileName,each.filename);
    	strcat(logFileName,".log");					//server/client/client.log	
    
    	//connect log
    	log = open(logFileName, O_WRONLY | O_CREAT | O_APPEND, 0666);	//create log file
    	timeInfo(buf);		//time
  		logInfo = (char*)malloc(512*sizeof(char));
    	sprintf(logInfo, "Client %s connected. \t %s\n",each.filename,buf );	
    	write(log, logInfo, strlen(logInfo));
    	close(log);
    	free(logInfo);   

    	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);	//unlock

    	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	//lock
	    touched * touchedList = (touched*)malloc(1024*sizeof(touched)); 	
	    
	    while(read(clientFD[clientFdIndex], &each, sizeof(entry))>0){		//read client dir 4Kb by 4Kb until disconnected

	    	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	    	if(each.flag == 2){	//remove rest files each client cycle
	    		removeRest(touchedList,touchedCount,clientDirName,logFileName);
	    		touchedCount=0;
	    		bzero(message,sizeof(message));
	    		strcpy(message,"go on");
	    		write(clientFD[clientFdIndex], message, sizeof(message));		//response
	    		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	    		continue;
	    	}

	    	char *name = (char *) malloc(512*sizeof(char));
	    	
	    	strcpy(name,serverPath);
	    	strcat(name,"/");
	    	strcat(name,each.filename);

	    	if(S_ISDIR(each.mode)){
	    		if(!isInclude(name,touchedList,touchedCount))
	    			strcpy(touchedList[touchedCount++].fname,name);
	    		
	    		if(mkdir(name,0700) != -1){
	    			writeLog(logFileName,"Directory","ADDED  ",name);
	    		}
	    	}
	    	else{
	    		if(lstat(name,&fileStat) != -1){		//file exist
					if(wfd == -1){		//open only once
						if(!isInclude(name,touchedList,touchedCount))
							strcpy(touchedList[touchedCount++].fname,name);

						if(each.lastModification > fileStat.st_mtime){	//file modified
							wfd = open(name, O_WRONLY | O_TRUNC, each.mode);
							
		    				writeLog(logFileName,"File    ","UPDATED",name);
						}
						else {			//nothing changed
							free(name);
							bzero(message,sizeof(message));
							strcpy(message,"go on");
							write(clientFD[clientFdIndex], message, sizeof(message));		//response
							pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
							continue;
						}
					}
					write(wfd,each.content,each.readSize);
    				
	    		}
	    		else{	//file not exist
	    			if(wfd == -1){		//open only once
	    				if(!isInclude(name,touchedList,touchedCount))
	    					strcpy(touchedList[touchedCount++].fname,name);

	    				wfd = open(name, O_WRONLY | O_CREAT | O_TRUNC, each.mode);
	    				
	    				writeLog(logFileName,"File    ","ADDED  ",name);

	    			}
	    			write(wfd,each.content,each.readSize);
	    		}
	    	}

	    	if(each.flag == 1){	//file done
	    		if(!S_ISDIR(each.mode)){
		    		close(wfd);		//clise file
		    		wfd = -1;		//reset wfd
		    	}
	    	}
	    	free(name);
	    	bzero(message,sizeof(message));
	    	strcpy(message,"go on");
	    	write(clientFD[clientFdIndex], message, sizeof(message));		//response
	    	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);	//unlock
	    }

	    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	//lock


	    //disconnect log
	    log = open(logFileName, O_WRONLY | O_APPEND, 0666);	
	    timeInfo(buf);
	    logInfo = (char*)malloc(512*sizeof(char));
	    sprintf(logInfo, "Client %s disconnected. \t %s\n", basename(dirname(logFileName)),buf );
	    write(log, logInfo, strlen(logInfo));
	    close(log);
	    free(logInfo);
	    free(touchedList);

	    removeFromOnline(basename(clientDirName));

	    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);	//unlock

	}

	return NULL;
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

void timeInfo(char *buf){
	strcpy(buf,"");
	time_t now = time(0);
	struct tm now_t = *localtime(&now);
	strftime (buf, 50, "%x - %I:%M:%S %p", &now_t);
}

void threadErr(int err,char* msg){
    errno = err;
    perror(msg);
    exit(EXIT_FAILURE);
}

void handler(int signo){
	if(pthread_self() == mainThread){	//only main thread execute signal handler
		char message[10];
		if(signo == SIGINT)
			printf("SIGINT handled.\n");
		else if(signo == SIGTERM)
			printf("SIGTERM handled.\n");
		
		bzero(message,sizeof(message));
		strcpy(message,"shutdown");

		for (int i = 0; i < threadPoolSize; ++i){
			if(clientFD[i]!=-1){
				write(clientFD[i], message, sizeof(message));	
			}
		}

		latency(1000);

		for (int i = 0; i < threadPoolSize; ++i){
			pthread_cancel(serverThreads[i]);	
		}
	}
}
