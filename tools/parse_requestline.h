#ifndef PARSE_RQLINE
#define PARSE_RQLINE
#define RQ_DATALEN 1024
typedef struct requestline_data{
        char method[RQ_DATALEN];
        char schema[RQ_DATALEN];
        char host[RQ_DATALEN];
        char port[RQ_DATALEN];
        char loc[RQ_DATALEN];
        char version[RQ_DATALEN];
} requestline_data_t;
typedef enum parse_state{
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
int parse_requestline(char* rqline, requestline_data_t *rqdata);

#endif
