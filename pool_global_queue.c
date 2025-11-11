#include "pool.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
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

#define SHUTDOWN 1 << 0
#define EXITING 1 << 1
struct thread_pool_t {
  /* Worker threads */
  pthread_t *workers;

  /* Task queue */
  task_t *task_queue_head;
  task_t *task_queue_tail;

  /* Task queue lock */
  pthread_mutex_t mu;

  /* Wake up a thread */
  pthread_cond_t cond;

  pthread_cond_t shutdown_cond;

  uint64_t num_tasks;
  uint8_t num_threads;
  uint8_t flags;
};
static int tid = 0;
int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg) {
  if (pool->flags & SHUTDOWN) {
    pthread_mutex_unlock(&pool->mu);
    return 0;
  }
  task_t *t = malloc(sizeof(task_t));
  if (!t) {
    free(arg);
    return 0;
  }
  pthread_mutex_lock(&pool->mu);
  t->id = ++tid;
  t->func = func;
  t->arg = arg;
  t->next = NULL;
  if (pool->task_queue_head == NULL) {
    pool->task_queue_head = t;
    pool->task_queue_tail = t;
  } else {
    pool->task_queue_tail->next = t;
    pool->task_queue_tail = t;
  }
  pool->num_tasks++;
  pthread_cond_signal(&pool->cond);
  pthread_mutex_unlock(&pool->mu);
  return 1;
}

/* Assumes you hold the lock already */
task_t *dequeue_task(thread_pool_t *pool) {
  if (pool->task_queue_head == NULL) {
    return NULL;
  }
  task_t *t = pool->task_queue_head;
  if (t == pool->task_queue_tail) {
    pool->task_queue_head = NULL;
    pool->task_queue_tail = NULL;
  }
  pool->num_tasks--;
  pool->task_queue_head = t->next;
  return t;
}

void *worker_thread(void *arg) {
  thread_pool_t *p = (thread_pool_t *)arg;
  while (1) {
    pthread_mutex_lock(&p->mu);
    while (p->task_queue_head == NULL && !(p->flags & EXITING)) {
      pthread_cond_wait(&p->cond, &p->mu);
    }
    if (p->flags & EXITING) {
      pthread_mutex_unlock(&p->mu);
      break;
    }
    task_t *t = dequeue_task(p);
    pthread_mutex_unlock(&p->mu);
    if (t) {
      t->func(t->arg);
      free(t->arg);
      free(t);
    }
    pthread_mutex_lock(&p->mu);
    if (p->num_tasks == 0 && (p->flags & SHUTDOWN)) {
      pthread_cond_signal(&p->shutdown_cond);
    }
    pthread_mutex_unlock(&p->mu);
  }
  return NULL;
}

thread_pool_t *thread_pool_create(int num_threads) {
  thread_pool_t *pool = malloc(sizeof(thread_pool_t));
  if (pool == NULL) {
    perror("malloc");
    exit(1);
  }
  pool->num_threads = num_threads;
  pool->task_queue_head = NULL;
  pool->task_queue_tail = NULL;
  pool->flags = 0;
  pthread_mutex_init(&pool->mu, NULL);
  pthread_cond_init(&pool->cond, NULL);
  pthread_cond_init(&pool->shutdown_cond, NULL);
  pool->workers = malloc(sizeof(pthread_t) * pool->num_threads);
  for (int i = 0; i < pool->num_threads; i++) {
    if (pthread_create(&pool->workers[i], NULL, worker_thread, pool) == -1) {
      perror("pthread_create");
      exit(1);
    }
  }
  return pool;
}
void thread_pool_destroy(thread_pool_t *pool) {
  pthread_mutex_lock(&pool->mu);
  pool->flags |= SHUTDOWN;
  pthread_cond_broadcast(&pool->cond);

  while (pool->num_tasks > 0) {
    pthread_cond_wait(&pool->shutdown_cond, &pool->mu);
  }
  pthread_mutex_unlock(&pool->mu);

  pthread_mutex_lock(&pool->mu);
  pool->flags |= EXITING;
  pthread_cond_broadcast(&pool->cond);
  pthread_mutex_unlock(&pool->mu);

  for (int i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->workers[i], NULL);
  }

  pthread_cond_destroy(&pool->cond);
  pthread_cond_destroy(&pool->shutdown_cond);
  pthread_mutex_destroy(&pool->mu);

  free(pool->workers);
  free(pool);
}
