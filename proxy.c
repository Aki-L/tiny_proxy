#include "csapp.h"
#include "tools/threadpool.h"
#include "tools/concurrent_hashmap.h"
#include "tools/parse_requestline.h"
#include "proxy.h"
#include <sys/epoll.h>

#define CONNFD_POOL_SIZE 7
#define EPOLL_SIZE 1000
#define EPOLL_EVENTS 100
#define HASHMAP_SIZE 100

/**********************************************************************************************/
/*
 *
 *
 *
 *                  typedef struct connection_status{
 *                          int keep_alive;
 *                          char transfer_encoding[TYPESTRLEN];
 *                          int client_shuttingdown;
 *                          int server_shuttingdown;
 *                          int content_length;
 *                          char content_type[TYPESTRLEN];
 *                  } connection_status_t;
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 * *******************************************************************************************/

pthread_mutex_t connfd_mutex;
int epollfd;
void doit(int fd);
void *doit_proxy(void *fdp);
int build_requesthdrs(char *rq, rio_t *rp);
int set_defaultrequesthdrs(char *rq, int is_host_set, char* hostname);
int forward_requesthdrs(char *hostname, char *port, char *request);
int parse_hostname(char *hostname, char *port);
int build_reply(int fd, char *reply);
int forward_requestheader(int fd, rio_t *rp, char *host, char *version, char *method, connection_status_t *cstatus);
int forward_reply(int serverfd, int clientfd);
int setnonblocking(int fd);
int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server);

void* do_connect(void* fdp);
chmap_t *fdmap;

int main(int argc, char **argv){
	int listenfd, connfd, nfds, n, awakedfd;
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

	fdmap = hashmap_init(HASHMAP_SIZE);
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
			printf("[main] fd %d awake\n", events[n].data.fd);
			pthread_mutex_lock(&connfd_mutex);
			awakedfd = events[n].data.fd;
			pthread_mutex_unlock(&connfd_mutex);
			if(events[n].data.fd==listenfd){
				clientlen = sizeof(clientaddr);
				//pthread_mutex_lock(&connfd_mutex);
				connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
				if(connfd == -1){
					perror("accept");
					exit(0);
				}
				getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
				printf("[main] Accept connection from (%s, %s)\n", hostname, port);
				//setnonblocking(connfd);
				ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				ev.data.fd = connfd;
				if(epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) == -1){
					perror("epoll_ctl: connfd");
					exit(-1);
				}
			}else if(hashmap_containsKey(fdmap, events[n].data.fd)){
				pthread_mutex_lock(&connfd_mutex);
				threadpool_add(pool, &do_connect, (void*)(&events[n].data.fd));
			}else{
				pthread_mutex_lock(&connfd_mutex);
				threadpool_add(pool, &doit_proxy, (void*)(&(events[n].data.fd)));		
			}
		}
		//doit(connfd);
		//threadpool_add(pool, &doit_proxy, (void*)(&connfd));
	}
	hashmap_destroy(fdmap);
	threadpool_destroy(pool);
	pthread_mutex_destroy(&connfd_mutex);
}

void* do_connect(void* fdp){
	int clientfd = *((int *)fdp);
	pthread_mutex_unlock(&connfd_mutex);
	printf("[do_connect] thread 0x%x entering do_connect with fd %d\n", pthread_self(), clientfd);
	int serverfd = hashmap_get(fdmap, clientfd);
	printf("[do_connect] clientfd: %d, serverfd: %d\n", clientfd, serverfd);
	char buf[MAXLINE];
	while(1){
		int read_bytes = read(clientfd, buf, MAXLINE);
		printf("[do_connect] received %d bytes from connect(%d, %d)\n", read_bytes, clientfd, serverfd);
		if(read_bytes==0){
			printf("[do_connect] received EOF from connect(%d, %d)\n", clientfd, serverfd);
			Close(clientfd);
			printf("[do_connect] closing fd: %d\n", clientfd);
			Close(serverfd);
			printf("[do_connect] closing fd: %d\n", serverfd);
			hashmap_remove(fdmap, clientfd);
			hashmap_remove(fdmap, serverfd);
			epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
			epoll_ctl(epollfd, EPOLL_CTL_DEL, serverfd, NULL);
			break;
		}else if(read_bytes<0){
			if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK){
				//rio_writen(serverfd, buf, sizeof(buf));
				//printf(("[do_connect][forward] %o\n", buf));
				return;
			}else{
				perror("rio_readn: do_connect");
				return;
			}
		}else{
			rio_writen(serverfd, buf, read_bytes);
			printf("[do_connect][forward] %o\n", buf);
			if(read_bytes<MAXLINE) return;
		}
	}
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
	struct epoll_event ev_server, ev_client;
	requestline_data_t *rqdata = (requestline_data_t *)malloc(sizeof(requestline_data_t));
	connection_status_t *cstatus = (connection_status_t *)malloc(sizeof(connection_status_t));

	rio_readinitb(&rio_client, fd);
	rio_readlineb(&rio_client, buf, MAXLINE);
	printf("Request line:\n");
	printf("%s", buf);
	if(parse_requestline(buf, rqdata)==-1){
		clienterror(fd, method, "400", "Bad request", "Invalid request line");
		Close(fd);
		return;
	}
	//sscanf(buf, "%s %*[^/]//%[^/]%[^ ] %s", method, hostname, uri, version);
	printf("method: %s, schema: %s, hostname: %s, port: %s, loc: %s, version: %s\n", rqdata->method, rqdata->schema, rqdata->host, rqdata->port, rqdata->loc, rqdata->version);
