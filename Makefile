CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS =

# Tools that need libm
MATH_TOOLS = sys_stats schedlag

# All binaries
BINS = use stats sys_stats netwatch procwatch netlatency fdwatch schedlag heaptrack

.PHONY: all clean

all: $(BINS) heaptrack_inject.so

use: use.c
	$(CC) $(CFLAGS) -o $@ $<

stats: stats.c
	$(CC) $(CFLAGS) -o $@ $<

sys_stats: sys_stats.c
	$(CC) $(CFLAGS) -o $@ $< -lm

netwatch: netwatch.c
	$(CC) $(CFLAGS) -o $@ $<

procwatch: procwatch.c
	$(CC) $(CFLAGS) -o $@ $<

netlatency: netlatency.c
	$(CC) $(CFLAGS) -o $@ $<

fdwatch: fdwatch.c
	$(CC) $(CFLAGS) -o $@ $<

schedlag: schedlag.c
	$(CC) $(CFLAGS) -o $@ $< -lm -lrt

heaptrack: heaptrack.c
	$(CC) $(CFLAGS) -o $@ $<

heaptrack_inject.so: heaptrack_inject.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $< -ldl -lpthread

clean:
	rm -f $(BINS) heaptrack_inject.so
