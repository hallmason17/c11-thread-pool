# Simple C11 Thread Pool

A lightweight and fast thread pool implementation in C11.

This project provides a minimal thread pool designed for high-throughput task execution. It contains two implementations, one with a global work queue that all worker threads share,
and another implementation with per-thread work queues meant to reduce lock contention.

In the "per-thread" implementation, work is divided round-robin style between all of the
worker threads, with no work-stealing involved at the moment. Each thread is notified about work in its queue via a condition variable:

```c
    pthread_mutex_lock(&q->mu);
    while (q->task_queue_head == NULL && !(atomic_load(&p->flags) & SHUTDOWN)) {
      pthread_cond_wait(&q->cond, &q->mu);
    }
```

so that when there is no work for the thread to complete, it goes to sleep and does not waste resources.

## Features

  * **Lightweight:** Minimal API.
  * **High Performance:** Uses per-thread queues to reduce contention on a single global queue.
  * **Safe Shutdown:** The `thread_pool_destroy` function guarantees that all submitted tasks will be completed before the pool is freed.

-----

## API Overview

The API is simple and contained in `pool.h`:

```c
/* Handle to the thread pool */
typedef struct thread_pool_t thread_pool_t;

/**
 * @brief Creates a new thread pool with a fixed number of worker threads.
 *
 * @param num_threads The number of worker threads to create.
 * @return A pointer to the new thread_pool_t, or NULL on error.
 */
thread_pool_t *thread_pool_create(int num_threads);

/**
 * @brief Submits a new task to the thread pool.
 *
 * The task is added to a worker's queue, and a thread will
 * execute it at some point in the future.
 *
 * @param pool The thread pool handle.
 * @param func The function (task) to execute.
 * @param arg  The argument to pass to the function.
 * @return 1 on success, 0 on failure.
 */
int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg);

/**
 * @brief Shuts down and destroys the thread pool.
 *
 * This function will:
 * - Stop accepting new tasks.
 * - Wait for all pending tasks in all queues to be completed.
 * - Join all worker threads.
 * - Free all resources associated with the pool.
 *
 * @param pool The thread pool handle.
 */
void thread_pool_destroy(thread_pool_t *pool);
```

-----

## Example Usage

Here is a simple program demonstrating how to use the pool.

```c
#include "pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Task function to be executed by the pool */
void *my_task(void *arg) {
    int id = *(int*)arg;
    printf("Task %d is starting...\n", id);

    /* Simulate some work */
    sleep(1);

    printf("Task %d is finishing.\n", id);

    free(arg);

    return NULL;
}

int main() {
    /* Create a pool with 4 worker threads */
    thread_pool_t *pool = thread_pool_create(4);
    if (pool == NULL) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    printf("Submitting 10 tasks to the pool...\n");

    for (int i = 0; i < 10; i++) {
        int *task_id = malloc(sizeof(int));
        *task_id = i + 1;

        /* Submit the task */
        thread_pool_submit(pool, my_task, task_id);
    }

    printf("All tasks submitted. Shutting down the pool.\n");
    printf("The pool will wait for all tasks to complete...\n");

    /* This will block until all 10 tasks are finished */
    thread_pool_destroy(pool);

    printf("Thread pool destroyed. Exiting.\n");

    return 0;
}
```

-----

## Building & Running Benchmarks

The project includes a `benches.c` file to test performance. You can compile and run it with the following commands:

### Compilation

```bash
make bench # Runs all benchmarks
```

-----

## Benchmark Results

This pool shows great performance when compared to the naive "one thread per task" model.

*(Note: These are example results from my machine. Numbers will vary based on CPU and OS.)*

### 1\. High-Throughput Test

This test measures how many trivial tasks the pool can process per second.

```
===========================================
          THREAD POOL BENCHMARKS
===========================================

Benchmark: Throughput (8 threads, 500000 tasks)
Completed: 500000 tasks
Time: 0.411 seconds
Throughput: 1216652 tasks/sec
Avg latency: 0.82 μs/task
```

**Conclusion:** The pool can process over 1,000,000 tasks per second with an average overhead of only \~0.82 microseconds per task.

### 2\. Thread Scaling

This test shows how performance scales as we add more threads. The ideal is to have the speedup match the thread count (8 threads = 8x speedup), but contention and overhead limit this.

```
Benchmark: Thread performance (500000 tasks)
Thread Count | Time (s) | Throughput (tasks/s) | Speedup
-------------|----------|----------------------|--------
           1 |    0.606 |               824848 |   1.00x
           2 |    0.318 |              1574034 |   1.91x
           4 |    0.173 |              2882925 |   3.50x
           8 |    0.153 |              3261963 |   3.95x
          16 |    0.596 |               838745 |   1.02x
          32 |    0.591 |               845467 |   1.02x
```

**Conclusion:**

  * **Scaling** up to 8 threads (on an 8-core machine), achieving a 3.95x speedup and over 3mil tasks per second.
  * **Diminishing returns** past 8 threads (hyper-threading), as contention for system resources increases.

### 3\. Comparison vs. Naive `pthread_create`

This test highlights the advantage of using a thread pool: re-using threads is far cheaper than creating a new one for every task.

```
Benchmark: Thread pool vs Pthread per task

Thread Pool (8 threads)
Time: 0.013 seconds
Throughput: 779842 tasks/sec

Pthread per task (time includes creation and joining)
Time: 0.661 seconds
Throughput: 15130 tasks/sec

Speedup: 51.54x faster with thread pool
```

**Conclusion:** For this workload, the thread pool is over **51 times faster** than creating and joining a new `pthread` for each task. This demonstrates the massive overhead of thread creation/destruction that the pool successfully avoids.

-----

## License

This project is under the **MIT License**.
