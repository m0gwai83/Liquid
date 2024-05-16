#ifndef NET_H
#define NET_H
#define _GNU_SOURCE
#define _REENTRANT

#include <stdio.h>		/* fileno() */
#include <stdlib.h>		/* getenv(), */
#include <stdbool.h>		/* for data-type: bool */
#include <ctype.h>		/* socketpair() */
#include <stdarg.h>		/* for macros: va_list, va_start, va_arg, va_end */
#include <string.h>
#include <malloc.h>		/* calloca() */
#include <errno.h>
#include <unistd.h>		/* getcwd() */
#include <limits.h>
#include <pthread.h>
#include <signal.h>		/* sigaction(), http://opengroup.org/onlinepubs/007908799/xsh/sigsuspend.html */
#include <time.h>
#include <math.h>
#include <syslog.h>

#include <sys/resource.h>
#include <sys/time.h>		/* timeval{} for select() */
#include <sys/wait.h>
#include <sys/stat.h>		/* for S_xxx file mode constants */

#include <sys/types.h>		/* basic system data types */
#include <sys/socketvar.h>
#include <sys/socket.h>		/* basic socket definitions */
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/uio.h>            /* for iovec{} and readv/writev */
#include <sys/un.h> 

#include <arpa/inet.h>		/* inet_pton(), inet_ntop(),  */
#include <netinet/in.h>		/* sockaddr_in{} and other Internet defns */
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/sctp.h>
#include <net/if.h>		/* if_nametoindex(), if_indextoname(), if_nameindex(),
				 * if_freenameindex(), getnameinfo(),  */
#include <netdb.h>		/* getaddrinfo(), freeaddrinfo(), gai_strerror() */

#include <linux/filter.h>    	/* for socket filters */
#include <fcntl.h>		/* for nonblocking */

#include <dirent.h>		/* opendir(), readdir(), rewinddir(), closedir() */

#include <ucontext.h>		/* getcontext(), setcontext(), makecontext(), swapcontext() */


#define NIPQUAD(addr) \
        ((unsigned char *)&addr)[0], \
        ((unsigned char *)&addr)[1], \
        ((unsigned char *)&addr)[2], \
        ((unsigned char *)&addr)[3]
        
#define HIPQUAD(addr) \
        ((unsigned char *)&addr)[3], \
        ((unsigned char *)&addr)[2], \
        ((unsigned char *)&addr)[1], \
        ((unsigned char *)&addr)[0]
        
#if defined(TCP_CORK) && !defined(TCP_NOPUSH)
#define TCP_NOPUSH TCP_CORK
#endif


#define MAX_PARAMS 20

#ifndef __FUNC__
#define __FUNC__ __func__
#endif


#define free(ptr)			free(ptr); ptr=NULL 
#define handle_error_en(en, msg)	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)


struct s_clnt {
	struct s_serv *server;

	int sock;	            
	char *ipaddr;
	u_short port;

	time_t t_accepted;

	struct http_request_header {
	    char *method;
	    char *proto;
	    
	    char *host;
	    char *user_agent;
	    char *accept;
	    char *accept_language;
	    char *accept_encoding;
	    char *accept_charset;
	    char *keep_alive;
	    char *connection;
	    char *cache_control;
	    char *cookie;
	    
	    char *action;
	    char *query_string;
	    size_t content_length;
	} request;
	
	struct http_response_header {
	    char *proto;
	} response;
};

struct s_serv {
    bool init;
    pid_t pid;
    int epfd;
    int numconns;
    pthread_attr_t attr;
    struct {
        char *name;
        char *domain;
        char *ipaddr;
        u_short port;
        int maxconns;
        struct {
	    char  home[200];
	    char *www;
	    char *dtd;
	    char *xsl;
	    char *css;
	    char *img;
	    char *bin;
        } path;
    } config;
    struct {
        bool init;
        pthread_t thread;
    } dispatcher;
    struct {
        bool init;
        pthread_t thread;
    } eventhandler;
    struct {
        int id;
        struct {
            int  family;
	    int  type;
	    int  proto;
	    int backlog;
        } config;
        struct {
	    bool broadcast;		
	    bool nonblock;    		
	    bool nodelay;		
	    bool reuseaddr;		
	    bool keepalive;		
	    bool linger;		
	    bool filter;		
	    bool defaccept;		
	    int  keepidle;
	    int  keepintvl;
	    int  keepcnt;
	    bool quickack;
	    bool nopush;
        } options;
    } socket;
};


extern void 	     *liq_eventhandler( void *arg );
extern void 	      liq_event_cleanuphandler( void *arg );
extern void 	     *liq_dispatcher( void *arg );
extern void 	      liq_dispatcher_cleanuphandler( void *arg );

extern struct s_serv *liq_new( void );
extern int            liq_init( struct s_serv *server );
extern int            liq_shutdown( struct s_serv *server );

extern int   	      liq_httpd( struct s_clnt *client );
extern int   	      liq_http_parse_header( FILE *fd, struct s_clnt *client );

extern char *chomp( char *s );
extern void  hex2ascii( char *str );
extern char  convert( char *hex );
extern char* connect_strings( char* str1, char* str2 );
extern int   liq_set_max_open_files( int num );

extern int liq_epoll_new( int size );
extern int liq_epoll_add( int epfd, int sock, struct s_clnt *client, unsigned int events );
extern int liq_epoll_del( int epfd, struct s_clnt *client );
extern int liq_epoll_close( int epfd );
extern int liq_epoll_wait( int epfd, struct epoll_event *events, int maxconns, int timeout );

extern int liq_socket_get_peerinfo( int sock, char **ipaddr, u_short *port );
extern int liq_socket_set_nonblock( int sock );
extern int liq_socket_set_option( int sock, int level, int option, int value );

extern int liq_socket_new( int family, int level, int proto );
extern int liq_socket_bind( int sock, int family, char *ipaddr, u_short port, bool reuseaddr );
extern int liq_socket_listen( int sock, unsigned int max_listen );
extern int liq_socket_accept( int sock );
extern int liq_socket_close( int sock );
extern int liq_socket_send( int sock, char *buf );

extern char *liq_get_cmd( FILE *fd, size_t size );
extern char *liq_file_getline( FILE *fd, size_t t, struct s_clnt *client );

extern int liq_thread_sigmask( int how, const sigset_t *set, sigset_t *oldset );
extern int liq_thread_sigprocmask( int how, const sigset_t *set, sigset_t *oldset );
extern int liq_thread_kill( pthread_t tid, int sig );
extern int liq_thread_sleep( unsigned int seconds  );
extern int liq_thread_nanosleep( const struct timespec * rqtp , struct timespec * rmtp );

#endif
