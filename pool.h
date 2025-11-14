#ifndef POOL_H
#define POOL_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
#endif
