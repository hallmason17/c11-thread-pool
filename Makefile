
CC=clang
CFLAGS=-Wall -Wpedantic -Werror -O2 -std=c11

bench: benches.c pool_global_queue.c
	mkdir -p bin && $(CC) $(CFLAGS) -o bin/bench_global_queue benches.c pool_global_queue.c -lpthread \
		&& $(CC) $(CFLAGS) -o bin/bench_queue_per_thread benches.c pool_queue_per_thread.c -lpthread \
		&& ./bin/bench_global_queue && ./bin/bench_queue_per_thread


