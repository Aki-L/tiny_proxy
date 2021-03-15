#ifndef __PROXY__
#define __PROXY__

#define TYPESTRLEN 96
typedef struct connection_status{
	int server_setclose;
	int client_setclose;
	char client_transfer_encoding[TYPESTRLEN];
	char server_transfer_encoding[TYPESTRLEN];
	char client_trailer[TYPESTRLEN];
	char server_trailer[TYPESTRLEN];
	int client_shuttingdown;
	int server_shuttingdown;
	int client_content_length;
	int server_content_length;
	char content_type[TYPESTRLEN];
} connection_status_t;
#endif
