#include "net.h"

extern struct s_serv *liq_new( void ) {
    struct s_serv *new_server = (struct s_serv *) malloc(sizeof(struct s_serv));
    if( new_server == NULL ) {
        fprintf(stderr,"liq_new = errno:%d \n", errno);
        fprintf(stderr, "malloc(sizeof(struct serv) failed: "), perror(NULL);
        return NULL;
    } else {
        memset(new_server, 0, sizeof(*new_server));
        new_server->numconns		= 0;
	new_server->init		= false;
	new_server->dispatcher.init	= false;
	new_server->eventhandler.init	= false;
        return new_server;
    }
}

extern int liq_init( struct s_serv *server ) {
    size_t stacksize;

    liq_set_max_open_files( server->config.maxconns );

	  server->pid = getpid();
    server->socket.id = liq_socket_new   ( server->socket.config.family,
    					   server->socket.config.type, 
    				           server->socket.config.proto );
    			liq_socket_bind  ( server->socket.id,
    		 			   server->socket.config.family,
    					   server->config.ipaddr,
    					   server->config.port, 
    					   server->socket.options.reuseaddr );
    			liq_socket_listen( server->socket.id,
    					   server->socket.config.backlog );
    	 server->epfd = liq_epoll_new    ( server->config.maxconns );

    pthread_attr_init(&server->attr);
    pthread_attr_getstacksize(&server->attr, &stacksize);
    
    if( pthread_create(&server->eventhandler.thread, &server->attr, liq_eventhandler, server) ) {
    	fprintf(stderr,"liq_init: pthread_create = errno:%d \n", errno);
	return -1;
    }

    if( pthread_create(&server->dispatcher.thread, &server->attr, liq_dispatcher, server) ) {
    	fprintf(stdout,"liq_init: pthread_create = errno:%d \n", errno);
	return -1;
    }
    
    server->init = true;
    return 0;
}

extern int liq_shutdown( struct s_serv *server ) {   
    pthread_attr_destroy(&server->attr);
    liq_socket_close(server->socket.id);
    liq_epoll_close(server->epfd);
    free(server);
    
    fprintf(stdout,"liq_shutdown = errno:%d \n", errno);
    return 0;
}

extern void *liq_dispatcher( void *arg ) {
    int new_client;
    struct s_serv *server = (struct s_serv *)arg;
    pthread_cleanup_push(liq_dispatcher_cleanuphandler, server);
        
    /* Auf ankommende Client-Verbindungen warten und diese annehmen */
    for( server->dispatcher.init=true ; server->dispatcher.init==true; ) {
    	fprintf(stdout, "liq_dispatcher: waiting for incoming client-connection (max:%d) ... \n", 
			server->config.maxconns);   		   	


	if( (new_client = liq_socket_accept(server->socket.id)) == -1 ) {
	    fprintf(stdout,"liq_dispatcher: liq_socket_accept = errno:%d \n", errno);
	    server->dispatcher.init = false;
	    pthread_exit((void*)EXIT_FAILURE);
	} else {
	    struct s_clnt *client = (struct s_clnt *) malloc(sizeof(struct s_clnt));
	    if( client == NULL ) {
	    	fprintf(stdout,"liq_dispatcher: malloc = errno:%d \n", errno);
	        liq_socket_close(new_client);
    		server->dispatcher.init = false;   
	        pthread_exit((void*)EXIT_FAILURE);
	    } else {
	        memset(client, 0, sizeof(*client));
	        time(&client->t_accepted);        
	        client->sock   = new_client;
	        client->server = server;
	        liq_socket_get_peerinfo(client->sock, &client->ipaddr, &client->port);
    	        liq_socket_set_nonblock(client->sock);       	        

                if( liq_epoll_add(server->epfd, new_client, client, EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP) == -1 ) {
                    liq_socket_close(client->sock);
                    free(client);

    		    server->dispatcher.init = false;
                    pthread_exit((void*)EXIT_FAILURE);
                }
            }
	}
	pthread_testcancel();
    }
    
    pthread_cleanup_pop(0);
    fprintf(stdout, "liq_dispatcher: success ... \n");
    pthread_exit((void*)EXIT_SUCCESS); /*PTHREAD_CANCELED*/
}

extern void liq_dispatcher_cleanuphandler( void *arg ) {
   struct s_serv *server = (struct s_serv *)arg;
   printf("Called liq_dispatcher_cleanuphandler handler \n");
   server->dispatcher.init = false;
}

