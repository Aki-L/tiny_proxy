/* proxy - a tiny HTTP proxy
** 
** Copyright © 2021 by Aki <akiyama310050@gmail.com>.
** All rights reserved.
**
** A tiny web proxy implements http/https proxy.
** 
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/


/* caspp rio package */
#include "csapp.h"

/* tools package */
#include "tools/threadpool.h"
#include "tools/concurrent_hashmap.h"
#include "tools/parse_requestline.h"
#include "tools/bqueue.h"

/* head file */
#include "proxy.h"

/* library */
#include <assert.h>
#include <sys/epoll.h>

#define CONNFD_POOL_SIZE 100
#define EPOLL_SIZE 1000
#define EPOLL_EVENTS 100
#define HASHMAP_SIZE 100


/* declarations */
void doit(int fd, int epollfd);
void *doit_proxy(void *fdp);
int build_requesthdrs(char *rq, rio_t *rp);
int set_defaultrequesthdrs(char *rq, int is_host_set, char* hostname);
int forward_requesthdrs(char *hostname, char *port, char *request);
int parse_hostname(char *hostname, char *port);
int build_reply(int fd, char *reply);
int forward_requestheader(int fd, rio_t *rp, char *host, char *version, char *method, connection_status_t *cstatus);
int forward_reply(int clientfd, rio_t *rp_server, connection_status_t *cstatus);
int setnonblocking(int fd);
int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server);
int forward_chunked(int fd, rio_t *rp, char* trailer);
int getchunksize(char *buf);
int hex2num(char ch);
void *connfd_handler(void *args);
int reset_oneshot(int epollfd, int fd);
int connect_handler(int clientfd, int serverfd);
void* do_connect(void* fdp);

/* global variables */


/* concurrent hashmap for fd pairs */
chmap_t *fdmap;

/* blocking queue for passing fd to handler threads */
bqueue_t *bqueue;

/* mutex for passing fd pointer passing and dereferencing in multithread condition */
pthread_mutex_t connfd_mutex;
pthread_mutex_t readfd_mutex;

/* listening fd epollfd and worker thread epollfd lists */
int epollfd, epoll_list[CONNFD_POOL_SIZE];


int main(int argc, char **argv){
	int listenfd, connfd, nfds, n, awakedfd, round_robin = 0;
	struct epoll_event ev, events[EPOLL_EVENTS];
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	pthread_t threads[CONNFD_POOL_SIZE];
	if(argc!=2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	bqueue = bqueue_init();
	listenfd = Open_listenfd(argv[1]);
	//threadpool_t *pool = threadpool_create(CONNFD_POOL_SIZE);
	pthread_mutex_init(&connfd_mutex, NULL);
	pthread_mutex_init(&readfd_mutex, NULL);
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

	for(int i = 0; i < CONNFD_POOL_SIZE; i++){
		pthread_mutex_lock(&connfd_mutex);
		int j = i;
		pthread_create(&(threads[i]), NULL, connfd_handler, &j);	
	}

	while(1){
		//pthread_mutex_lock(&(readfd_mutex));
		nfds = epoll_wait(epollfd, events, EPOLL_EVENTS, -1);
		//pthread_mutex_unlock(&(readfd_mutex));
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
				
				
				setnonblocking(connfd);
				ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				ev.data.fd = connfd;
				assert(epoll_list[round_robin]>0);
				if(epoll_ctl(epoll_list[round_robin], EPOLL_CTL_ADD, connfd, &ev) == -1){
					perror("epoll_ctl: connfd");
					exit(-1);
				}

				round_robin = (round_robin+1)%CONNFD_POOL_SIZE;
				//bqueue_insert(bqueue, connfd);
				
			}/*else if(hashmap_containsKey(fdmap, events[n].data.fd)){
				pthread_mutex_lock(&connfd_mutex);
				threadpool_add(pool, &do_connect, (void*)(&events[n].data.fd));
			}else{
				pthread_mutex_lock(&readfd_mutex);
				pthread_mutex_lock(&connfd_mutex);
				threadpool_add(pool, &doit_proxy, (void*)(&(events[n].data.fd)));
				pthread_mutex_unlock(&readfd_mutex);		
			}*/
			else{
				printf("[main] unexpected fd: %d\n", events[n].data.fd);
			}
		}
	}
	for(int i = 0 ; i < CONNFD_POOL_SIZE; i++){
		pthread_join(threads[i], NULL);
	}
	bqueue_destroy(bqueue);
	hashmap_destroy(fdmap);
	//threadpool_destroy(pool);
	pthread_mutex_destroy(&connfd_mutex);
	pthread_mutex_destroy(&readfd_mutex);

}






