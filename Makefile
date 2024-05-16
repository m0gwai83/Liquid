
##****************************************************************************
##
##  Titel:		Liquid ICS
##  Name:		Makefile
##  Inhalt:		Generierung des ausf�hrbaren Programms.
##  Autor:		m0gwai@arcor.de
##
##  Stand:
##  13.12.2008 		Zweite Fassung.
##
##****************************************************************************

##============================================================================
##  allgemeine Definitionen
##============================================================================
## Weitere wichtige Optionen f�r den Compiler, die man benutzen sollte, sind: 
## 
## -Wall: warnt an dubiosen Stellen im Code, die in der Regel tats�chlich 
##        Programmierfehler sind (obwohl sie f�r C eigentlich noch tolerabel w�ren). 
##
## -ansi: sorgt daf�r, dass ANSI-C-Code akzeptiert wird. Solcher Code wird meist
##        auch ohne diese Option akzeptiert, nur f�r wenige Sprachkonstrukte ist die 
##        Option wirklich notwendig. Aber auch manche Nicht-ANSI-C-Programme werden akzeptiert. 
##
## -pedantic: sorgt daf�r, dass bei Sprachkonstruktionen, die nicht dem ANSI-Standard 
##            entsprechen, Warnungen erzeugt werden. 
##
## -O2: optimiert den Code in Hinsicht auf Laufzeit und deckt dabei manchmal auch 
##      verborgene Programmfehler auf. 
##
## -lm: ist notwendig, wenn man die mathematischen Funktione 
##      der Standardbibliothek aus math.h benutzt. 
##============================================================================
# -D_REENTRANT   -lepoll

CC      = /usr/bin/gcc
CFLAGS	= -c -O2 -Wall -pedantic -g  -D_POSIX_PTHREAD_SEMANTICS
LDFLAGS = -lm  -lpthread -I`pg_config --includedir` -L`pg_config --libdir` -lpq
OBJ 	= main.o net.o

liquid: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o liquid

main.o: inc/net.h main.c
	$(CC) $(CFLAGS) main.c

net.o: inc/net.h inc/net.c
	$(CC) $(CFLAGS) inc/net.c


##============================================================================
##  Aufr�umen
##============================================================================

clean:
	rm -f liquid *.o *~


##*****************************************************************************