extern void *liq_eventhandler( void *arg ) {  
    int i, nfds, ret;
    struct s_clnt *client;   
    struct epoll_event *events;
    struct s_serv *server = (struct s_serv *)arg;
    pthread_cleanup_push(liq_event_cleanuphandler, server);
    
    if( (events = (struct epoll_event *) malloc(server->config.maxconns * sizeof(struct epoll_event))) == NULL ) {
    	fprintf(stdout,"liq_eventhandler: malloc");
        pthread_exit((void*)EXIT_FAILURE);
    }

    for(server->eventhandler.init=true; server->eventhandler.init==true;) {
    	fprintf(stdout, "liq_eventhandler: waiting for incoming events ... \n");
    				   
	nfds = liq_epoll_wait(server->epfd, events, server->config.maxconns, -1);
	
    	fprintf(stdout, "liq_eventhandler: received %d events ... \n", nfds);
        for(i=0; i < nfds; i++, events++) {			    
            fprintf(stdout, "liq_eventhandler: handling event %d of %d event(s) ... \n", i+1, nfds);
            client = events->data.ptr;
            
	    /* wenn die verbindung zum client nicht mehr benötigt wird */
	    if( (ret=liq_httpd(client)) <= 0 ) { /* prüfe ob sie geschlossen werden darf */
	        fprintf(stdout, "liq_eventhandler: closing client-connection (ret:%d, sock:%d) ... \n", ret, client->sock);
	        
	        liq_epoll_del(server->epfd, client);
                liq_socket_close(client->sock); /* schließe die verbindung zum client */
                /* noch die einzelnen werte der http_header_request-struktur frei geben */
		free(client);
		
            } else  /* wenn die verbindung zum client noch benötigt wird, dann halte sie offen */
                fprintf(stdout, "liq_eventhandler: keeping client-connection (ret:%d, sock:%d) open ... \n", ret, client->sock);
        }
        pthread_testcancel();

    }

    free(events);
    pthread_cleanup_pop(0);
    fprintf(stdout, "liq_eventhandler: success ... \n");
    pthread_exit((void*)EXIT_SUCCESS);
}

extern void liq_event_cleanuphandler( void *arg ) {
   struct s_serv *server = (struct s_serv *)arg;
   printf("Called liq_event_cleanuphandler handler \n");
   server->eventhandler.init=false;
}

extern int liq_httpd( struct s_clnt *client ) {   
    int ret;
    FILE *fd = fdopen(client->sock, "r");
    if( fd == NULL ) {
        fprintf(stdout,"liq_httpd: fdopen failed ... \n");
        return -1;
    }
    
    if( (ret = liq_http_parse_header(fd, client)) < 0 ) {
                        fprintf(stdout, "error in liq_httpd( struct s_clnt *client ) \n");
        if( ret == -1 ) fprintf(stdout, "CONNECTION CLOSED BY PEER, RESOURCE TEMPORARILY UNAVAILABLE, ...  \n");
        if( ret == -2 ) fprintf(stdout, "HTTP SYNTAX ERROR \n");
        if( ret == -3 ) fprintf(stdout, "TARGET URL NOT FOUND \n");
        if( ret == -4 ) fprintf(stdout, "PROTOCOL NOT FOUND \n");
        if( ret == -5 ) fprintf(stdout, "METHOD NOT SUPPORTED \n");
        
        /*my_free(client,"fd",fd);*/
        return ret;
    } errno = 0;
    
    fprintf(stdout, "---- received client-information ---- \n");    
    fprintf(stdout, "sock: '%d' \n", 		client->sock);
    fprintf(stdout, "port: '%d' \n", 		client->port);
    fprintf(stdout, "ipaddr: '%s' \n", 		client->ipaddr);
    fprintf(stdout, "accepted at: '%s' \n", 	chomp(ctime(&client->t_accepted)));
    
    fprintf(stdout, "method: '%s' \n", 		client->request.method);
    fprintf(stdout, "proto: '%s' \n", 		client->request.proto);
    
    fprintf(stdout, "host: '%s' \n", 		client->request.host);
    fprintf(stdout, "user-agent: '%s' \n", 	client->request.user_agent);
    fprintf(stdout, "accept: '%s' \n", 		client->request.accept);
    fprintf(stdout, "accept-language: '%s' \n", client->request.accept_language);
    fprintf(stdout, "accept-encoding: '%s' \n", client->request.accept_encoding);
    fprintf(stdout, "accept-charset: '%s' \n", 	client->request.accept_charset);
    fprintf(stdout, "keep-alive: '%s' \n", 	client->request.keep_alive);
    fprintf(stdout, "connection: '%s' \n", 	client->request.connection);
    fprintf(stdout, "cache-control: '%s' \n",	client->request.cache_control);
    fprintf(stdout, "cookie: '%s' \n", 		client->request.cookie);
    
    fprintf(stdout, "action: '%s' \n", 		client->request.action);
    fprintf(stdout, "query_string: '%s' \n", 	client->request.query_string);   
    fprintf(stdout, "content_length: '%d' \n", 	client->request.content_length);
    fprintf(stdout, "------------------------------------------- \n"); 
    
    /*
    if(client->request.method != NULL) my_free(client,"method",client->request.method);
    if(client->request.cookie != NULL) my_free(client,"cookie",client->request.cookie);
    */
    
    /*my_free(client,"fd",fd);*/
    fprintf(stdout, "liq_httpd: errno:%d ... \n", errno);
    return 0; /* SUCCESS: CLOSE CONNECTION */
}

