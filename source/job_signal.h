#pragma once
#include <stdbool.h>

#ifdef __3DS__
#include <3ds.h>
typedef LightEvent JobSignal;
static inline void job_signal_init(JobSignal *s) { LightEvent_Init(s, RESET_ONESHOT); }
static inline void job_signal_send(JobSignal *s) { LightEvent_Signal(s); }
static inline void job_signal_wait(JobSignal *s) { LightEvent_Wait(s); }
#else
#include <pthread.h>
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  bool pending;
} JobSignal;

static inline void job_signal_init(JobSignal *s) {
  pthread_mutex_init(&s->mutex, NULL);
  pthread_cond_init(&s->condition, NULL);
  s->pending = false;
}

/* Keep a notification sent between checking the queue and entering the wait. */
static inline void job_signal_send(JobSignal *s) {
  pthread_mutex_lock(&s->mutex);
  s->pending = true;
  pthread_cond_signal(&s->condition);
  pthread_mutex_unlock(&s->mutex);
}

static inline void job_signal_wait(JobSignal *s) {
  pthread_mutex_lock(&s->mutex);
  while (!s->pending) pthread_cond_wait(&s->condition, &s->mutex);
  s->pending = false;
  pthread_mutex_unlock(&s->mutex);
}
#endif
