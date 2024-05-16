/* compile: gcc -o liquid main.c -lepoll -lpthread */
#define _GNU_SOURCE
#define _REENTRANT
	        			
#include "inc/net.h"

int main( int argc, char **argv /*, char **envp*/ ) {
    struct s_serv *server;
    
    puts( "Liquid ICS ( Internet-Community-Software | ALPHA VERSION )" );
    puts( "(C)Copyright 1995-2009, m0gwai@arcor.de\n" );

    if((server=liq_new())==NULL) pthread_exit(NULL);
    /* *************** DEFAULT SERVER-CONFIG *************** */
    server->config.name			= "liquid-httpd";
    server->config.domain		= "www.liquid-chat.de";
    server->config.ipaddr		= "127.0.0.1";
    server->config.port			= 2000;
    server->config.maxconns		= 20000;
    strcpy(server->config.path.home,"./liquid/");
    server->config.path.www		= "./www/";
    server->config.path.dtd		= "./www/dtd/";
    server->config.path.xsl		= "./www/xsl/";
    server->config.path.css		= "./www/css/";
    server->config.path.img		= "./www/img/";
    server->config.path.bin		= "./www/bin/";
    /* ***************************************************** */
    server->socket.config.family	     = AF_INET;
    server->socket.config.type		= SOCK_STREAM;
    server->socket.config.proto		= IPPROTO_TCP;
    server->socket.config.backlog	     = 2048;
    server->socket.options.reuseaddr	= true;
    server->socket.options.broadcast	= false;
    server->socket.options.nonblock	= false;
    server->socket.options.nodelay	     = false;
    server->socket.options.keepalive	= false;
    server->socket.options.linger	     = false;
    server->socket.options.filter	     = false;
    server->socket.options.defaccept	= false;
    server->socket.options.keepidle	= -1;
    server->socket.options.keepintvl	= -1;
    server->socket.options.keepcnt	     = -1;
    server->socket.options.quickack	= false;
    server->socket.options.nopush	     = false;
    /* ***************************************************** */
    liq_init(server);

    for(;server->init==true;) {   			
    	 char *cmd = chomp(liq_get_cmd(stdin,50));
 	 if( cmd == NULL || strlen(cmd)==0 ) {
 	 	printf("help: please enter valid cmd or 'help' for more info! \n");

 	 } else if( strcmp(chomp(cmd),"help")==0) {
 	 	printf( "valid commands: \n"
 	 		"help \t - this help \n"
 	 		"info \t - server-info \n"
 	 		"quit \t - shutdown server \n");
    			 	 	
 	 } else if( strcmp(chomp(cmd),"info")==0) {
 	 	printf("server->config.name: '%s' \n", server->config.name);
 		printf("server->init: %d \t server->dispatcher.init: %d \t server->eventhandler.init: %d \n",
    			server->init, 	    server->dispatcher.init, 	   server->eventhandler.init );

 	 } else if( strcmp(chomp(cmd),"quit")==0) {
		/* BEIDE THREADS CANCELN UND DEN SERVER HERUNTER FAHREN */
	    	int s; void *res;
	    	if( server->eventhandler.init == true ) {
		    printf("Canceling thread\n");
		    if( (s = pthread_cancel(server->eventhandler.thread)) != 0 )
		       handle_error_en(s, "pthread_cancel");
		    else if( (s = pthread_join(server->eventhandler.thread, &res)) != 0 )
		         handle_error_en(s, "pthread_join");	
		    else if( res == PTHREAD_CANCELED )
		         printf("Thread was canceled; retval:%d \n", s);
		    else printf("Thread terminated normally; retval:%d \n", s);
		}
		
	    	if( server->dispatcher.init == true ) {
		    printf("Canceling thread\n");
		    if( (s = pthread_cancel(server->dispatcher.thread)) != 0 )
		         handle_error_en(s, "pthread_cancel");
		    else if( (s = pthread_join(server->dispatcher.thread, &res)) != 0 )
		         handle_error_en(s, "pthread_join");
		    else if( res == PTHREAD_CANCELED )
		         printf("Thread was canceled; retval:%d \n", s);
		    else printf("Thread terminated normally; retval:%d \n", s);
		}   	        			
 	 } else 
 	        printf("error: unknown cmd! \n");

    	 free(cmd);
    	 if( server->dispatcher.init   == false && 
    	     server->eventhandler.init == false )
    	     server->init = false;
   }    

    liq_shutdown(server);
    fprintf(stdout, "main() success ... \n");
    pthread_exit(NULL);
}
