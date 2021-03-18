#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "parse_requestline.h"
#define RQ_DATALEN 1024
/*
typedef struct requestline_data{
	char method[RQ_DATALEN];
	char schema[RQ_DATALEN];
	char host[RQ_DATALEN];
	char port[RQ_DATALEN];
	char loc[RQ_DATALEN];
	char version[RQ_DATALEN];
} requestline_data_t;

enum parse_state{
	st_start = 0,
	st_method,
	st_space_after_method,
	st_uri,
	st_colon_after_uri,
	st_schema_slash,
	st_schema_slash_slash,
	st_host,
	st_colon_before_port,
	st_port,
	st_loc,
	st_space_before_version,
	st_http_H,
	st_http_HT,
	st_http_HTT,
	st_http_HTTP,
	st_http_slash,
	st_http_slash_version,
	st_done
} parse_state_t;
*/
int parse_requestline(char* rqline, requestline_data_t *rqdata);
/*
int main(){
	char requestline[4096];
	scanf("%[^\n]", requestline);
	requestline_data_t *rqdata = (requestline_data_t *)malloc(sizeof(requestline_data_t));
	printf("%s\n", requestline);
	parse_requestline(requestline, rqdata);
	printf("%s, %s, %s, %s, %s, %s\n", rqdata->method, rqdata->schema, rqdata->host, rqdata->port, rqdata->loc, rqdata->version);
	free(rqdata);
	return 0;
}
*/
/**
 *
 *	[method][space](schema://)(host)(:port)[loc][space][version]
 *
 * 	st_uri refers to the state on which schema/host is not defined
 * 	st_colon_after_uri refers to the state on which port/host is not defined
 * **/
int parse_requestline(char* rqline, requestline_data_t *rqdata){
	char ch, *p, *strend, *rc_start, *rc_end;
	printf("[parse] %s\n", rqline);
	parse_state_t state = st_start;
	p = rqline;
	strend = rqline+strlen(rqline);
	while(p<strend && state!=st_done){
		ch = *p++;
		switch(state){
			case st_start:
				if(isspace(ch)) break;
				else if(ch>'Z'||ch<'A'){
					perror("invalid method, should constructed with A-Z\n");
					return -1;
				}
				rc_start = p-1;
				state = st_method;
				break;
			case st_method:
				if(ch == ' '){
					state = st_space_after_method;
					rc_end = p-1;
					strncpy(rqdata->method, rc_start, rc_end-rc_start);
					rqdata->method[rc_end-rc_start] = '\0';
					if(strcmp(rqdata->method, "GET")&&strcmp(rqdata->method, "POST")&&strcmp(rqdata->method, "CONNECT")&&strcmp(rqdata->method, "HEAD")){
						perror("unimplemented method\n");
						return -1;
					}
					break;
				}else if(ch>'Z' || ch<'A'){
					perror("invalid method, should constructed with A-Z\n");
					return -1;
				}else break;
			case st_space_after_method:
				if(ch==' '){
					break;
				}else if(ch=='/'){
					state = st_loc;
					rc_start = p-1;
					break;
				}else{
					state = st_uri;
					rc_start = p-1;
					break;
				}
			case st_uri:
				if(ch==':'){
					state = st_colon_after_uri;
					rc_end = p-1;
					break;
				}else if(ch=='/'){
					state = st_loc;
					rc_end = p-1;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
					rc_start = p-1;
					break;
				}else if(isalnum(ch)||ch=='.'||ch=='-'){
					break;
				}else if(ch==' '){
					state = st_space_before_version;
					rc_end = p-1;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
					break;
				}else{
					perror("invalid char in uri");
					return -1;
				}
			case st_colon_after_uri:
				if(ch=='/'){
					state = st_schema_slash;
					strncpy(rqdata->schema, rc_start, rc_end-rc_start);
					rqdata->schema[rc_end-rc_start] = '\0';
					break;
				}else if(isdigit(ch)){
					state = st_port;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
					rc_start = p-1;
					break;
				}else{
					perror("invalid request line, st_colon_after_uri encounter unexpected char\n");
					return -1;
				}
			case st_schema_slash:
				if(ch=='/'){
					state = st_schema_slash_slash;
					break;
				}else{
					perror("invalid request line, <schema> should followed by '://'\n");
					return -1;
				}
			case st_schema_slash_slash:
				if(isalnum(ch)){
					state = st_host;
					rc_start = p-1;
					break;
				}else{
					perror("host name should start with digit or alpha\n");
					return -1;
				}
			case st_host:
				if(isalnum(ch)||ch=='.'||ch=='-'){
					break;
				}else if(ch=='/'){
					state = st_loc;
					rc_end = p-1;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
					rc_start = p-1;
					break;
				}else if(ch==':'){
					state = st_colon_before_port;
					rc_end = p-1;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
					break;
				}else if(ch==' '){
					state = st_space_before_version;
					rc_end = p-1;
					strncpy(rqdata->host, rc_start, rc_end-rc_start);
					rqdata->host[rc_end-rc_start] = '\0';
				}else{
					perror("invalid host name\n");
					return -1;
				}
			case st_colon_before_port:
				if(isdigit(ch)){
					state = st_port;
					rc_start = p-1;
					break;
				}else{
					perror("invalid port\n");
					return -1;
				}
			case st_port:
				if(ch=='/'){
					state = st_loc;
					rc_end = p-1;
					strncpy(rqdata->port, rc_start, rc_end-rc_start);
					rqdata->port[rc_end-rc_start] = '\0';
					rc_start = p-1;
					break;
				}else if(ch==' '){
					state = st_space_before_version;
					rc_end = p-1;
					strncpy(rqdata->port, rc_start, rc_end-rc_start);
					rqdata->port[rc_end-rc_start] = '\0';
					break;
				}else if(isdigit(ch)){
					break;
				}else{
					perror("invalid port\n");
					return -1;
				}
			case st_loc:
				if(ch==' '){
					state = st_space_before_version;
					rc_end = p-1;
					strncpy(rqdata->loc, rc_start, rc_end-rc_start);
					rqdata->loc[rc_end-rc_start] = '\0';
					break;
				}else if(!isspace(ch)){
					break;
				}else{
					perror("invalid loc\n");
					return -1;
				}
			case st_space_before_version:
				if(ch == 'H'){
					state = st_http_H;
					rc_start = p-1;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_H:
				if(ch=='T'){
					state = st_http_HT;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_HT:
				if(ch=='T'){
					state = st_http_HTT;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_HTT:
				if(ch=='P'){
					state = st_http_HTTP;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_HTTP:
				if(ch=='/'){
					state = st_http_slash;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_slash:
				if(isdigit(ch)){
					state = st_http_slash_version;
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_http_slash_version:
				if(isdigit(ch)||ch=='.'){
					break;
				}else if(isspace(ch)){
					state = st_done;
					rc_end = p-1;
					strncpy(rqdata->version, rc_start, rc_end-rc_start);
					rqdata->version[rc_end-rc_start] = '\0';
					break;
				}else{
					perror("invalid HTTP version\n");
					return -1;
				}
			case st_done:
				break;
		}
	}
	if(p==strend){
		switch(state){
			case st_http_slash_version:
				state = st_done;
				rc_end = p;
				strncpy(rqdata->version, rc_start, rc_end-rc_start);
				rqdata->version[rc_end-rc_start] = '\0';
				break;
			case st_done:
				break;
			default:
				printf("break inner parse phase: %d\n", state);
				return -1;
		}
	}else{
		switch(state){
			case st_done:
				break;
			default:
				printf("parse incomplete\n");
				return -1;
		}
	}
	return 0;
}
