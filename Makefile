
#CFLAGS=-O2 -g -DDEBUG
CFLAGS=-O2 -g

OBJS= watcher.o 
MISSINGS = setproctitle.o progname.o

app: $(OBJS) $(MISSINGS)
	$(CC) $(CFLAGS) -o watcher $(OBJS) $(MISSINGS)

clean:	
	$(RM) *.o  watcher