/***************************************************************************/
/*  Handler used to handle the connfd received from the listenfd with
 *  multithread. Each thread hold its own epollfd and wait in a infinite 
 *  loop respectively. On each loop, check if there is new connfd avalible
 *  in the bqueue, if so, multithread waiting on bqueue_remove racing for 
 *  the bqueue_lock and get and register the connfd on success.
 *  Parameters: NULL */
/***************************************************************************/
void *connfd_handler(void *args){
	int i = *((int *)args);
	pthread_mutex_unlock(&(connfd_mutex));
	printf("[connfd] receive parameter: %d", i);
	struct epoll_event ev, events[EPOLL_EVENTS];
	int conn_epollfd, nfds;
	conn_epollfd = epoll_create(EPOLL_SIZE);
	epoll_list[i] = conn_epollfd;
	if(conn_epollfd == -1){
                perror("epoll_create error");
                exit(-1);
        }
	while(1){
		/*int fd = bqueue_remove(bqueue);
		if(fd>0){
			printf("connfd %d listened from listenfd\n", fd);
			ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
			setnonblocking(fd);
                        ev.data.fd = fd;
   			if(epoll_ctl(conn_epollfd, EPOLL_CTL_ADD, fd, &ev)==-1){
        	        	perror("epoll_ctl: listen_sock");
        	        	exit(-1);
       			}
		}else{
			//printf("bqueue is empty\n");
		}
		*/
		nfds = epoll_wait(conn_epollfd, events, EPOLL_EVENTS, -1);
		if(nfds == -1){
                        perror("epoll_wait");
                        exit(-1);
                }
                for(int n = 0; n < nfds; n++){
			doit(events[n].data.fd, conn_epollfd);
		}
	}
	Close(conn_epollfd);
}





/**********************************************************************************************/
/* Handles request with CONNECT method accepted from clientfd, the connected peer is on serverfd
 * . connect_handler register both clientfd and serverfd into a seperate epollfd to make a tunnel
 * for transforming data between c/s. data tunnel build in an infinite loop, only when read returns
 *  0 which indicating EOF or returns -1 with unexpected errno would the loop break and return.
 *  Parameters: the socket fd of client and server*/
/**********************************************************************************************/

