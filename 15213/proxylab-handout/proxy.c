/* proxy.c
 *
 * tianyinj@andrew.cmu.edu
 *
 * Accept conncetion request from client
 * Parsing connection request and forward to server
 * Send server's respons back to client.
 *
 * With Posix thread concurrent
 *   and Semaphores to implment synchronizing threads.
 *
 * Caching with a chill LRU evicting policy
 */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"
#include "cache.c"

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection ="Proxy-Connection: close\r\n";

int parse_uri(char *uri, char *hostname, char *path, char *port);
void read_requesthdrs(rio_t *rp);
void doit(int fd);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void *thread(void *vargp);

/* Waiting for connection request on the port specified
 * by commandline.
 *
 * Once received, create connection file descriptor and
 * handle the fd with a thread.
 *
 * Initialize caching global parameter.
 */
int main(int argc, char **argv)
{
    int listenfd;
    int *connfdp;
    //char hostname[MAXLINE],port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_in clientaddr;

    pthread_t tid;

    init();

    //printf("%s%s%s", user_agent_hdr, accept_hdr, accept_encoding_hdr);

    if (argc!=2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(-1);
    }

    listenfd=Open_listenfd(argv[1]);

    while(1){
        clientlen=sizeof(struct sockaddr_in);
        connfdp=Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        //Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                //port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        //doit((void*)connfd_ptr);
        Pthread_create(&tid,NULL,thread,connfdp);
        //Pthread_join(tid,NULL);
    }
}

/* thread prototype from textbook
 * detach thread, pass fd to util function
 * After executation, free pointer and close file
 *
 */
void *thread(void *vargp){
    int connfd=*((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * parsing the input uri.
 *
 * hostname must begin with http://
 *
 * If uri has path and port, use the input
 * Otherwise use 80 for port and / for path
 *
 */
int parse_uri(char *uri, char *hostname, char *path, char *port){

    char *name_start;
    char *name_end;
    char *port_start;
    char *path_start;
    int path_len;

    if(strncasecmp(uri, "http://", 7)!=0){
        printf("uri:%s\n",uri);
        unix_error("Bad URI hostname\n");
    }

    name_start=uri+7;
    port_start=strchr(name_start,':');
    path_start=strchr(name_start,'/');
    if (!path_start){
        strcpy(path,"/");
        path_start=uri+strlen(uri);
    }
    else{
        path_len=strlen(uri)-(path_start-uri);
        strncpy(path,path_start,path_len);
    }

    if (!port_start){
        strcpy(port,"80");
        name_end=path_start;
    }

    else{
        strncpy(port,port_start+1,path_start-port_start-1);
        name_end=port_start;
    }

    strncpy(hostname,name_start,name_end-name_start);
    //hostname[name_end-name_start]="\r\n";
    //path[path_end-path_start]="\r\n";
    return 1;
}

/*
 * Take connfd as argument, parsing out method,
 *  protocal version and hostname, path and port
 *
 * This proxy only accept GET and use only HTTP/1.0
 *
 * First edtablish a connection with hostname at port
 * Then extract all headers from fd and attach default header to it
 *
 * Write those header as well as fd body to the server-proxy file
 * Read the server-proxy file line by line and write the content
 *  to the client-proxy fd
 *
 * If the same uri has occured in the cache link list, derictly
 *  pull out the saved server reponse and write it to the
 *  client-proxy fd.
 * Put this newly used cache elem in the most significant place.
 *
 * If it has not been cached yet and the response size is less than
 *  max object size to cache, and the total cache size didn't exceed
 *  the max, write it to the cache. Otherwise, evict the oldest cache elem.
 */
void doit(int fd){

    rio_t request;
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    int write_host=0;
    char requests[MAXLINE];

    Rio_readinitb(&request,fd);

    if ((Rio_readlineb(&request,buf,MAXLINE)<0)&&(errno==ECONNRESET)){
        Open((void*)&fd,NULL,NULL);
        Rio_readlineb(&request,buf,MAXLINE);
    }


    sscanf(buf, "%s %s %s", method, uri, version);

    //printf("buf %s\n",buf);
    //printf("method %s, uri %s, version %s\n", method,uri, version);
    if (strcmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                                "Tiny does not implement this method");
        return;
    }

    if (!parse_uri(uri,hostname,path,port)){
        clienterror(fd, uri, "400", "Bad Request",
                             "Cannot parsing request");
    }

    elem *cache;

    if ((cache=find(uri))!=NULL){
        update(cache);
        Rio_writen(fd,cache->content,cache->size);
        return;
    }

    sprintf(buf,"GET %s HTTP/1.0\r\n",path);
    //Rio_writen(client_fd,buf,strlen(buf));
    strcpy(requests,buf);
    //rio_t proxy;

    //Rio_readinitb(&proxy,client_fd);

    //Rio_writen(client_fd,(void*)user_agent_hdr,strlen(user_agent_hdr));
    //Rio_writen(client_fd,(void*)accept_hdr,strlen(accept_hdr));
    //Rio_writen(client_fd,(void*)accept_encoding_hdr,
            //strlen(accept_encoding_hdr));
    //Rio_writen(client_fd,(void*)connection,strlen(connection));
    //Rio_writen(client_fd,(void*)proxy_connection,strlen(proxy_connection));

    while(Rio_readlineb(&request,buf,MAXLINE)>2){
        if (strstr(buf,"Host:")){
            strcat(requests,buf);
            write_host=1;
        }
        else if ((!strstr(buf, "User-Agent:"))
                &&(!strstr(buf,"Accept:"))
                &&(!strstr(buf,"Accept-Encoding:"))
                &&(!strstr(buf, "Connection:"))
                &&(!strstr(buf,"Proxy Connection:"))){
            //printf("writing to clientfd:%s\n",buf);
            strcat(requests,buf);
        }
        //printf("looping?\n");
    }

    if(!write_host){
        sprintf(buf,"Host: %s\r\n",hostname);
        strcat(requests,buf);
    }

    strcat(requests,user_agent_hdr);
    strcat(requests,accept_hdr);
    strcat(requests,accept_encoding_hdr);
    strcat(requests,connection);
    strcat(requests,proxy_connection);
    strcat(requests,"\r\n");

    int client_fd=Open_clientfd(hostname,port);
    //Rio_writen(client_fd,requests,strlen(requests));

    rio_t serverreply;
    //Rio_readinitb(&serverreply,client_fd);
    Rio_writen(client_fd,requests,strlen(requests));

    Rio_readinitb(&serverreply,client_fd);
    int size;

    int obj_size=0;
    char cache_buf[MAX_OBJECT_SIZE];

    while((size=Rio_readnb(&serverreply,buf,MAXLINE))>0){
        //printf("writing to fd:%s\n",buf);
        Rio_writen(fd,buf,size);
        obj_size+=size;
        if (obj_size < MAX_OBJECT_SIZE){
            memcpy(cache_buf,buf,size);
        }
    }

    if (obj_size<MAX_OBJECT_SIZE){
        insert(uri,obj_size,cache_buf);
        //printf("insert to cache %s, %s\n",uri,cache_buf);
    }

    //Rio_writen(fd,"\r\n",2);

    //printf("closing\n");
    Close(client_fd);

    return;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienaterror */
