#include "pool.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
typedef struct {
    struct timeval start;
    struct timeval end;
} my_timer_t;

void timer_start(my_timer_t *timer) { gettimeofday(&timer->start, NULL); }

double timer_stop(my_timer_t *timer) {
    gettimeofday(&timer->end, NULL);

    double start_sec = timer->start.tv_sec + timer->start.tv_usec / 1000000.0;
    double end_sec = timer->end.tv_sec + timer->end.tv_usec / 1000000.0;

    return end_sec - start_sec;
}

atomic_long tasks_completed = 0;
void *fast_task(void *arg) {
    volatile long sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += i;
    }

    atomic_fetch_add(&tasks_completed, 1);

    return NULL;
}

void *medium_task(void *arg) {
    volatile long sum = 0;
    for (int i = 0; i < 1000; i++) {
        sum += i;
    }

    atomic_fetch_add(&tasks_completed, 1);

    return NULL;
}

void benchmark_throughput(int thread_count, int num_tasks) {
    printf("\nBenchmark: Throughput (%d threads, %d tasks)\n", thread_count,
           num_tasks);

    tasks_completed = 0;
    my_timer_t timer;

    thread_pool_t *pool = thread_pool_create(thread_count);

    timer_start(&timer);

    for (int i = 0; i < num_tasks; i++) {
        thread_pool_submit(pool, fast_task, NULL);
    }

    /* This waits for all tasks */
    thread_pool_destroy(pool);

    double elapsed = timer_stop(&timer);

    double throughput = num_tasks / elapsed;

    /* microseconds */
    double avg_latency = (elapsed / num_tasks) * 1000000;

    printf("Completed: %ld tasks\n", tasks_completed);
    printf("Time: %.3f seconds\n", elapsed);
    printf("Throughput: %.0f tasks/sec\n", throughput);
    printf("Avg latency: %.2f μs/task\n", avg_latency);
}

void benchmark_num_threads(void) {
    int num_tasks = 500000;
    printf("\nBenchmark: Thread performance (%d tasks)\n", num_tasks);
    printf("Thread Count | Time (s) | Throughput (tasks/s) | Speedup\n");
    printf("-------------|----------|----------------------|--------\n");

    double baseline_time = 0;

    int thread_counts[] = {1, 2, 4, 8, 16, 32};
    int num_tests = sizeof(thread_counts) / sizeof(thread_counts[0]);

    for (int i = 0; i < num_tests; i++) {
        int threads = thread_counts[i];

        tasks_completed = 0;
        my_timer_t timer;

        thread_pool_t *pool = thread_pool_create(threads);

        timer_start(&timer);

        for (int j = 0; j < num_tasks; j++) {
            thread_pool_submit(pool, medium_task, NULL);
        }

        thread_pool_destroy(pool);

        double elapsed = timer_stop(&timer);

        if (i == 0) {
            baseline_time = elapsed;
        }

        double speedup = baseline_time / elapsed;
        double throughput = num_tasks / elapsed;

        printf("%12d | %8.3f | %20.0f | %6.2fx\n", threads, elapsed, throughput,
               speedup);
    }
}

void *thread_per_task_worker(void *arg) {
    fast_task(arg);
    return NULL;
}

void benchmark_vs_naive(void) {
    printf("\nBenchmark: Thread pool vs Pthread per task\n");

    int num_tasks = 10000;

    printf("\nThread Pool (8 threads)\n");
    tasks_completed = 0;
    my_timer_t timer1;

    thread_pool_t *pool = thread_pool_create(8);

    timer_start(&timer1);

    for (int i = 0; i < num_tasks; i++) {
        thread_pool_submit(pool, fast_task, NULL);
    }

    thread_pool_destroy(pool);
    double pool_time = timer_stop(&timer1);

    printf("Time: %.3f seconds\n", pool_time);
    printf("Throughput: %.0f tasks/sec\n", num_tasks / pool_time);

    // Test 2: Thread per task
    printf("\nPthread per task (time includes creation and joining)\n");
    tasks_completed = 0;
    my_timer_t timer2;

    pthread_t *threads = malloc(sizeof(pthread_t) * num_tasks);

    timer_start(&timer2);

    for (int i = 0; i < num_tasks; i++) {
        pthread_create(&threads[i], NULL, thread_per_task_worker, NULL);
    }

    for (int i = 0; i < num_tasks; i++) {
        pthread_join(threads[i], NULL);
    }

    double naive_time = timer_stop(&timer2);

    printf("Time: %.3f seconds\n", naive_time);
    printf("Throughput: %.0f tasks/sec\n", num_tasks / naive_time);

    printf("\nSpeedup: %.2fx faster with thread pool\n",
           naive_time / pool_time);

    free(threads);
}
int main(void) {
    printf("===========================================\n");
    printf("          THREAD POOL BENCHMARKS\n");
    printf("===========================================\n");

    srand(time(NULL));

    benchmark_throughput(8, 500000);

    benchmark_num_threads();

    benchmark_vs_naive();
    return 0;
}