extern int liq_http_parse_header( FILE *fd, struct s_clnt *client ) {
    char *line = NULL;
    char *attribute = NULL;
    char *url = NULL;
        
    /* HTTP-Request (method, url, proto) auslesen */
    if( (line = liq_file_getline(fd, 0, client)) == NULL ) { /* für fehler beim lesen vom client */
        fprintf(stdout,"liq_file_getline\n");
        return -1;
    } else {
        /*fprintf(stdout, "received request-line: '%s' \n", line);*/
        
        client->request.method  = strtok(line," ");       
        url    		        = strtok(NULL," ");
        client->request.proto   = strtok(NULL," ");

	line=NULL;

        if( client->request.method == NULL )	return -2; 
        if( url == NULL ) 			return -3; 
        if( client->request.proto == NULL ) 	return -4; 
    }
  
    
    /* HTTP-Header auslesen */
    while( (line = liq_file_getline(fd, 0, client)) != NULL && strlen(line) != 0 ) {
    	/* fprintf(stdout, "[len:%d] %s \n", strlen(line), line); */
    	
    	attribute = strtok(line," ");
    	     if( strcmp(attribute,"Host:")		==0 )	client->request.host 	     	= strtok(NULL,"\n");
    	else if( strcmp(attribute,"User-Agent:")	==0 )	client->request.user_agent  	= strtok(NULL,"\n");
    	else if( strcmp(attribute,"Accept:")		==0 )	client->request.accept	     	= strtok(NULL,"\n");
    	else if( strcmp(attribute,"Accept-Language:")	==0 )	client->request.accept_language = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Accept-Encoding:")	==0 )	client->request.accept_encoding = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Accept-Charset:")	==0 )	client->request.accept_charset  = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Keep-Alive:")	==0 )	client->request.keep_alive      = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Connection:")	==0 )	client->request.connection      = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Cache-Control:")	==0 )	client->request.cache_control   = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Cookie:")		==0 )	client->request.cookie          = strtok(NULL,"\n");
    	else if( strcmp(attribute,"Content-Length:")	==0 )	client->request.content_length  = atoi(strtok(NULL,"\n"));
    	else fprintf(stdout, "FOUND UNKNOWN ATTRIB IN HTTP-HEADER: [%d] '%s'='%s' \n",
    			     strlen(attribute)+strlen(line)+1, attribute, line);
    }
    line=NULL;
    attribute=NULL;

    /* HTTP-Request: URL auslesen */
    if( strcmp(client->request.method,"GET")==0 ) { 	
    	client->request.action       = strtok(url, "\?");
    	client->request.query_string = strtok(NULL,"\n");
        if( client->request.query_string != NULL )
    	    client->request.content_length = strlen(client->request.query_string);
    	    
    	url = NULL; /* my_free(client,"url",url) */

    } else if( strcmp(client->request.method,"POST")==0 ) {
    	strcpy(client->request.action,url);
    	url = NULL; /* my_free(client,"url",url) */
    	client->request.query_string = liq_file_getline(fd, client->request.content_length,client);
    	
    } else 
    	return -5;
    
    return 0;
}

extern char *chomp( char *s ) {
    int end = strlen(s) - 1;
    if( end >= 0 && s[end] == '\n' ) {
        s[end] = '\0';
        if( s[end-1] == '\r' )
            s[end-1] = '\0';
    }
    return s;
}

extern char* connect_strings( char* str1, char* str2 ) {
  if( str1 == NULL || str2 == NULL ) {
        fprintf(stderr, "Fehler: Ein (oder beide) bergebenen Strings sind NULL! \n");
        exit(1);

  } else {
        char *new_string = calloc( strlen(str1)+strlen(str2)+1, sizeof(char) );
        if(new_string == NULL) {
            fprintf(stderr, "Fehler: Konnte keinen Speicherplatz der Größe %d+1 reservieren! \n",
            	    strlen(str1)+strlen(str2));
            exit(1);
        }

	strcpy(new_string, str1);
	strcat(new_string, str2);
	return new_string;
  }
}

extern char *liq_get_cmd( FILE *fd, size_t size ) {
    int nread;
    char *line;
    
    fprintf(stdout, "cmd: ");
    /* These 2 lines are the heart of the program. */
    line = (char *) malloc(size+1);
    if( line == NULL ) {
    	fprintf(stdout, "error %d in liq_file_getline(%d, %d) \n", errno, fileno(fd), size);
    	fprintf(stdout, "can't allocate memory for request-line \n");
    	return NULL;
    }

    nread = getline (&line, &size, fd);
    return line;
}

extern char *liq_file_getline( FILE *fd, size_t t, struct s_clnt *client ) {
    int nread;
    char *r;
    
    /*fprintf(stdout, "liq_file_getline(%d): trying to read a line ... \n", fileno(fd));*/
    
    /* These 2 lines are the heart of the program. */
    r = (char *) malloc(t+1);
    if( r == NULL ) {
    	fprintf(stdout, "error %d in liq_file_getline(%d, %d) \n", errno, fileno(fd), t);
    	fprintf(stdout, "can't allocate memory for request-line \n");
    	return NULL;
    }

    nread = getline (&r, &t, fd);
    
    /*fprintf(stdout, "status: %d, t:%d \n", nread, t);*/
            
    /* FAILURE: INVALID SEEK */
    if( nread == -1 && errno == 29 ) {
        fprintf(stdout, "error %d in liq_file_getline(%d, %d) \n", errno, fileno(fd), t);
        fprintf(stdout, "getline(&r, &t, %d) failed: ", fileno(fd)), perror(NULL);
        fprintf(stdout, "client-connection closed by peer ...\n");
        free(r);
        return NULL;
    }

    /* FAILURE: RESOURCE TEMPORARILY UNAVAILABLE */    
    if( nread == -1 && errno == 11 ) {
        fprintf(stdout, "error %d in liq_file_getline(%d, %d) \n", errno, fileno(fd), t);
        fprintf(stdout, "getline(&r, &t, %d) failed: ", fileno(fd)), perror(NULL);
        fprintf(stdout, "client-connection temporarily unavailable ... \n");
        
        /* ERNEUT VERSUCHEN UM VOM CLIENT ZU LESEN 
        {
	    	int i, retries;
	        for(i=0, retries=3; i<retries; i++) {
	            fprintf(stderr, "getline: retry %d of %d  ... \n", i+1, retries);
	            nread = getline (&r, &t, fd);
	            fprintf(stderr, "part of request-line received: '%s' (nread:%d) ... \n", r, nread);
		}
	}*/
        
        free(r);
        return NULL;
    }
    
    /* FAILURE: UNKNOWN ERROR AT CONNECTION */    
    if( nread == -1 && errno > 0 ) {
        fprintf(stdout, "error %d in liq_file_getline(%d, %d) \n", errno, fileno(fd), t);
        fprintf(stdout, "getline(&r, &t, %d) failed: ", fileno(fd)), perror(NULL);
        fprintf(stdout, "unknown error at client-connection ... \n");
        free(r);
        return NULL;
    }
    
    /* SUCCESS: CONNECTION HANDLED */
	/* add_slashes(r); 
	hex2ascii(&r);
	unescape(&r);*/
	
        return chomp(r);
}

/* Anweisung: Wandelt einzelne Hexzeichen (%xx) in ASCII-Zeichen
 * und kodierte Leerzeichen (+) in echte Leerzeichen um */
extern void hex2ascii( char *str )  {
   int x, y;
   
   for(x=0,y=0; str[y] != '\0'; ++x,++y) {
      str[x] = str[y];
      
      /* Ein Hexadezimales Zeichen? */
      if(str[x] == '%')  {
         str[x] = convert(&str[y+1]);
         y += 2;
      }
      
      /* Ein Leerzeichen? */
      else if( str[x] == '+') str[x]=' ';
   }
   /* Geparsten String sauber terminieren */
   str[x] = '\0';
}

/* Funktion konvertiert einen String von zwei hexadezimalen
 * Zeichen und gibt das einzelne dafür stehende Zeichen zurck */
extern char convert( char *hex ) {
   char ascii;
   /* erster Hexawert */
   ascii =
   (hex[0] >= 'A' ? ((hex[0] & 0xdf) - 'A')+10 : (hex[0] - '0'));  
   ascii <<= 4; /* Bitverschiebung schneller als ascii*=16 */
   /* zweiter Hexawert */
   ascii +=
   (hex[1] >= 'A' ? ((hex[1] & 0xdf) - 'A')+10 : (hex[1] - '0'));  
   return ascii;
}


extern int liq_epoll_new( int size ) {
    int epfd = epoll_create(size);
    if( epfd == -1 ) {
    	fprintf(stderr, "error %d in liq_epoll_new(%d) \n", errno, size);
    	fprintf(stderr, "epoll_create(%d) failed: ", size), perror(NULL);
        return -1;
    }
    return epfd;
}

extern int liq_epoll_add( int epfd, int sock, struct s_clnt *client, unsigned int events ) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events   = events;
    ev.data.ptr = client;
    if( epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) < 0 ) {
    	fprintf(stdout, "error %d in liq_epoll_add(%d, %d, client, events) \n", errno, epfd, sock);
    	fprintf(stdout, "epoll_ctl(%d, EPOLL_CTL_ADD, %d, &ev) failed: ", epfd, sock), perror(NULL);
        return -1;
    }
    return 0;
}

