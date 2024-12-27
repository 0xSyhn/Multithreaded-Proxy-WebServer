#include "proxy_parse.h"
#include<iostream>
#include<time.h>
#include<vector>
#include<pthread.h>
#include<semaphore.h>
#include<sys/socket.h>               //contains socket fn and constants like AF_INET, AF_INET6, etc
#include <sys/types.h>
#include<netinet/in.h>               //for specifying ip versions. provides sockaddr_in
#include<unistd.h>                   //provides close() to close sockets
#include <arpa/inet.h>
#include<netdb.h>

#define MAX_CLIENTS 400
#define BUFFER_SIZE 4096
#define MAX_ELEMENT_SIZE 10*(1<<10)
#define MAX_SIZE 200*(1<<20)
using namespace std;

struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_lapse;
    cache_element *next;
};

cache_element* find(string url);
int addElement(char* data, int size, string url);
void deleteElement();

int port_number = 8080;             //default port to communicate with the server
int proxy_socketId;       
vector<pthread_t>tid(MAX_CLIENTS);              //array to store the thread ids of the client
pthread_mutex_t mutex_lock;        //to manage resources for the clients who are requesting concurrently
sem_t semaphore;                   //makes a thread wait until tid has a free slot

cache_element* head;
int cache_size;

cache_element* find(char* url){
    cache_element *curr = NULL;
    int tempLock = pthread_mutex_lock(&mutex_lock);
    cout<<"Remove Cache Lock Acquired "<<tempLock<<endl;
    if(head!=NULL){
        curr = head;
        while(curr!=NULL){
            if(!strcmp(curr->url, url)){
                cout<<"LRU time track before: "<<curr->lru_time_lapse<<endl;
                cout<<"Url was Found"<<endl;
                curr->lru_time_lapse = time(NULL);
                cout<<"LRU time track after: "<<curr->lru_time_lapse<<endl;
                break;
            }
            curr = curr->next;
        } 
    }
    else{
        cout<<"Url Not Found"<<endl;
    }
    tempLock = pthread_mutex_unlock(&mutex_lock);
    cout<<"Remove Cache Lock Unlocked"<<endl;
    return curr;
}

int addElement(char* data, int size, char* url){
    int tempLock = pthread_mutex_lock(&mutex_lock);
    cout<<"Add Cache Lock Acquired "<<tempLock<<endl;
    int element_size = size+1+strlen(url)+sizeof(cache_element);
    if(element_size>MAX_ELEMENT_SIZE){
        tempLock = pthread_mutex_unlock(&mutex_lock);
        cout<<"Add Cache Lock is Unlocked"<<endl;
        return 0;
    }
    else{
        while(cache_size+element_size>MAX_SIZE){
            deleteElement();
        }
        cache_element *element = new cache_element;
        element->data = strdup(data);
        element->url = strdup(url);
        element->lru_time_lapse = time(NULL);
        element->next = head;
        element->len = size;
        head = element;
        cache_size+=element_size;
        tempLock = pthread_mutex_unlock(&mutex_lock);
        cout<<"Add Cache Lock is Unlocked"<<endl;
        return 1;
    }
    return 0;
}

void deleteElement(){
    cache_element *prev;
    cache_element *temp;
    cache_element *curr;

    int tempLock = pthread_mutex_lock(&mutex_lock);
    cout<<"Lock Aqcuired";
    if(head!=NULL){
        prev = head, temp = head, curr = head;
        while(curr->next!=NULL){
            if((curr->next)->lru_time_lapse<(temp->lru_time_lapse)){
                temp = curr->next;
                prev = curr;
            }
            curr = curr->next;  
        }
        if(temp == head) head = head->next;
        else prev->next = temp->next;

        cache_size = cache_size-(temp->len)-sizeof(cache_element)-strlen(temp->url)-1;
        delete temp->data;
        delete temp->url;
        delete temp;
    }
    tempLock = pthread_mutex_unlock(&mutex_lock);
    cout<<"Remove Cache Lock Unlocked "<<tempLock<<endl;
}

int checkHTTPversion(char *msg)
{
	int version = -1;
	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										
	}
	else
		version = -1;

	return version;
}