int connect_handler(int clientfd, int serverfd){
	int connect_epollfd, nfds, n, disconnect=0;
	struct epoll_event ev_server, ev_client, events[EPOLL_EVENTS];
        char buf[MAXLINE];
        connect_epollfd = epoll_create1(0);
        if(epollfd==-1){
                perror("epoll_create1");
                return;
        }
        setnonblocking(serverfd);
        ev_server.events = EPOLLIN | EPOLLET;
        ev_server.data.fd = serverfd;
        if(epoll_ctl(connect_epollfd, EPOLL_CTL_ADD, serverfd, &ev_server)==-1){
                perror("epoll_ctl: connect server");
		Close(clientfd);
        	Close(serverfd);
        	Close(connect_epollfd);
                return;
        }
        ev_client.events = EPOLLIN | EPOLLET;
        ev_client.data.fd = clientfd;
        setnonblocking(clientfd);
        if(epoll_ctl(connect_epollfd, EPOLL_CTL_ADD, clientfd, &ev_client)==-1){
                perror("epoll_ctl: connect client");
                epoll_ctl(connect_epollfd, EPOLL_CTL_DEL, serverfd, &ev_server);
		Close(clientfd);
        	Close(serverfd);
        	Close(connect_epollfd);
                return;
        }

        sprintf(buf, "\r\n");
        rio_writen(clientfd, buf, strlen(buf));
        while(1){
                nfds = epoll_wait(connect_epollfd, events, EPOLL_EVENTS, -1);
                if(nfds==-1){
                        perror("epoll_wait: connect_handler");
                        break;
                }
                for(n = 0; n < nfds; n++){
                        if(events[n].data.fd == clientfd){
                                while(1){
                                        int read_bytes = read(clientfd, buf, MAXLINE);
                                        printf("[do_connect] received %d bytes from connect(%d, %d)\n", read_bytes, clientfd, serverfd);
                                        if(read_bytes==0){
                                                printf("[do_connect] received EOF from connect(%d, %d)\n", clientfd, serverfd);
                                                disconnect = 1;
                                                break;
                                        }else if(read_bytes<0){
                                                if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK){
                                                        break;
                                                }else{
                                                        perror("read: connect_handler");
							//disconnect = 1;
                                                        break;
                                                }
                                        }else{
                                                rio_writen(serverfd, buf, read_bytes);
                                                //printf("[do_connect][forward] %o\n", buf);
                                                if(read_bytes<MAXLINE) break;
                                        }
                                }
                        }else{
                                while(1){
                                        int read_bytes = read(serverfd, buf, MAXLINE);
                                        printf("[do_connect] received %d bytes from connect(%d, %d)\n", read_bytes, serverfd, clientfd);
                                        if(read_bytes==0){
                                                printf("[do_connect] received EOF from connect(%d, %d)\n", serverfd, clientfd);
                                                disconnect = 1;
                                                break;
                                        }else if(read_bytes<0){
                                                if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK){
                                                        break;
                                                }else{
                                                        perror("read: connect_handler");
							//disconnect = 1;
                                                        break;
                                                }
                                        }else{
                                                rio_writen(clientfd, buf, read_bytes);
                                                //printf("[do_connect][forward] %o\n", buf);
                                                if(read_bytes<MAXLINE) break;
                                        }
                                }
                        }
                }
		printf("disconnect: %d\n", disconnect);
                if(disconnect) break;
        }
	printf("[connect_handler] Closing fd...\n");
	Close(clientfd);
	Close(serverfd);
	Close(connect_epollfd);
}





/****************************************************************************************************************/
/* Old version of connect_handler, do_connect is user to handle one epollin event of the regitered fd of which are
 * marked as tunnel fd by adding to concurrent hashmap. On read returning 0, close tunnel fds and removes from 
 * epollfd, else, remodification them cause register set EPOLLONESHOT flag
 * Parameter: the pointer of activated clientfd*/
/****************************************************************************************************************/
void* do_connect(void* fdp){
	int clientfd = *((int *)fdp);
	struct epoll_event ev;
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
			hashmap_remove(fdmap, clientfd);
			hashmap_remove(fdmap, serverfd);
			epoll_ctl(epollfd, EPOLL_CTL_DEL, clientfd, NULL);
			epoll_ctl(epollfd, EPOLL_CTL_DEL, serverfd, NULL);
			Close(clientfd);
			printf("[do_connect] closing fd: %d\n", clientfd);
			Close(serverfd);
			printf("[do_connect] closing fd: %d\n", serverfd);
			return;
		}else if(read_bytes<0){
			if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK){
				//rio_writen(serverfd, buf, sizeof(buf));
				//printf(("[do_connect][forward] %o\n", buf));
				break;
			}else{
				perror("rio_readn: do_connect");
				break;
			}
		}else{
			rio_writen(serverfd, buf, read_bytes);
			printf("[do_connect][forward] %o\n", buf);
			if(read_bytes<MAXLINE) break;
		}
	}
	reset_oneshot(epollfd, clientfd);
}




/********************************************************************************/
/* Rearm the fd with EPOLLONESHOT set into the epollfd
 * Parameters: epollfd and the fd to be rearm*/
/********************************************************************************/
int reset_oneshot(int epollfd, int fd){
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    return epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}




/********************************************************************************/
/* Set to nonblocking mode.
 * Parameters: fd*/
/********************************************************************************/
int setnonblocking(int fd){
	int flag = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flag| O_NONBLOCK);
}




/********************************************************************************/
/* Wrapper function for doit() to fit pthread_create(3)
 * Parameters: pointer to the fd to be passed into doit()*/
