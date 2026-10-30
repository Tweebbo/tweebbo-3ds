#include "../source/net.c"
#include <assert.h>
#include <stdatomic.h>
#include <time.h>

static atomic_int requests;
static atomic_bool ready = true;
static atomic_bool production_seen;
bool conn_ready(void) { return atomic_load(&ready); }
bool endpoint_parse(const char *url, Endpoint *out) {
  memset(out, 0, sizeof *out);
  snprintf(out->host, sizeof out->host, "%s", url);
  return true;
}
void http_request(const Endpoint *ep, const char *method, const char *path, const char *bearer,
                  const char *json, int timeout_ms, HttpResult *out) {
  (void)method; (void)bearer; (void)json; (void)timeout_ms;
  memset(out, 0, sizeof *out);
  if (!strcmp(path, "/healthz")) {
    assert(!strcmp(ep->host, PROD_SERVER));
    atomic_store(&production_seen, true);
  }
  atomic_fetch_add(&requests, 1);
}
static u64 monotonic_ms(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (u64)t.tv_sec*1000 + t.tv_nsec/1000000;
}
static void consume(int id) {
  HttpResult result;
  u64 until = monotonic_ms() + 2000;
  while (!net_http_result(id, &result)) { assert(monotonic_ms() < until); usleep(1000); }
}
int main(void) {
  mutex_init(&N.mu); job_signal_init(&N.http_wake);
  /* A request queued before the worker starts retains its wake-up. */
  int first = net_http("GET", "/one", NULL, false); assert(first);
  ThreadHandle worker = spawn(http_thread); assert(worker);
  consume(first);
  for (int round=0; round<100; round++) {
    int ids[MAX_HTTP];
    for (int i=0; i<MAX_HTTP; i++) { ids[i]=net_http("GET", "/burst", NULL, false); assert(ids[i]); }
    assert(!net_http("GET", "/full", NULL, false));
    for (int i=0; i<MAX_HTTP; i++) consume(ids[i]);
  }
  int prod=net_check_prod(); assert(prod); consume(prod); assert(atomic_load(&production_seen));
  atomic_store(&ready, false);
  int not_ready=net_http("GET", "/early", NULL, false); assert(not_ready); consume(not_ready);
  assert(atomic_load(&requests)==802);
  /* Measure the actual worker while no requests are queued. */
  usleep(20000);
  clockid_t cpu_clock; assert(!pthread_getcpuclockid(*worker, &cpu_clock));
  struct timespec before, after;
  assert(!clock_gettime(cpu_clock, &before)); usleep(250000);
  assert(!clock_gettime(cpu_clock, &after));
  long long cpu_ns=(after.tv_sec-before.tv_sec)*1000000000ll+after.tv_nsec-before.tv_nsec;
  assert(cpu_ns < 10000000); /* a sleeping worker needs essentially no CPU */
  for (int restart=0; restart<20; restart++) {
    lock(&N.mu); N.quit=true; unlock(&N.mu); job_signal_send(&N.http_wake);
    u64 start=monotonic_ms(); join(worker); assert(monotonic_ms()-start < 1000);
    lock(&N.mu); N.quit=false; unlock(&N.mu);
    worker=spawn(http_thread); assert(worker);
    usleep(1000);
  }
  lock(&N.mu); N.quit=true; unlock(&N.mu); job_signal_send(&N.http_wake); join(worker);
  printf("PASS: 802 HTTP jobs, full queue, early request, production flag, idle CPU %lld ns / 250 ms, 20 restarts and sleeping shutdown.\n", cpu_ns);
}