int sendError(int socket, int statusCode) {
    char str[1024];
    char currentTime[50];
    time_t now = time(0);
    struct tm tm = *gmtime(&now); 

    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S GMT", &tm);

    switch (statusCode) {
        case 400:
            cout<<"400 Bad Request"<<endl;
            break;

        case 403:
            cout<<"403 Forbidden"<<endl;
            break;

        case 404:
            cout<< "404 Not Found"<<endl;
            send(socket, str, strlen(str), 0);
            break;

        case 500:
            cout<<"500 Internal Server Error"<<endl;
            send(socket, str, strlen(str), 0);
            break;

        case 501:
            cout<<"501 Not Implemented"<<endl;
            send(socket, str, strlen(str), 0);
            break;

        case 505:
            cout<<"505 HTTP Version Not Supported"<<endl;
            send(socket, str, strlen(str), 0);
            break;

        default:
            cerr << "Invalid status code: " << statusCode << endl;
            return -1;
    }
    return 1;
}


int connectRemoteServer(char *host_addr, int port) {
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0) {
        cerr << "Error in creating socket" << endl;
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);
    if(host == NULL) {
        cout << "No such host exists" << endl;
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr, host->h_length);

    if(connect(remoteSocket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Error in connecting to remote server" << endl;
        close(remoteSocket);
        return -1;
    }
    return remoteSocket;
}

int handle_request(int client_socketId, ParsedRequest *req, char *tempReq) {
    char *buffer = new char[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        req->path,
        req->host
    );

    size_t len = strlen(buffer);

    if(ParsedHeader_set(req, "Connection", "close") < 0) {
        cout << "Set header key is not working" << endl;
    }

    if(ParsedHeader_get(req, "Host") == NULL) {
        if(ParsedHeader_set(req, "Host", req->host) < 0) {
            cout << "Set Host header key is not working" << endl;
        }
    }

    if(ParsedRequest_unparse_headers(req, buffer + len, (size_t)BUFFER_SIZE - len) < 0) {
        cout << "Unparse failed" << endl;
    }

    int serverPort = 80;
    if(req->port != NULL) {
        serverPort = atoi(req->port);
    }

    int remoteSocketId = connectRemoteServer(req->host, serverPort);
    if(remoteSocketId < 0) return -1;

    int bytes_send = send(remoteSocketId, buffer, strlen(buffer), 0);
    memset(buffer, 0, BUFFER_SIZE);
    bytes_send = recv(remoteSocketId, buffer, BUFFER_SIZE - 1, 0);

    // Improved buffer handling
    char *tempBuffer = new char[BUFFER_SIZE];
    int tempBufferSize = BUFFER_SIZE;
    int tempBufferIdx = 0;

    while(bytes_send > 0) {
        if(send(client_socketId, buffer, bytes_send, 0) < 0) {
            cerr << "Error sending to client" << endl;
            delete[] buffer;
            delete[] tempBuffer;
            close(remoteSocketId);
            return -1;
        }

        // Safe buffer copying
        if(tempBufferIdx + bytes_send < tempBufferSize) {
            memcpy(tempBuffer + tempBufferIdx, buffer, bytes_send);
            tempBufferIdx += bytes_send;
        } else {
            int newSize = tempBufferSize * 2;
            char *newBuffer = new char[newSize];
            memcpy(newBuffer, tempBuffer, tempBufferIdx);
            delete[] tempBuffer;
            tempBuffer = newBuffer;
            tempBufferSize = newSize;
            
            memcpy(tempBuffer + tempBufferIdx, buffer, bytes_send);
            tempBufferIdx += bytes_send;
        }

        memset(buffer, 0, BUFFER_SIZE);
        bytes_send = recv(remoteSocketId, buffer, BUFFER_SIZE - 1, 0);
    }

    tempBuffer[tempBufferIdx] = '\0';
    delete[] buffer;
    addElement(tempBuffer, tempBufferIdx, tempReq);
    delete[] tempBuffer;
    close(remoteSocketId);
    return 0;
}

