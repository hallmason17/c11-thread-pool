#include "pool.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SHUTDOWN 1 << 0

typedef struct task {
    /* Function to execute */
    void *(*func)(void *arg);

    /* Arg to the function */
    void *arg;

    /* Next task in linked-list */
    struct task *next;

    /* Task id */
    int id;
} task_t;

typedef struct worker_queue {

    /* Task queue */
    task_t *task_queue_head;
    task_t *task_queue_tail;

    /* Task queue lock */
    pthread_mutex_t mu;

    /* Wake up a thread */
    pthread_cond_t cond;

    uint64_t num_tasks;
} worker_queue_t;

typedef struct worker_args {
    thread_pool_t *pool;
    worker_queue_t *queue;
} worker_args_t;

struct thread_pool_t {
    /* Worker threads */
    pthread_t *workers;

    /* Queue for each of the workers */
    worker_queue_t *worker_queues;

    /* For state management, shutdown, exiting, etc. */
    atomic_int flags;

    /* Number of threads in the pool */
    uint8_t num_threads;
};

/* Task Ids */
static atomic_int tid = (0);

/* Submit a task to the thread pool */
int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg) {
    if (atomic_load(&pool->flags) & SHUTDOWN) {
        return 0;
    }

    task_t *t = malloc(sizeof(task_t));

    if (!t) {
        free(arg);
        return 0;
    }
    t->id = atomic_load(&tid);
    atomic_fetch_add(&tid, 1);

    t->func = func;
    t->arg = arg;
    t->next = NULL;

    /* Assign to thread based on its Id */
    int index = t->id % pool->num_threads;

    pthread_mutex_lock(&pool->worker_queues[index].mu);

    if (pool->worker_queues[index].task_queue_head == NULL) {
        pool->worker_queues[index].task_queue_head = t;
        pool->worker_queues[index].task_queue_tail = t;
    } else {
        pool->worker_queues[index].task_queue_tail->next = t;
        pool->worker_queues[index].task_queue_tail = t;
    }

    /* Wake up the thread */
    pool->worker_queues[index].num_tasks++;
    pthread_cond_signal(&pool->worker_queues[index].cond);
    pthread_mutex_unlock(&pool->worker_queues[index].mu);
    return 1;
}

/* Task a task out of the thread's queue */
task_t *dequeue_task(worker_queue_t *queue) {
    if (queue->task_queue_head == NULL) {
        return NULL;
    }
    task_t *t = queue->task_queue_head;
    queue->task_queue_head = t->next;
    if (queue->task_queue_head == NULL) {
        queue->task_queue_tail = NULL;
    }
    queue->num_tasks--;
    return t;
}

/* The main thread for each worker thread, pulls work out of its queue and
 * executes the function. This frees the task upon completion. If there are no
 * tasks in its queue, the thread goes to sleep. */
void *worker_thread(void *arg) {
    worker_args_t *a = (worker_args_t *)arg;
    thread_pool_t *p = a->pool;
    worker_queue_t *q = a->queue;
    if (!a || !p || !q) {
        exit(1);
    }

    while (1) {
        pthread_mutex_lock(&q->mu);

        /* If there are no tasks in queue and we are not shutting down, go to
         * sleep */
        while (q->task_queue_head == NULL &&
               !(atomic_load(&p->flags) & SHUTDOWN)) {
            pthread_cond_wait(&q->cond, &q->mu);
        }
        /* If queue is empty and we are shutting down, exit the while loop */
        if (q->task_queue_head == NULL && (atomic_load(&p->flags) & SHUTDOWN)) {
            pthread_mutex_unlock(&q->mu);
            break;
        }

        task_t *t = dequeue_task(q);
        pthread_mutex_unlock(&q->mu);
        if (t) {
            t->func(t->arg);
            free(t->arg);
            free(t);
        }
    }
    return arg;
}

/* Initialize the pool with num_threads worker threads, return a pointer to it
 */
thread_pool_t *thread_pool_create(int num_threads) {
    thread_pool_t *pool = malloc(sizeof(thread_pool_t));
    if (pool == NULL) {
        perror("malloc");
        exit(1);
    }
    pool->num_threads = num_threads;
    pool->flags = 0;

    pool->worker_queues = malloc(sizeof(worker_queue_t) * num_threads);
    if (pool->worker_queues == NULL) {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < num_threads; ++i) {
        pool->worker_queues[i].task_queue_head = NULL;
        pool->worker_queues[i].task_queue_tail = NULL;
        pthread_mutex_init(&pool->worker_queues[i].mu, NULL);
        pthread_cond_init(&pool->worker_queues[i].cond, NULL);
    }

    pool->workers = malloc(sizeof(pthread_t) * pool->num_threads);
    if (pool->workers == NULL) {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < pool->num_threads; i++) {
        worker_args_t *args = malloc(sizeof(worker_args_t));
        if (args == NULL) {
            perror("malloc");
            exit(1);
        }
        args->pool = pool;
        args->queue = &pool->worker_queues[i];
        if (pthread_create(&pool->workers[i], NULL, worker_thread, args) ==
            -1) {
            perror("pthread_create");
            exit(1);
        }
    }

    return pool;
}

/* Join all of the threads and free all associated resources. Waits for all
 * submitted tasks to be complete before fully shutting down */
void thread_pool_destroy(thread_pool_t *pool) {
    if (pool == NULL)
        return;

    /* Set the shutdown bit */
    atomic_fetch_or(&pool->flags, SHUTDOWN);

    /* Wake up all of the sleeping threads */
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_mutex_lock(&pool->worker_queues[i].mu);
        pthread_cond_broadcast(&pool->worker_queues[i].cond);
        pthread_mutex_unlock(&pool->worker_queues[i].mu);
    }

    /* Free all the worker_args structs we allocated. */
    for (int i = 0; i < pool->num_threads; i++) {
        void *args = NULL;
        pthread_join(pool->workers[i], &args);
        free(args);
    }

    for (int i = 0; i < pool->num_threads; i++) {
        /* Clean up remaining tasks (there shouldn't be any) */
        task_t *t = pool->worker_queues[i].task_queue_head;
        while (t) {
            task_t *tmp = t;
            t = t->next;
            free(tmp->arg);
            free(tmp);
        }

        pthread_cond_destroy(&pool->worker_queues[i].cond);
        pthread_mutex_destroy(&pool->worker_queues[i].mu);
    }

    free(pool->worker_queues);
    free(pool->workers);
    free(pool);
}
