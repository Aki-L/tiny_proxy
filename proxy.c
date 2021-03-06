#include "csapp.h"
#include "tools/threadpool.h"
#include <sys/epoll.h>

#define CONNFD_POOL_SIZE 7
#define EPOLL_SIZE 1000
#define EPOLL_EVENTS 100

pthread_mutex_t connfd_mutex;
int epollfd;
void doit(int fd);
void *doit_proxy(void *fdp);
int build_requesthdrs(char *rq, rio_t *rp);
int set_defaultrequesthdrs(char *rq, int is_host_set, char* hostname);
int forward_requesthdrs(char *hostname, char *port, char *request);
int parse_hostname(char *hostname, char *port);
int build_reply(int fd, char *reply);
int forward_requestheader(int fd, rio_t *rp, char *host);
int forward_reply(int serverfd, int clientfd);
int setnonblocking(int fd);
int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server);

int main(int argc, char **argv){
	int listenfd, connfd, nfds, n;
	struct epoll_event ev, events[EPOLL_EVENTS];
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	if(argc!=2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	threadpool_t *pool = threadpool_create(CONNFD_POOL_SIZE);
	pthread_mutex_init(&connfd_mutex, NULL);
	epollfd = epoll_create(EPOLL_SIZE);
	if(epollfd == -1){
		perror("epoll_create error");
		exit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.fd = listenfd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev)==-1){
		perror("epoll_ctl: listen_sock");
		exit(-1);
	}

	while(1){
		nfds = epoll_wait(epollfd, events, EPOLL_EVENTS, -1);
		if(nfds == -1){
			perror("epoll_wait");
			exit(-1);
		}
		for(n = 0; n < nfds; n++){
			if(events[n].data.fd==listenfd){
				clientlen = sizeof(clientaddr);
				//pthread_mutex_lock(&connfd_mutex);
				connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
				if(connfd == -1){
					perror("accept");
					exit(0);
				}
				Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
				printf("Accept connection from (%s, %s)\n", hostname, port);
				setnonblocking(connfd);
				ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				ev.data.fd = connfd;
				if(epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) == -1){
					perror("epoll_ctl: connfd");
					exit(-1);
				}
			}else{
				pthread_mutex_lock(&connfd_mutex);
				threadpool_add(pool, &doit_proxy, (void*)(&(events[n].data.fd)));		
			}
		}
		//doit(connfd);
		//threadpool_add(pool, &doit_proxy, (void*)(&connfd));
	}
	threadpool_destroy(pool);
	pthread_mutex_destroy(&connfd_mutex);
}

int setnonblocking(int fd){
	int flag = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flag| O_NONBLOCK);
}

void *doit_proxy(void *fdp){
	int fd = *((int *)fdp);
	printf("doit proxy with fd %d\n", fd);
	pthread_mutex_unlock(&connfd_mutex);
	doit(fd);
}

void doit(int fd){
	int is_static, is_host_set, serverfd;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], rq_port[MAXLINE];
	char hostname[MAXLINE], filename[MAXLINE], cgiargs[MAXLINE], request[MAXLINE], reply[MAXLINE];
	rio_t rio_client, rio_server;

	Rio_readinitb(&rio_client, fd);
	Rio_readlineb(&rio_client, buf, MAXLINE);
	printf("Request headers:\n");
	printf("%s", buf);
	sscanf(buf, "%s %*[^/]//%[^/]%[^ ] %s", method, hostname, uri, version);
	printf("method: %s, hostname: %s, uri: %s, version: %s\n", method, hostname, uri, version);
//	if(strcasecmp(method, "GET")){
//		clienterror(fd, method, "501", "Not implemented", "Proxy does not implement this method");
//		return;
//	}
	sprintf(request, "%s %s %s\r\n", method, uri, version);
	parse_hostname(hostname, rq_port);	
	serverfd = Open_clientfd(hostname, rq_port);
	Rio_readinitb(&rio_server, serverfd);
	Rio_writen(serverfd, request, strlen(request));
	if(!strcmp(version, "HTTP/1.0")){
		forward_requestheader(serverfd, &rio_client, hostname);
		reply_nonconnection(serverfd, fd, &rio_server);
		Close(serverfd);
		Close(fd);
	}else if(!strcmp(version, "HTTP/1.1")){
		if(!strcasecmp(method, "CONNECT")){
					
		}else{
			clienterror(fd, method, "501", "Not implemented", "Proxy does not implement this method");
			Close(serverfd);
			Close(fd);
			return;
		}
	}else{
		clienterror(fd, version, "501", "Not implemented", "Proxy does not implement this version of HTTP protocal");
	}



	/*
	is_host_set = build_requesthdrs(request, &rio);
	set_defaultrequesthdrs(request, is_host_set, hostname);
	clientfd = forward_requesthdrs(hostname, rq_port, request);
	build_reply(clientfd, reply);
	Rio_writen(fd, reply, strlen(reply));
	*/


	//forward_reply(serverfd, fd);
	//Close(serverfd);
}

