#ifndef __PROXY__
#define __PROXY__

#define TYPESTRLEN 96
typedef struct connection_status{
	int keep_alive;
	char transfer_encoding[TYPESTRLEN];
	int client_shuttingdown;
	int server_shuttingdown;
	int content_length;
	char content_type[TYPESTRLEN];
} connection_status_t;
#endif