//	if(strcasecmp(method, "GET")){
//		clienterror(fd, method, "501", "Not implemented", "Proxy does not implement this method");
//		return;
//	}
	//sprintf(request, "%s %s %s\r\n", rqdata->method, rqdata->loc, rqdata->version);
	//parse_hostname(hostname, rq_port);

	if((serverfd = open_clientfd(rqdata->host, rqdata->port))<0){
		clienterror(fd, rqdata->host, "404", "Not found", "Failed to open connection to host");
		Close(fd);
		return;
	}
	rio_readinitb(&rio_server, serverfd);
	//rio_writen(serverfd, request, strlen(request));
	//printf("strcasecmp: %d", !strcasecmp(rqdata->method, "GET"));
	if(!strcmp(rqdata->version, "HTTP/1.0")){
		sprintf(request, "%s %s %s\r\n", rqdata->method, rqdata->loc, rqdata->version);
		rio_writen(serverfd, request, strlen(request));
		forward_requestheader(serverfd, &rio_client, rqdata->host, rqdata->version, rqdata->method, cstatus);
		reply_nonconnection(serverfd, fd, &rio_server);
		Close(serverfd);
		printf("closing fd: %d\n", serverfd);
		Close(fd);
		printf("closing fd: %d\n", fd);
	}else if(!strcmp(rqdata->version, "HTTP/1.1")){
		if(!strcasecmp(rqdata->method, "CONNECT")){
			//sprintf(request, "%s %s:%s %s\r\n", rqdata->method, rqdata->host, (strlen(rqdata->port)==0 ? "443" : rqdata->port), rqdata->version);
			//rio_writen(serverfd, request, strlen(request));
			//printf("[forward] %s\n", request);
			while(strcmp(buf, "\r\n")){
				rio_readlineb(&rio_client, buf, MAXLINE);
				//rio_writen(serverfd, buf, strlen(buf));
				printf("[received] %s\n", buf);
			}
			//rio_writen(serverfd, buf, strlen(buf));
			printf("[received] %s\n", buf);
			
			sprintf(buf, "HTTP/1.1 200 Connection established\r\n");
			hashmap_put(fdmap, fd, serverfd);
			hashmap_put(fdmap, serverfd, fd);
			setnonblocking(serverfd);
			ev_server.events = EPOLLIN | EPOLLET;
			ev_server.data.fd = serverfd;
			if(epoll_ctl(epollfd, EPOLL_CTL_ADD, serverfd, &ev_server)==-1){
				perror("epoll_ctl: connect server");
				exit(-1);
			}
			ev_client.events = EPOLLIN | EPOLLET;
			ev_client.data.fd = fd;
			setnonblocking(fd);
			if(epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev_client)==-1){
				perror("epoll_ctl: connect client");
				epoll_ctl(epollfd, EPOLL_CTL_DEL, serverfd, &ev_server);
			}

			rio_writen(fd, buf, strlen(buf));
			sprintf(buf, "\r\n");
			rio_writen(fd, buf, strlen(buf));

		}else if (!strcasecmp(rqdata->method, "GET")){
			sprintf(request, "%s %s %s\r\n", rqdata->method, rqdata->loc, rqdata->version);
			rio_writen(serverfd, request, strlen(request));			
			forward_requestheader(serverfd, &rio_client, rqdata->host, rqdata->version, rqdata->method, cstatus);
			reply_nonconnection(serverfd, fd, &rio_server);
			Close(serverfd);
			printf("closing fd: %d\n", serverfd);
			Close(fd);
			printf("closing fd: %d\n", fd);
		}else{
			//printf("rqdata: 0x%x, method: %s", rqdata, rqdata->method);
			clienterror(fd, rqdata->method, "501", "Not implemented", "Proxy does not implement this method");
			Close(serverfd);
			printf("closing fd: %d\n", serverfd);
			Close(fd);
			printf("closing fd: %d\n", fd);
		}
	}else{
		clienterror(fd, rqdata->version, "501", "Not implemented", "Proxy does not implement this version of HTTP protocal");
	}
	free(rqdata);
	free(cstatus);

	/*
	is_host_set = build_requesthdrs(request, &rio);
	set_defaultrequesthdrs(request, is_host_set, hostname);
	clientfd = forward_requesthdrs(hostname, rq_port, request);
	build_reply(clientfd, reply);
	rio_writen(fd, reply, strlen(reply));
	*/


	//forward_reply(serverfd, fd);
	//Close(serverfd);
}