extern int liq_epoll_del( int epfd, struct s_clnt *client ) {
    struct epoll_event ev; 
    memset(&ev, 0, sizeof(ev));
    if( epoll_ctl(epfd, EPOLL_CTL_DEL, client->sock, &ev) < 0 ) {
    	fprintf(stdout, "error %d in liq_epoll_del(%d, %d) \n", errno, epfd, client->sock);
    	fprintf(stdout, "epoll_ctl(%d, EPOLL_CTL_DEL, %d, &ev) failed: ",
    	    	    	epfd, client->sock), perror(NULL);
	return -1;
    }
    return 0;
}

extern int liq_epoll_close( int epfd ) {
    if( close(epfd) < 0 ) {
	fprintf(stderr, "error %d in liq_epoll_close(%d) \n", errno, epfd );
	fprintf(stderr, "close(%d) failed: ", epfd), perror(NULL);
	return -1;
    }
    return 0;
}

/* Wait for events on an epoll instance "epfd". Returns the number of
   triggered events returned in "events" buffer. Or -1 in case of
   error with the "errno" variable set to the specific error code. The
   "events" parameter is a buffer that will contain triggered
   events. The "maxevents" is the maximum number of events to be
   returned ( usually size of "events" ). The "timeout" parameter
   specifies the maximum wait time in milliseconds (-1 == infinite). */
extern int liq_epoll_wait( int epfd, struct epoll_event *events, int maxconns, int timeout ) {
    	int nfds = epoll_wait(epfd, events, maxconns, timeout);
        if( nfds == -1 ) {
    	    fprintf(stderr, "error %d in liq_epoll_wait(%d, struct epoll_event *events, %d, %d) \n",
    	    		    errno, epfd, maxconns, timeout);
    	    fprintf(stderr, "epoll_wait(%d, struct epoll_event *events, %d, %d) failed: ",
    	    		    epfd, maxconns, timeout), perror(NULL);
            return -1;
        }
        return nfds;
}


extern int liq_socket_get_peerinfo( int sock, char **ipaddr, u_short *port ) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if( getpeername(sock, (struct sockaddr *)&addr, &addrlen) < 0 ) {
    	fprintf(stderr, "error %d in liq_socket_get_peerinfo(%d, \"%s\", %d) \n", errno, sock, *ipaddr, *port);
        fprintf(stderr, "getpeername(%d, (struct sockaddr *)&addr, &addrlen) failed: ", sock), perror(NULL);
	return -1;
    } else {
    	/* (Quick note: the old way of doing things used a function called inet_addr() or another
    	 * function called inet_aton(); these are now obsolete and don't work with IPv6.)
    	 *
    	 * inet_ntop() ("ntop" means "network to presentation"—you can call it 
    	 * "network to printable" if that's easier to remember). */
	   *ipaddr = inet_ntoa(addr.sin_addr);
	   *port   = ntohs(addr.sin_port);

        return 0;
    }
}

extern int liq_socket_set_nonblock( int sock ) {
    int flags = 1;
    if( ioctl(sock, FIONBIO, &flags) &&
        ((flags = fcntl(sock, F_GETFL, 0)) < 0 ||
        fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)) {
    	fprintf(stderr, "error %d in liq_socket_set_nonblock(%d) \n", errno, sock);
        fprintf(stderr, "ioctl(%d, FIONBIO, &flags) && \n"
        		"((%d = fcntl(%d, F_GETFL, 0)) < 0 || \n"
        		"fcntl(%d, F_SETFL, %d | O_NONBLOCK) < 0) \n"
        		"failed: ", sock, flags, sock, sock, flags), perror(NULL);
	return -1;
    }
    fprintf(stdout, "liq_socket_set_nonblock(%d) success ... \n", sock);
    return 0;
}