int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server){
	char buf[MAXLINE];
	size_t n;
	while((n = Rio_readnb(rp_server, buf, MAXLINE))!=0){
		printf("proxy received %d bytes, forwarded to client", n);
		Rio_writen(clientfd, buf, n);
	}
}
int forward_reply(int serverfd, int clientfd){
        rio_t rio;
        int length;
        char buf[MAXLINE], type[MAXLINE], *srcp;
        char headername[MAXLINE], headerdata[MAXLINE];

        Rio_readinitb(&rio, serverfd);

        Rio_readlineb(&rio, buf, MAXLINE);
        Rio_writen(clientfd, buf, strlen(buf));

        while(1){
                Rio_readlineb(&rio, buf, MAXLINE);

                if(!strcmp(buf, "\r\n")) {
			Rio_writen(clientfd, buf, strlen(buf));
			break;
		}

                sscanf(buf, "%[^:]: %s", headername, headerdata);
                if(!strcmp(headername, "Content-length")){
                        length = atoi(headerdata);
                }
                if(!strcmp(headername, "Content-type")){
                        strcpy(type, headerdata);
                }
		Rio_writen(clientfd, buf, strlen(buf));
        }


	/* devide and process response body */
        if((!strcmp(type, "text/html"))||(!strcmp(type, "text/plain"))){
                while(Rio_readlineb(&rio, buf, MAXLINE)){
                        Rio_writen(clientfd, buf, strlen(buf));
                }
        }else{
		char nbuf[length+1];
                Rio_readnb(&rio, nbuf, length);
                Rio_writen(clientfd, nbuf, length);
        }
        return 0;
}

int forward_requestheader(int fd, rio_t *rp, char *host){
        char buf[MAXLINE], headername[MAXLINE], headerdata[MAXLINE];
        int is_host_set = 0, read_bytes;

        printf("Request header:\n");

	sprintf(buf, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n");      
        Rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Connection: close\r\n");     
        Rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Proxy-Connection: close\r\n");      
        Rio_writen(fd, buf, strlen(buf));

        while(1){
                while((read_bytes = Rio_readlineb(rp, buf, MAXLINE))<=0){
			if(read_bytes<0){
				if((errno != EINTR) && (errno != EWOULDBLOCK) && (errno != EAGAIN)){
					perror("Rio_readlineb: forward_requestheader");
					break;
				}
			}else{
				printf("connection closed by remote server");
				break;
			}
		}
                if(!strcmp(buf, "\r\n")) {
        		if(!is_host_set){
				sprintf(buf, "Host: %s", host);
				printf("%s", buf);
				Rio_writen(fd, buf, strlen(buf));
			}
                        Rio_writen(fd, buf, strlen(buf));
                        break;
                }
                sscanf(buf, "%[^:]: %s", headername, headerdata);
                if(!strcmp(headername, "Host")) is_host_set = 1;
		if((!strcmp(headername, "User-Agent"))||(!strcmp(headername, "Connection"))||(!strcmp(headername, "Proxy-Connection"))) continue;

                Rio_writen(fd, buf, strlen(buf));
        }
}

int build_reply(int fd, char *reply){
	rio_t rio;
	int length;
	char buf[MAXLINE], type[MAXLINE];
	char headername[MAXLINE], headerdata[MAXLINE];

	Rio_readinitb(&rio, fd);

	/* build response line */
	Rio_readlineb(&rio, buf, MAXLINE);
	sprintf(reply, "%s", buf);

	/* build response header */
	while(1){
		Rio_readlineb(&rio, buf, MAXLINE);
		sprintf(reply, "%s%s", reply, buf);
		if(!strcmp(buf, "\r\n")) break;
		sscanf(buf, "%[^:]: %s", headername, headerdata);
		if(!strcmp(headername, "Content-length")){
			length = atoi(headerdata);
		}
		if(!strcmp(headername, "Content-type")){
			strcpy(type, headerdata);
		}
	}

	/* devide and process response body */
	if((!strcmp(type, "text/html"))||(!strcmp(type, "text/plain"))){
		while(Rio_readlineb(&rio, buf, MAXLINE)){
			sprintf(reply, "%s%s", reply, buf);
		}
	}else{
		Rio_readnb(&rio, buf, length);
		sprintf(reply, "%s%s", reply, buf);
	}
	return 0;
}

int set_defaultrequesthdrs(char *rq, int is_host_set, char* hostname){
	if(!is_host_set){
		printf("Host set in original request");
		sprintf(rq, "%sHost: %s\r\n", rq, hostname);
	}

	sprintf(rq, "%sUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n", rq);
	sprintf(rq, "%sConnection: close\r\n", rq);
	sprintf(rq, "%sProxy-Connection: close\r\n", rq);

	return 0;
}

int build_requesthdrs(char *rq, rio_t *rp){
	char buf[MAXLINE], headername[MAXLINE], headerdata[MAXLINE];
	int is_host_set = 0;

	printf("Request header:\n");
	while(1){
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s\n", buf);
		if(!strcmp(buf, "\r\n")) {
			sprintf(rq, "%s\r\n", rq);
			break;
		}
		sscanf(buf, "%[^:]: %s", headername, headerdata);
		if(!strcmp(headername, "Host")) is_host_set = 1;
		sprintf(rq, "%s%s", rq, buf);
	}
	return is_host_set;
}

int forward_requesthdrs(char *hostname, char *rq_port, char *request){
	int clientfd;

	clientfd = Open_clientfd(hostname, rq_port);
	Rio_writen(clientfd, request, strlen(request));

	return clientfd;
}

int parse_hostname(char *hostname, char *port){
	char *token, *save_ptr;
	const char s[2] = ":";
	token = strtok_r(hostname, s, &save_ptr);
	if((token = strtok_r(NULL, s, &save_ptr))!=NULL){
		strcpy(port, token);
	}else{
		strcpy(port, "80");
	}
	return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
	char buf[MAXLINE], body[MAXBUF];

	sprintf(body, "<html><title>Proxy Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web Proxy</em>\r\n", body);

	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
}