int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server){
	char buf[MAXLINE];
	size_t n;
	while((n = rio_readnb(rp_server, buf, MAXLINE))!=0){
		printf("proxy received %d bytes, forwarded to client", n);
		rio_writen(clientfd, buf, n);
	}
}
int forward_reply(int serverfd, int clientfd){
        rio_t rio;
        int length;
        char buf[MAXLINE], type[MAXLINE], *srcp;
        char headername[MAXLINE], headerdata[MAXLINE];

        rio_readinitb(&rio, serverfd);

        rio_readlineb(&rio, buf, MAXLINE);
        rio_writen(clientfd, buf, strlen(buf));

        while(1){
                rio_readlineb(&rio, buf, MAXLINE);

                if(!strcmp(buf, "\r\n")) {
			rio_writen(clientfd, buf, strlen(buf));
			break;
		}

                sscanf(buf, "%[^:]: %s", headername, headerdata);
                if(!strcmp(headername, "Content-length")){
                        length = atoi(headerdata);
                }
                if(!strcmp(headername, "Content-type")){
                        strcpy(type, headerdata);
                }
		rio_writen(clientfd, buf, strlen(buf));
        }


	/* devide and process response body */
        if((!strcmp(type, "text/html"))||(!strcmp(type, "text/plain"))){
                while(rio_readlineb(&rio, buf, MAXLINE)){
                        rio_writen(clientfd, buf, strlen(buf));
                }
        }else{
		char nbuf[length+1];
                rio_readnb(&rio, nbuf, length);
                rio_writen(clientfd, nbuf, length);
        }
        return 0;
}

int forward_requestheader(int fd, rio_t *rp, char *host, char *version, char *method, connection_status_t *cstatus){
        char buf[MAXLINE], headername[MAXLINE], headerdata[MAXLINE];
        int is_host_set = 0, read_bytes;

        printf("Request header:\n");

	sprintf(buf, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n");      
        rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Connection: close\r\n");     
        rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Proxy-Connection: close\r\n");
        rio_writen(fd, buf, strlen(buf));

        while(1){
                if((read_bytes = rio_readlineb(rp, buf, MAXLINE))<=0){
			if(read_bytes<0){
				if((errno != EINTR) && (errno != EWOULDBLOCK) && (errno != EAGAIN)){
					perror("rio_readlineb: forward_requestheader");
					continue;
				}
			}else{
				printf("connection closed by remote client");
				cstatus->client_shuttingdown = 1;
				break;
			}
		}
                if(!strcmp(buf, "\r\n")) {
        		if(!is_host_set){
				sprintf(buf, "Host: %s", host);
				printf("%s", buf);
				rio_writen(fd, buf, strlen(buf));
			}
                        rio_writen(fd, buf, strlen(buf));
                        break;
                }
                sscanf(buf, "%[^:]: %s", headername, headerdata);
                if(!strcmp(headername, "Host")) is_host_set = 1;
		if(!strcmp(headername, "User-Agent")){
			continue;
		}
		if(!strcmp(headername, "Connection")){

			continue;
		}
		if(!strcmp(headername, "Proxy-Connection")){
			continue;
		}

                rio_writen(fd, buf, strlen(buf));
        }
}

int build_reply(int fd, char *reply){
	rio_t rio;
	int length;
	char buf[MAXLINE], type[MAXLINE];
	char headername[MAXLINE], headerdata[MAXLINE];

	rio_readinitb(&rio, fd);

	/* build response line */
	rio_readlineb(&rio, buf, MAXLINE);
	sprintf(reply, "%s", buf);

	/* build response header */
	while(1){
		rio_readlineb(&rio, buf, MAXLINE);
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
		while(rio_readlineb(&rio, buf, MAXLINE)){
			sprintf(reply, "%s%s", reply, buf);
		}
	}else{
		rio_readnb(&rio, buf, length);
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
		rio_readlineb(rp, buf, MAXLINE);
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
	rio_writen(clientfd, request, strlen(request));

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
	rio_writen(fd, buf, strlen(buf));
}