/********************************************************************************/
void *doit_proxy(void *fdp){
	int fd = *((int *)fdp);
	printf("doit proxy with fd %d\n", fd);
	pthread_mutex_unlock(&connfd_mutex);
	doit(fd, epollfd);
}




/********************************************************************************/
/* Main service logic, takes the activated fd and the epollfd it activated from as
 *  parameter. Parse the request to method/schema/host/port/loc/version and process
 *  them with corresponding functions.
 *  Parameters: fd received from epoll_wait and the corresponding epollfd*/
/********************************************************************************/
void doit(int fd, int epollfd){
	printf("doit with fd %d, epollfd %d\n", fd, epollfd);
	int is_static, is_host_set, serverfd, read_bytes;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], rq_port[MAXLINE];
	char hostname[MAXLINE], filename[MAXLINE], cgiargs[MAXLINE], request[MAXLINE], reply[MAXLINE];
	rio_t rio_client, rio_server;
	struct epoll_event ev_server, ev_client;
	requestline_data_t *rqdata = (requestline_data_t *)malloc(sizeof(requestline_data_t));
	connection_status_t *cstatus = (connection_status_t *)malloc(sizeof(connection_status_t));

	do{
		/* Read the request line and parse */
		rio_readinitb(&rio_client, fd);
		rio_readlineb(&rio_client, buf, MAXLINE);
		printf("Request line:\n");
		printf("%s\n", buf);
		if(parse_requestline(buf, rqdata)==-1){
			printf("parse_requestline error\nclosing fd: %d\n", fd);
			printf("method: %s, schema: %s, hostname: %s, port: %s, loc: %s, version: %s\n", rqdata->method, rqdata->schema, rqdata->host, rqdata->port, rqdata->loc, rqdata->version);
			clienterror(fd, method, "400", "Bad request", "Invalid request line");
			Close(fd);
			break;
		}
		printf("method: %s, schema: %s, hostname: %s, port: %s, loc: %s, version: %s\n", rqdata->method, rqdata->schema, rqdata->host, rqdata->port, rqdata->loc, rqdata->version);
		if(strlen(rqdata->port)==0) strcpy(rqdata->port, "80");


		/* Open the serverfd which the requestline requested */
		if((serverfd = open_clientfd(rqdata->host, rqdata->port))<0){
			clienterror(fd, rqdata->host, "404", "Not found", "Failed to open connection to host");
			Close(fd);
			break;
		}
		rio_readinitb(&rio_server, serverfd);


		/* Process the request according to it request_data */
		if(!strcmp(rqdata->version, "HTTP/1.0")){
			/* HTTP/1.0 request: set nonconnection default */
			sprintf(request, "%s %s %s\r\n", rqdata->method, rqdata->loc, rqdata->version);
			rio_writen(serverfd, request, strlen(request));
			forward_requestheader(serverfd, &rio_client, rqdata->host, rqdata->version, rqdata->method, cstatus);
			reply_nonconnection(serverfd, fd, &rio_server);

			if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)==-1){
                        	perror("epoll_ctl_del: clientfd");
                        }
			/* Close clientfd and serverfd */
			Close(serverfd);
			printf("closing fd: %d\n", serverfd);
			Close(fd);
			printf("closing fd: %d\n", fd);
		}else if(!strcmp(rqdata->version, "HTTP/1.1")){
			/* HTTP/1.1 request: persistent connection if connection header is not set to close */
			
			if(!strcasecmp(rqdata->method, "CONNECT")){
				/* CONNECT method with HTTP/1.1 */

				/* read the rest CONNECT request headers and do nothing */
				while(strcmp(buf, "\r\n") && read_bytes>0){
					read_bytes = rio_readlineb(&rio_client, buf, MAXLINE);
					printf("[received] %s\n", buf);
				}
				printf("[received] %s\n", buf);
				
				/* On success, reply code 200 and Connection established */
				sprintf(buf, "HTTP/1.1 200 Connection established\r\n");
				rio_writen(fd, buf, strlen(buf));

				/* call connect_handler to process the connect loop */
				if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)==-1){
                                	perror("epoll_ctl_del: clientfd");
                                }
				connect_handler(fd, serverfd);

			}else if (!strcasecmp(rqdata->method, "GET")||!strcasecmp(rqdata->method, "POST")){
				/* GET or POST method */

				/* forward request line */
				sprintf(request, "%s %s %s\r\n", rqdata->method, rqdata->loc, rqdata->version);
				rio_writen(serverfd, request, strlen(request));


				/* do request and reply */
				forward_requestheader(serverfd, &rio_client, rqdata->host, rqdata->version, rqdata->method, cstatus);
				forward_reply(fd, &rio_server, cstatus);

				/* close server fd by default and clientfd according to client_setclose and client_shutting down flag */
				Close(serverfd);
				printf("closing fd: %d\n", serverfd);
				if(cstatus->client_setclose || cstatus->client_shuttingdown){
					printf("closing fd: %d\n", fd);
					if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)==-1){
						perror("epoll_ctl_del: clientfd");
					}
					Close(fd);
				}else{
					printf("reset_oneshot: %d\n", fd);
					reset_oneshot(epollfd, fd);
				}
			}else{
				/* unimplemented HTTP/1.1 methods, close fd */
				clienterror(fd, rqdata->method, "501", "Not implemented", "Proxy does not implement this method");
				if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)==-1){
                                	perror("epoll_ctl_del: clientfd");
                                }
				Close(serverfd);
				printf("closing fd: %d\n", serverfd);
				Close(fd);
				printf("closing fd: %d\n", fd);
			}
		}else{
			/* unimplemented HTTP version, close fd */
			clienterror(fd, rqdata->version, "501", "Not implemented", "Proxy does not implement this version of HTTP protocal");
			if(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL)==-1){
                        	perror("epoll_ctl_del: clientfd");
                        }
			Close(fd);
			printf("closing fd: %d\n", fd);
			Close(serverfd);
			printf("closing fd: %d\n", serverfd);
		}
	}while(0);


	/* release malloced space */
	free(rqdata);
	free(cstatus);

}