extern int liq_socket_set_option( int sock, int level, int option, int value ) {
    if( setsockopt(sock, level, option, &value, sizeof(value)) == -1 ) {
    	fprintf(stderr, "error %d in liq_socket_set_option(%d, %d, %d, %d) \n",
    			errno, sock, level, option, value);
        fprintf(stderr, "setsockopt(%d, %d, %d, %d, %d) failed: ", 
        		sock, level, option, value, sizeof(value)), perror(NULL);
	return -1;
    }
    fprintf(stdout, "liq_socket_set_option(%d, %d, %d, %d) success ... \n",
     		    sock, level, option, value);
    return 0;
}


    /* 
    struct hostent *he;
    struct servent *sp;   
   
    UDP/IPv4	-> broadcast
    sockoption	-> SO_BROADCAST 	// muss gesetzt werden bevor broadcast-nachrichten 
    					// mit sendto() verschickt werden können
    UDP/IPv6	-> multicast
    
    // IPv4-Sockets:
    struct in_addr  {			// Internet address (a structure for historical reasons)
    	uint32_t 	   s_addr; 	// that's a 32-bit int (4 bytes)
    };
    	
    struct sockaddr_in {
	short int          sin_family;  // Address family, AF_INET
	unsigned short int sin_port;    // Port number
	struct in_addr     sin_addr;    // Internet address
	unsigned char      sin_zero[8]; // Same size as struct sockaddr
    };
    
    struct in6_addr {
        unsigned char   s6_addr[16];   // IPv6 address
    };  
    	
    struct sockaddr_in6 {
        u_int16_t       sin6_family;   // address family, AF_INET6
        u_int16_t       sin6_port;     // port number, Network Byte Order
        u_int32_t       sin6_flowinfo; // IPv6 flow information
        struct in6_addr sin6_addr;     // IPv6 address
        u_int32_t       sin6_scope_id; // Scope ID
    };

     You'll load this struct up a bit, and then call getaddrinfo(). It'll return a pointer 
     to a new linked list of these structures filled out with all the goodies you need. 
    
     You can force it to use IPv4 or IPv6 in the ai_family field, or leave it as AF_UNSPEC 
     to use whatever. This is cool because your code can be IP version-agnostic. 
         
        struct addrinfo {
            int              ai_flags;     		// AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST, ...
            int              ai_family;    		// AF_INET, AF_INET6, AF_UNSPEC
            int              ai_socktype;  		// SOCK_STREAM, SOCK_DGRAM
            int              ai_protocol;  		// use 0 for "any"
            size_t           ai_addrlen;   		// size of ai_addr in bytes
            struct sockaddr {
	        unsigned short    sa_family;    	// address family, AF_xxx 
	        char              sa_data[14];  	// 14 bytes of protocol address
	    } *ai_addr;      	    			// struct sockaddr_in or _in6
            char            *ai_canonname; 		// full canonical hostname
            struct addrinfo *ai_next;      		// linked list, next node
        }
    */
extern int liq_socket_new( int /* domain */ family, int level, int proto ) {
	/*  	
        int sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	*/
	int sock = socket(family, level, proto);
	if( sock == -1 ) {
	    fprintf(stderr, "error %d in liq_socket_new(%d, %d, %d) \n", errno, family, level, proto);
	    fprintf(stderr, "socket(%d, %d, %d) failed: ", family, level, proto), perror(NULL);
	    return -1;
	}
	fprintf(stdout, "liq_socket_new(%d, %d, %d) success ... \n", family, level, proto);
	return sock;
}

/*	htons()		host to network short
	htonl()		host to network long
	ntohs()		network to host short
	ntohl()		network to host long */
extern int liq_socket_bind( int sock, int family, char *ipaddr, u_short port, bool reuseaddr ) {
	/* Instead of inet_aton() or inet_addr(), use inet_pton().
	 * Instead of inet_ntoa(), use inet_ntop().
	 * Instead of gethostbyname(), use the superior getaddrinfo().
	 * Instead of gethostbyaddr(), use the superior getnameinfo() (although gethostbyaddr() can still work with IPv6).
	 * INADDR_BROADCAST no longer works. Use IPv6 multicast instead.
	 * inet_pton() returns -1 on error, or 0 if the address is messed
	 * up. So check to make sure the result is greater than 0.
	inet_pton(AF_INET, "192.168.10.110", &addr.sin_addr);		// IPv4
	addr.sin_addr.s_addr = INADDR_ANY;
	 *
	inet_pton(AF_INET6, "_:_:_:_::_",    &(addr6.sin6_addr)); 	// IPv6
	addr6.sin6_flowinfo  = 0;
	addr6.sin6_addr      = in6addr_any;
	 *
	 * the value IN6ADDR_ANY_INIT can be used as an initializer 
	 * when the struct in6_addr is declared:
	struct in6_addr ia6 = IN6ADDR_ANY_INIT;
	*/

	/* Bind to the wildcard address (all) and port:
	bind(sock, (struct sockaddr*)&ip4addr, sizeof ip4addr);
	bind(sock, (struct sockaddr*)&ip6addr, sizeof ip6addr);
	*
	* Bind it to the port we passed in to getaddrinfo():
	bind(sock, res->ai_addr, res->ai_addrlen);
	*/
	
    	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family           = family;
	if( ipaddr != NULL )
	     addr.sin_addr.s_addr = inet_addr(ipaddr);
	else addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port 		  = htons(port);	
	
        if( reuseaddr == true ) liq_socket_set_option(sock, SOL_SOCKET, SO_REUSEADDR, 1);
	
	if( bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ) {
	    fprintf(stderr, "error %d in liq_socket_bind(%d, %d, %d) \n", errno, sock, family, port);
	    fprintf(stderr, "bind(%d, (struct sockaddr *)&addr, %d) failed: ", 
	    		    sock, sizeof(addr)), perror(NULL);
	    return -1;
	}
	fprintf(stdout, "liq_socket_bind(%d, %d, %d) success ... \n", sock, family, port);
	return 0;
}

