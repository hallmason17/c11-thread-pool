
CC=clang
CFLAGS=-Wall -Wpedantic -Werror -O2 -std=c11

bench: benches.c pool_global_queue.c
	$(CC) $(CFLAGS) -o bin/bench_global_queue benches.c pool_global_queue.c

bench1: benches.c pool_queue_per_thread.c
	$(CC) $(CFLAGS) -o bin/bench_queue_per_thread benches.c pool_queue_per_thread.c