/********************************************************************************/
/* Forward reply received from serverfd to clientfd, assert serverfd would close
 * when data transfer completed. Rerurn only when received 0 from serverfd.
 * Parameters: serverfd, cliented, server rio_t.*/
/********************************************************************************/
int reply_nonconnection(int serverfd, int clientfd, rio_t *rp_server){
	char buf[MAXLINE];
	size_t n;
	while((n = rio_readnb(rp_server, buf, MAXLINE))!=0){
		printf("proxy received %d bytes, forwarded to client", n);
		rio_writen(clientfd, buf, n);
	}
}




/********************************************************************************/
/* Premier function for forwarding reply. Parses response headers and behave 
 * according to the header data. implement chunked encoding and content-length
 *  type response, otherwise, return a clienterror with code 400.
 *  Parameters: clientfd, server rio_t, connection_status data*/
/********************************************************************************/
int forward_reply(int clientfd, rio_t *rp_server, connection_status_t *cstatus){
        rio_t rio;
        int length, is_contentlength_set = 0, read_bytes;
        char buf[MAXLINE], type[MAXLINE], *srcp, version[MAXLINE], code[MAXLINE], msg[MAXLINE];
        char headername[MAXLINE], headerdata[MAXLINE];


        if(rio_readlineb(rp_server, buf, MAXLINE)==0){
		cstatus->server_shuttingdown = 1;
		return 0;
	}

	/* Process response heeader */
	sscanf(buf, "%s %s %s", version, code, msg);
	if(!strcmp(version, "HTTP/1.0")){
		cstatus->server_setclose = 1;
	}
        rio_writen(clientfd, buf, strlen(buf));

	if(cstatus->client_setclose){
		sprintf(buf, "Connection: close\r\n");
        	rio_writen(clientfd, buf, strlen(buf));
        	sprintf(buf, "Proxy-Connection: close\r\n");
        	rio_writen(clientfd, buf, strlen(buf));
	}

        while(1){
		if((read_bytes = rio_readlineb(rp_server, buf, MAXLINE))<=0){
                        if(read_bytes<0){
                                if((errno != EINTR) && (errno != EWOULDBLOCK) && (errno != EAGAIN)){
                                        perror("rio_readlineb: forward_requestheader");
                                        continue;
                                }
                        }else{
                                printf("connection closed by remote client");
                                cstatus->client_shuttingdown = 1;
                                return -1;
                        }
                }
                if(!strcmp(buf, "\r\n")) {
			rio_writen(clientfd, buf, strlen(buf));
			break;
		}

                sscanf(buf, "%[^:]: %s", headername, headerdata);
		if(!strcmp(headername, "Connection")){
			printf("[server] Connection: %s\n", headerdata);
			if(!strcasecmp(headerdata, "close")){
				cstatus->server_setclose = 1;
			}
			continue;
		}
		if(!strcmp(headername, "Proxy-Connection")){
			printf("[server] Proxy-Connection: %s\n", headerdata);
			if(!strcasecmp(headerdata, "close")){
				cstatus->server_setclose = 1;
			}
			continue;
		}

		if(!strcasecmp(headername, "Trailer")){
			strcpy(cstatus->server_trailer, headerdata);
		}

		if(!strcmp(headername, "Content-length")){
                        cstatus->server_content_length = atoi(headerdata);
                        is_contentlength_set = 1;
                }

                if(!strcmp(headername, "Transfer-Encoding")){
                        strcpy(cstatus->server_transfer_encoding, headerdata);
                }

		rio_writen(clientfd, buf, strlen(buf));
        }


	/* Process response body */
	if(!strcasecmp(cstatus->server_transfer_encoding, "chunked")){
                forward_chunked(clientfd, rp_server, cstatus->server_trailer);
        }else if(is_contentlength_set){
                char contentbuf[cstatus->server_content_length+1];
                if(rio_readnb(rp_server, contentbuf, cstatus->server_content_length)==0){
			cstatus->server_shuttingdown = 1;
		}
                rio_writen(clientfd, contentbuf, cstatus->server_content_length);
	}else{
                clienterror(clientfd, "server reply", "400", "Bad reply", "reply should contains a valid content-length or be chunked");
                cstatus->client_setclose = 1;
                cstatus->server_setclose = 1;
        }

}