extern int liq_socket_listen( int sock, unsigned int max_listen ) {
	/* Finally, when we're eventually all done with the linked list that getaddrinfo() so
	 * allocated for us, we can (and should) free it all up with a call to freeaddrinfo(). 
	freeaddrinfo(servinfo);	// free the linked-list
	*/
	
	if( listen(sock, max_listen) < 0 ) {
	    fprintf(stderr, "error %d in liq_socket_listen(%d, %d) \n", errno, sock, max_listen);
	    fprintf(stderr, "listen(%d, %d) failed: ", sock, max_listen), perror(NULL);
	    return -1;
	}
	fprintf(stdout, "liq_socket_listen(%d, %d) success ... \n", sock, max_listen);
	return 0;
}

extern int liq_socket_accept( int sock ) {
    	int new_sock;

	/* IPv4:
	char ip4[INET_ADDRSTRLEN];  	// space to hold the IPv4 string
	struct sockaddr_in addr;    	// pretend this is loaded with something
	inet_ntop(AF_INET, &(addr.sin_addr), ip4, INET_ADDRSTRLEN);
	printf("The IPv4 address is: %s\n", ip4); */
	
	/* IPv6:
	char ip6[INET6_ADDRSTRLEN]; 	// space to hold the IPv6 string
	struct sockaddr_in6 addr6;    	/// pretend this is loaded with something
	inet_ntop(AF_INET6, &(addr6.sin6_addr), ip6, INET6_ADDRSTRLEN);
	printf("The address is: %s\n", ip6);*/
	
	    
        /*char ipaddr[INET6_ADDRSTRLEN];
    	struct sockaddr_storage addr;
    	socklen_t addrlen = sizeof(addr);*/
    
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	
	/* int new_sock = accept(sock, (struct sockaddr *)0, (size_t *)0); */
	while( (new_sock = accept(sock, (struct sockaddr *) &addr, &addrlen)) == -1 ) {
            fprintf(stderr, "error %d in liq_socket_accept(%d) \n", errno, sock);
            fprintf(stderr, "accept(%d, (struct sockaddr *)0, (size_t *)0) failed: ",
            		    sock), perror(NULL);
    	    if( errno == EINTR )
		 continue;
	    if( errno != EAGAIN && errno != EWOULDBLOCK )
		 return -1;
	}
	
	/* 
	inet_ntop( addr.ss_family, get_in_addr((struct sockaddr *)&addr), ipaddr, INET6_ADDRSTRLEN );	
	fprintf(stdout, "NEW CONNECTION FROM: %s \n", ipaddr);
	inet_ntop( addr.ss_family, get_in_addr((struct sockaddr *)&addr), ipaddr, sizeof ipaddr );
	*/
        fprintf(stdout, "new client-connection (addr:%s:%d sock:%d) established ... \n",
	                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), new_sock);
	
        return new_sock;
}

extern int liq_socket_close( int sock ) {
	if( close(sock) < 0 ) {
	    fprintf(stderr, "error %d in liq_socket_close(%d) \n", errno, sock);
	    fprintf(stderr, "close(%d) failed: ", sock), perror(NULL);
	    return -1;
	}
	return 0;
}

/* ERRORS:
EAGAIN Insufficient  resources  to  create another thread, or a system-
      imposed limit on the number of  threads  was  encountered.   The
      latter  case  may  occur  in  two  ways:  the  RLIMIT_NPROC soft
      resource limit (set via setrlimit(2)), which limits  the  number
      of process for a real user ID, was reached; or the kernel's sys-
      tem-wide  limit  on  the  number  of   threads,   /proc/sys/ker-
      nel/threads-max, was reached. */
