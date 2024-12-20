CC = g++
CFLAGS = -g -Wall

all: proxy

proxy: server.cpp
	$(CC) $(CFLAGS) -o proxy_parse.o -c proxy_parse.c -lpthread
	$(CC) $(CFLAGS) -o proxy.o -c server.cpp -lpthread
	$(CC) $(CFLAGS) -o proxy proxy_parse.o proxy.o -lpthread

clean:
	rm -f proxy *.o

tar:
	tar -cvzf ass1.tgz server.cpp README Makefile proxy_parse.c proxy_parse.h