/********************************************************************************/
/* Forward request header from client to server. This function parse the request 
 * header and behaves different according to the request data, similar to forward_reply() 
 * . When requested method is POST, the function also implement chunked or content-length
 *  style of transforming request data;
 *  Parameters: serverfd, client rio_t, host, version, method, connection status data*/
/********************************************************************************/
int forward_requestheader(int fd, rio_t *rp, char *host, char *version, char *method, connection_status_t *cstatus){
        char buf[MAXLINE], headername[MAXLINE], headerdata[MAXLINE];
        int is_host_set = 0, is_contentlength_set = 0, read_bytes;

        printf("Request header:\n");

	//sprintf(buf, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n");      
        //rio_writen(fd, buf, strlen(buf));
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
				return -1;
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
			//continue;
		}
		if(!strcmp(headername, "Connection")){
			printf("[client] Connection: %s\n", headerdata);
			if(!strcasecmp(headerdata, "close")){
				cstatus->client_setclose = 1;
			}
			continue;
		}
		if(!strcmp(headername, "Proxy-Connection")){
			printf("[client] Proxy-Connection: %s\n", headerdata);
			if(!strcasecmp(headerdata, "close")){
				cstatus->client_setclose = 1;
			}
			continue;
		}

		if(!strcasecmp(headername, "Trailer")){
                        strcpy(cstatus->client_trailer, headerdata);
                }

		if(!strcmp(headername, "Content-length")){
                        cstatus->client_content_length = atoi(headerdata);
			is_contentlength_set = 1;
                }

		if(!strcmp(headername, "Transfer-Encoding")){
			strcpy(cstatus->client_transfer_encoding, headerdata);
		}

                rio_writen(fd, buf, strlen(buf));
        }


	if(!strcmp(method, "POST")){
		if(!strcasecmp(cstatus->client_transfer_encoding, "chunked")){
			forward_chunked(fd, rp, cstatus->client_trailer);
		}else if(is_contentlength_set){
			char contentbuf[cstatus->client_content_length+1];
			if(rio_readnb(rp, contentbuf, cstatus->client_content_length)==0){
				cstatus->client_shuttingdown = 1;
			}
			rio_writen(fd, contentbuf, cstatus->client_content_length);
		}else{
			clienterror(fd, method, "400", "Bad request", "POST method should contains a valid content-length or be chunked");
			cstatus->client_setclose = 1;
			cstatus->server_setclose = 1;
		}
	}
	return 0;
}



