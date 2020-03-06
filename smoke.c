#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#define NUM_ITERATIONS 1000

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

struct Agent {
  uthread_mutex_t mutex;
  uthread_cond_t  match;
  uthread_cond_t  paper;
  uthread_cond_t  tobacco;
  uthread_cond_t  smoke;
};
struct Agent* createAgent() {
  struct Agent* agent = malloc (sizeof (struct Agent));
  agent->mutex   = uthread_mutex_create();
  agent->paper   = uthread_cond_create (agent->mutex);
  agent->match   = uthread_cond_create (agent->mutex);
  agent->tobacco = uthread_cond_create (agent->mutex);
  agent->smoke   = uthread_cond_create (agent->mutex);
  return agent;
}

//
// TODO
// You will probably need to add some procedures and struct etc.
//
struct Smoker{
  uthread_cond_t matchPaper;
  uthread_cond_t matchTobacco;
  uthread_cond_t paperTobacco;
  uthread_cond_t smoke;
  struct Agent*  agent;
  int match, paper, tobacco;
};
struct Smoker* createSmoker(struct Agent* a){
  struct Smoker* smoker = malloc(sizeof(struct Smoker));
  smoker->matchPaper    = uthread_cond_create (a->mutex);
  smoker->matchTobacco  = uthread_cond_create (a->mutex);
  smoker->paperTobacco  = uthread_cond_create (a->mutex);
  smoker->smoke         = uthread_cond_create (a->mutex);
  smoker->agent         = a;
  smoker->match = 0;
  smoker->paper = 0;
  smoker->tobacco = 0;
  return smoker;
}

/**
 * You might find these declarations helpful.
 *   Note that Resource enum had values 1, 2 and 4 so you can combine resources;
 *   e.g., having a MATCH and PAPER is the value MATCH | PAPER == 1 | 2 == 3
 */
enum Resource            {    MATCH = 1, PAPER = 2,   TOBACCO = 4};
char* resource_name [] = {"", "match",   "paper", "", "tobacco"};

int signal_count [5];  // # of times resource signalled
int smoke_count  [5];  // # of times smoker with resource smoked

/**
 * This is the agent procedure.  It is complete and you shouldn't change it in
 * any material way.  You can re-write it if you like, but be sure that all it does
 * is choose 2 random reasources, signal their condition variables, and then wait
 * wait for a smoker to smoke.
 */
void* agent (void* av) {
  struct Agent* a = av;
  static const int choices[]         = {MATCH|PAPER, MATCH|TOBACCO, PAPER|TOBACCO};
  static const int matching_smoker[] = {TOBACCO,     PAPER,         MATCH};

  uthread_mutex_lock (a->mutex);
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      int r = random() % 3;
      signal_count [matching_smoker [r]] ++;
      int c = choices [r];
      if (c & MATCH) {
        VERBOSE_PRINT ("match available\n");
        uthread_cond_signal (a->match);
      }
      if (c & PAPER) {
        VERBOSE_PRINT ("paper available\n");
        uthread_cond_signal (a->paper);
      }
      if (c & TOBACCO) {
        VERBOSE_PRINT ("tobacco available\n");
        uthread_cond_signal (a->tobacco);
      }
      VERBOSE_PRINT ("agent is waiting for smoker to smoke\n");
      uthread_cond_wait (a->smoke);
    }
  uthread_mutex_unlock (a->mutex);
  return NULL;
}

void* toSmokerMatch(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    uthread_cond_wait(s->agent->match);
    s->match = 1;
    if(s->match && s->tobacco){
      s->match = 0;
      s->tobacco = 0;
      uthread_cond_signal (s->matchTobacco);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
    else if(s->match && s->paper){
      s->match = 0;
      s->paper = 0;
      uthread_cond_signal (s->matchPaper);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}
void* toSmokerPaper(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    uthread_cond_wait(s->agent->paper);
    s->paper = 1;
    if(s->paper && s->tobacco){
      s->paper = 0;
      s->tobacco = 0;
      uthread_cond_signal (s->paperTobacco);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
    else if(s->match && s->paper){
      s->match = 0;
      s->paper = 0;
      uthread_cond_signal (s->matchPaper);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}
void* toSmokerTobacco(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    uthread_cond_wait(s->agent->tobacco);
    s->tobacco = 1;
    if(s->match && s->tobacco){
      s->match = 0;
      s->tobacco = 0;
      uthread_cond_signal (s->matchTobacco);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
    else if(s->tobacco && s->paper){
      s->tobacco = 0;
      s->paper = 0;
      uthread_cond_signal (s->paperTobacco);
      uthread_cond_wait   (s->smoke);
      uthread_cond_signal (s->agent->smoke);
    }
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}

void* smokerMatch(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    //printf("Match is waiting!\n");
    uthread_cond_wait(s->paperTobacco);
    //printf("Match is smoking!\n");
    smoke_count[MATCH]++;
    uthread_cond_signal(s->smoke);
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}
void* smokerTobacco(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    //printf("Tobacco is waiting!\n");
    uthread_cond_wait(s->matchPaper);
    //printf("Tobacco is smoking!\n");
    smoke_count[TOBACCO]++;
    uthread_cond_signal(s->smoke);
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}
void* smokerPaper(void* sv){
  struct Smoker* s = sv;
  uthread_mutex_lock(s->agent->mutex);
  while(1){
    //printf("Paper is waiting!\n");
    uthread_cond_wait(s->matchTobacco);
    //printf("Paper is smoking!\n");
    smoke_count[PAPER]++;
    uthread_cond_signal(s->smoke);
  }
  uthread_mutex_unlock(s->agent->mutex);
  return NULL;
}

int main (int argc, char** argv) {
  uthread_init (7);
  struct Agent*  a = createAgent();
  // TODO

  struct Smoker* s = createSmoker(a);
  uthread_create (toSmokerMatch, s);
  uthread_create (toSmokerTobacco, s);
  uthread_create (toSmokerPaper, s);
  uthread_create (smokerMatch, s);
  uthread_create (smokerTobacco, s);
  uthread_create (smokerPaper, s);
  uthread_join (uthread_create (agent, a), 0);

  assert (signal_count [MATCH]   == smoke_count [MATCH]);
  assert (signal_count [PAPER]   == smoke_count [PAPER]);
  assert (signal_count [TOBACCO] == smoke_count [TOBACCO]);
  assert (smoke_count [MATCH] + smoke_count [PAPER] + smoke_count [TOBACCO] == NUM_ITERATIONS);
  printf ("Smoke counts: %d matches, %d paper, %d tobacco\n",
          smoke_count [MATCH], smoke_count [PAPER], smoke_count [TOBACCO]);


}