extern int liq_set_max_open_files( int num ) {
    struct rlimit open_files;
    memset(&open_files, 0, sizeof(open_files));
    open_files.rlim_max = open_files.rlim_cur = num+50;
    if( setrlimit(RLIMIT_NOFILE, &open_files) == -1 ) {
    	fprintf(stderr, "error %d in liq_set_max_open_files(%d) \n", errno, num);
    	fprintf(stderr, "setrlimit(RLIMIT_NOFILE, &open_files) failed: "), perror(NULL);
        fprintf(stderr, "Hinweis: Eine Schranke kann die Anzahl der maximal geöffneten Dateien sein. \n"
	       "Dabei gibt es zwei Werte. Der eine beschränkt die Gesamtzahl der offenen Dateien \n"
	       "und der andere die Anzahl der Dateien, die ein einzelner Prozess öffnen darf. \n\n");
        fprintf(stderr, "Bei einer Linux-Standardinstallation wird die Anzahl der gleichzeitig offenen Dateien \n"
               "auf 1024 begrenzt. Die Einstellung befindet sich in der Datei /usr/src/linux/include/linux/fs.h \n"
               "und heißt NR_FILES. NR_INODES ist um den gleichen Faktor zu erhöhen. Eine Neubildung des Kernels ist \n"
               "notwendig, um den Parameter zu erhöhen. Dieser Wert scheint zunächst hoch. Allerdings kann in einer \n"
               "Serverumgebung mit 200 Benutzern jeder Anwender nur noch fünf Dateien öffnen. \n");
	return -1;
    }
    
    /* http://www.ibm.com/developerworks/linux/library/l-hisock.html?ca=dgr-lnxw961BoostSocket 
    *  Weitere Konfigurations-Möglichkeiten über:
    *    /proc/sys/fs/file-max = 65535
    *    /proc/sys/net/ipv4/tcp_fin_timeout = 15
    *    /proc/sys/net/ipv4/tcp_max_fin_backlog = 16384
    *    /proc/sys/net/ipv4/tcp_tw_reuse = 1
    *    /proc/sys/net/ipv4/tcp_tw_recycle = 1
    *    /proc/sys/net/ipv4/ip_local_port_range = 1024 65535
    *    ulimit -n 65536
    *
    *  server->listen: you might need to increase "net.ipv4.tcp_max_syn_backlog" to use this value
    */
    fprintf(stdout, "minimum/maximum number of files that can be open at once: %d/%d \n", FOPEN_MAX, num);    
    fprintf(stdout, "liq_set_max_open_files(%d) success ... \n", num);
    return 0;
}

/* Examine and change mask of blocked signals. this function is just like sigprocmask(2),
 * with the difference that its use in multithreaded programs is explicitly specified by
 * POSIX.1-2001.  Other differences are noted in this page.
 *
 * notes: A new thread inherits a copy of its creator's signal mask.  On success,
 *	  pthread_sigmask() returns 0; on error, it returns an error number. 
 *
 * see also: 
 * sigaction(2), sigpending(2), sigprocmask(2) pthread_create(3),
 * pthread_kill(3), sigsetops(3), pthreads(7), signal(7) */
extern int liq_thread_sigmask( int how, const sigset_t *set, sigset_t *oldset ) {
    int ret = pthread_sigmask(how, set, oldset);
    if( ret == 0 )
        return 0;
    else return ret;
}

/* This function is used to fetch and/or change the signal mask of the calling thread.  
 * The signal mask is the set of signals whose delivery is currently blocked for the caller 
 * (see also signal(7) for more details).
 *
 * The behavior of the call is dependent on the value of how, as follows.
 *
 * SIG_BLOCK	The set of blocked signals is the union of the current set and the set argument.
 * SIG_UNBLOCK	The signals in set are removed from the current set of blocked signals.
 *             	It is permissible to attempt to unblock a signal which is not blocked.
 * SIG_SETMASK	The set of blocked signals is set to the argument set.
 *		If oldset is non-null, the previous value of the signal mask is stored in oldset.
 *		If set is NULL, then the signal mask is unchanged (i.e., how is ignored), but the 
 *		current value of the signal mask is nevertheless returned in oldset (if it is not NULL).
 *
 * The use of sigprocmask() is unspecified in a multithreaded process; see pthread_sigmask(3). If SIGBUS,
 * SIGFPE, SIGILL, or SIGSEGV are generated while they are blocked, the result is undefined, unless the 
 * signal was generated by the kill(2), sigqueue(2), or raise(3). */
extern int liq_thread_sigprocmask( int how, const sigset_t *set, sigset_t *oldset ) {
    int ret = sigprocmask(how, set, oldset);
    if( ret == 0 )
        return 0;
    else return -1;
}

/* The pthread_kill() function sends the signal sig to thread, another thread in the same process as the 
 * caller. The signal is asynchronously directed to thread. If sig is 0, then no signal is  sent, but error 
 * checking is still performed; this can be used to check for the existence of a  thread ID. On success, 
 * pthread_kill() returns 0; on error, it returns an error number, and no signal is sent. */
extern int liq_thread_kill( pthread_t tid, int sig ) {
    int ret = pthread_kill(tid, sig);
    if( ret == 0 )
        return 0;
    else return ret;	
}

/* The sleep function delays the execution of the calling thread by at least the given number of seconds. */
extern int liq_thread_sleep( unsigned int seconds  ) {
    int ret = sleep(seconds);
    if( ret == 0 )
         return 0;
    else return -1;
}

/* The nanosleep function delays the execution of the calling thread until either the time interval 
 * given by rtqp has elapsed or a signal is handled by the thread.
 *
 * The nanosleep function returns zero to indicate the given time interval has elapsed. Otherwise it returns -1 
 * to indicate that the delay has been interrupted, and sets rmtp to the time interval remaining. */
extern int liq_thread_nanosleep( const struct timespec * rqtp , struct timespec * rmtp ) {
    int ret = nanosleep(rqtp , rmtp);
    if( ret == 0 )
         return 0;
    else return -1;
}