void *threadFn(void *socketNew) {
    sem_wait(&semaphore);
    int sem;
    sem_getvalue(&semaphore, &sem);
    cout << "Semaphore value: " << sem << endl;

    int *t = (int*)(socketNew);
    int socket = *t;

    char *buffer = new char[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    int total_bytes = 0;
    int bytes_recv;
    
    while ((bytes_recv = recv(socket, buffer + total_bytes, BUFFER_SIZE - total_bytes, 0)) > 0) {
        total_bytes += bytes_recv;
        buffer[total_bytes] = '\0';
        
        // Check if we've received the complete HTTP request
        if (strstr(buffer, "\r\n\r\n") != NULL) {
            break;
        }
        
        // Prevent buffer overflow
        if (total_bytes >= BUFFER_SIZE - 1) {
            sendError(socket, 413); 
            delete[] buffer;
            sem_post(&semaphore);
            return NULL;
        }
    }

    char *tempReq = new char[strlen(buffer)];
    strcpy(tempReq, buffer);

    struct cache_element *temp = find(tempReq);
    if(temp != NULL) {
        int bytes_sent = send(socket, temp->data, temp->len, 0);
        if(bytes_sent < 0) {
            cerr << "Error sending cached data" << endl;
        }
        cout << "Data retrieved from Cache" << endl;
    } else if(bytes_recv > 0) {
        cout << "Received request:\n" << buffer << endl;
        ParsedRequest *request = ParsedRequest_create();
        if(ParsedRequest_parse(request, buffer, strlen(buffer)) < 0) {
            cerr << "Parsing Failed" << endl;
            sendError(socket, 400);
        } else {
             if(!strcmp(request->method, "GET")) {
            if(!request->host) {
                cerr << "Missing Host header" << endl;
                sendError(socket, 400);
            } else if(!request->path || checkHTTPversion(request->version) != 1) {
                sendError(socket, 400);
            } else {
                if(handle_request(socket, request, tempReq) < 0) {
                    sendError(socket, 500);
                }
            }
        } else {
            sendError(socket, 501);
        }
        }
        ParsedRequest_destroy(request);
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    delete[] buffer;
    delete[] tempReq;
    sem_post(&semaphore);
    return NULL;
} 

int main(int argc, char* argv[]){
    
    if(argc==2){
        port_number = atoi(argv[1]);
    }
    else{
        cout<<"Invalid arguments"<<endl;
        exit(1);
    }

    cout<< "Starting Proxy Server at port number: "<<atoi(argv[1])<<endl;

    int client_socketId, clientLen;
    struct sockaddr_in server_addr, client_addr; 
    
    //sockaddr_in is used for specifying address family (sin_family), port (sin_port), ip address (sin_addr)

    //Initializing Locks
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&mutex_lock,NULL);

    proxy_socketId = socket(AF_INET,SOCK_STREAM, 0);

    if(proxy_socketId<0){
        cerr<<"Failed to create Socket"<<endl;
        exit(1);
    }

    cout<<"Socket Created"<<endl;

    /*I want the socket I just created to be global socket i.e i want to treat this socket kind of like a middleware through
    which clients are able to send requests and get response*/

    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse))<0){
        cerr<<"setSockOpt Failed"<<endl;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_port = htons(port_number);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //Binding Socket
     if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr))<0){
        cerr<<"Couldn't bind socket as port is not free"<<endl;
        exit(1);
     }

     cout<<"Binding on port: "<<atoi(argv[1])<<endl;

    //Proxy Socket listening to the request
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status<0){
        cerr<<"Error listening to requests from clients"<<endl;
        exit(1);
    }

    int i = 0;
    int connected_sockedid[MAX_CLIENTS];
    while(1){
        memset(&client_addr, 0, sizeof(client_addr));
        clientLen = sizeof(client_addr);
        //accepting the connection
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&clientLen);
        if (client_socketId < 0)
        {
            cerr << "Error in accepting connection. Error code: " << errno << endl;
            exit(1);
        }
        else connected_sockedid[i] = client_socketId;  //storing accepted client into the array
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ip_addr, str,INET_ADDRSTRLEN);
        cout<<"Client connected to "<<ntohs(client_addr.sin_port)<<" and Client IP: "<<str<<endl;

        //creating thread for each client
        pthread_create(&tid[i],NULL, threadFn, (void*)&connected_sockedid[i]);
        i++;
    }
    close(proxy_socketId);
    return 0;
}
