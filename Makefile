CFLAGS = -Wall -O2 -g

all: latency2001

latency2001: latency2001.c support.c support.h

support.c:
	wget -nv http://ozlabs.org/~anton/junkcode/support.c

support.h:
	wget -nv http://ozlabs.org/~anton/junkcode/support.h

clean:
	rm -f *.o latency2001