/********************************************************************************/
/* Transfer data which are marked as chunked transfer encoding. On success, return
 *  the whole content-length which is the sum of all the chunk size. when trailer is
 *   set, chunk end up with extra trailer header and finally \r\n.
 *  Parameters: peer fd, peer rio_t, trailer*/
/********************************************************************************/
int forward_chunked(int fd, rio_t *rp, char* trailer){
	char buf[MAXLINE];
	int read_bytes, chunksize, content_length;
	while(1){
		read_bytes = rio_readlineb(rp, buf, MAXLINE);
		printf("[chunk] %s\n", buf);
		if(!strcmp(buf, "0\r\n")){
			printf("encounter end chunk\n");
			rio_writen(fd, buf, read_bytes);
			break;
		}
		chunksize = getchunksize(buf);
		printf("[chunk] read %d bytes chunk\n", chunksize);
		if(chunksize == -1){
			perror("invalid chunk");
			return -1;
		}
		content_length += chunksize;
		read_bytes = rio_readnb(rp, buf, chunksize+2);
		rio_writen(fd, buf, read_bytes);
	}
	if(strlen(trailer)!=0){
		read_bytes = rio_readlineb(rp, buf, MAXLINE);
		printf("[chunk] %s\n", buf);
		rio_writen(fd, buf, read_bytes);
	}
	read_bytes = rio_readlineb(rp, buf, MAXLINE);
	printf("[chunk] %s\n", buf);
	assert(!strcmp(buf, "\r\n"));
	rio_writen(fd, buf, read_bytes);
	return content_length;
}




/********************************************************************************/
/* Decode the chunk, get chunk size from the first line of each chunk
 * Parameters: the first line of trunk*/
/********************************************************************************/
int getchunksize(char *buf){
	int i, num=0, digit;
	char ch;
	for(i = 0 ; i < MAXLINE; i++){
		ch = buf[i];
		if(!isxdigit(ch)) break;
		num*=16;
		digit = hex2num(ch);
		if(digit==-1) return -1;
		num+=digit;
	}
	return num;
}




/********************************************************************************/
/* Cast hex string to its decimal int value.
 * Parameters: hex string*/
/********************************************************************************/
int hex2num(char ch){
	if(ch>= '0' && ch<='9'){
		return ch-'0';
	}
	if(ch>='a' && ch<='f'){
		return 10+ch-'a';
	}
	if(ch>='A' && ch<='F'){
		return 10+ch-'A';
	}
	return -1;
}




/********************************************************************************/
/* Old version function used to build reply with received data from serverfd.
 * Parameters: serverfd, reply buffer used to save built reply.*/
/********************************************************************************/
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




/********************************************************************************/
/* Old version funtion used to set default request header for HTTP/1.0 for consistency.
 * Parameters: buffer, flag if host is set in original request, hostname*/
/********************************************************************************/
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





/********************************************************************************/
/* Old version function to build request header except the default one defined in
 * set_defaultrequesthdrs().
 * Parameters: buffer, client rio_t.*/
/********************************************************************************/
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




/********************************************************************************/
/* Old version forward built request header from to server.
 * Parameters: hostname and port for openclientfd, built request*/
/********************************************************************************/
int forward_requesthdrs(char *hostname, char *rq_port, char *request){
	int clientfd;

	clientfd = Open_clientfd(hostname, rq_port);
	rio_writen(clientfd, request, strlen(request));

	return clientfd;
}




/********************************************************************************/
/* Old version of parsing hostname and port from a host:port pair, if no port
 * exists, set default port as 80.
 * Parameters: host:port pair string, port buffer*/
/********************************************************************************/
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




/********************************************************************************/
/* Set and send client error html to client.
 * Parameters: clientfd, caust string, errcode, shortmsg, longmsg*/
/********************************************************************************/
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
