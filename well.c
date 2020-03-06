#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

#define MAX_OCCUPANCY      3
#define NUM_ITERATIONS     100
#define NUM_PEOPLE         20
#define FAIR_WAITING_COUNT 4

/**
 * You might find these declarations useful.
 */
enum Endianness {LITTLE = 0, BIG = 1};
const static enum Endianness oppositeEnd [] = {BIG, LITTLE};

struct Well {
  uthread_mutex_t mutex;
  uthread_cond_t  little;
  uthread_cond_t  big;
  int             occupancy;
  int             waiting[2];
  enum Endianness endiannessIn;
  int             waitingCount;
};

struct Well* createWell() {
  struct Well* well = malloc (sizeof (struct Well));
  well->mutex  = uthread_mutex_create();
  well->little = uthread_cond_create (well->mutex);
  well->big    = uthread_cond_create (well->mutex);
  well->occupancy  = 0;
  well->waiting[0] = 0;
  well->waiting[1] = 0;
  well->endiannessIn = 0;
  well->waitingCount = 0;
  return well;
}

struct Well* Well;

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_PEOPLE)
int             entryTicker;                                          // incremented with each entry
int             waitingHistogram         [WAITING_HISTOGRAM_SIZE];
int             waitingHistogramOverflow;
uthread_mutex_t waitingHistogrammutex;
int             occupancyHistogram       [2] [MAX_OCCUPANCY + 1];

void enterWell (enum Endianness g) {
  uthread_mutex_lock(Well->mutex);
  while(1){
    if(Well->occupancy == 0){            // empty Well
      Well->waitingCount = 0;
      //Well->waitingCount++;
      Well->occupancy++;
      Well->endiannessIn = g;
      entryTicker ++;
      break;
    }
    else if(Well->occupancy<MAX_OCCUPANCY && Well->endiannessIn==g && (Well->waitingCount<FAIR_WAITING_COUNT || Well->waiting[oppositeEnd[g]] == 0)){                 // same person can get in
      //Well->waitingCount++;
      Well->occupancy++;
      entryTicker ++;
      break;
    }

    // can not in
    if(Well->endiannessIn!=g && Well->waiting[g]==0)      // different person starts to wait
      Well->waitingCount = 0;

    Well->waiting[g]++;
    if(g==BIG){
      uthread_cond_wait(Well->big);
    }
    else{
      uthread_cond_wait(Well->little);
    }
    Well->waiting[g]--;
    //Well->waitingCount++;
    Well->endiannessIn=g;
  }
  Well->waitingCount++;

   // assert (Well->occupancy == 0 || Well->endiannessIn == g);
   // assert (Well->occupancy <= MAX_OCCUPANCY);
   // assert (Well->waitingCount <= FAIR_WAITING_COUNT);

  occupancyHistogram[Well->endiannessIn][Well->occupancy]++;
  uthread_mutex_unlock(Well->mutex);
}

void leaveWell() {
  uthread_mutex_lock(Well->mutex);

    Well->occupancy--;
    if(Well->endiannessIn==BIG && (Well->waitingCount<FAIR_WAITING_COUNT || Well->waiting[oppositeEnd[Well->endiannessIn]] == 0) && Well->waiting[Well->endiannessIn]!=0)
      uthread_cond_signal(Well->big);                     // if big is waiting and can in
    else if(Well->endiannessIn==LITTLE && (Well->waitingCount<FAIR_WAITING_COUNT || Well->waiting[oppositeEnd[Well->endiannessIn]] == 0) && Well->waiting[Well->endiannessIn]!=0)
      uthread_cond_signal(Well->little);
    else if( (Well->waitingCount>=FAIR_WAITING_COUNT || Well->waiting[Well->endiannessIn] == 0) && Well->waiting[oppositeEnd[Well->endiannessIn]] != 0){
      if (Well->occupancy == 0) {
        if(Well->endiannessIn == BIG){
          for (int i = 0; i < MAX_OCCUPANCY; i++) {
            uthread_cond_signal (Well->little);
          }
        }
        else{
          for (int i = 0; i < MAX_OCCUPANCY; i++) {
            uthread_cond_signal (Well->big);
          }
        }
      }
    }

  uthread_mutex_unlock(Well->mutex);
}

void recordWaitingTime (int waitingTime) {
  uthread_mutex_lock (waitingHistogrammutex);
  if (waitingTime < WAITING_HISTOGRAM_SIZE)
    waitingHistogram [waitingTime] ++;
  else
    waitingHistogramOverflow ++;
  uthread_mutex_unlock (waitingHistogrammutex);
}

//
// TODO
// You will probably need to create some additional produres etc.
//
void* createPerson() {
  enum Endianness person = random()&1;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int startTime = entryTicker;
    enterWell(person);
    recordWaitingTime(entryTicker - startTime - 1);
    for (int j = 0; j < NUM_PEOPLE; j++)
      uthread_yield();
    leaveWell();
    for (int j = 0; j < NUM_PEOPLE; j++)
      uthread_yield();
  }
  return NULL;
}

int main (int argc, char** argv) {
  uthread_init (1);
  Well = createWell();
  uthread_t pt [NUM_PEOPLE];
  waitingHistogrammutex = uthread_mutex_create ();

  // TODO
  for (int i = 0; i < NUM_PEOPLE; i++)
    pt[i]=uthread_create(createPerson,Well);
  for (int i = 0; i < NUM_PEOPLE; i++)
    uthread_join (pt[i], 0);


  printf ("Times with 1 little endian %d\n", occupancyHistogram [LITTLE]   [1]);
  printf ("Times with 2 little endian %d\n", occupancyHistogram [LITTLE]   [2]);
  printf ("Times with 3 little endian %d\n", occupancyHistogram [LITTLE]   [3]);
  printf ("Times with 1 big endian    %d\n", occupancyHistogram [BIG] [1]);
  printf ("Times with 2 big endian    %d\n", occupancyHistogram [BIG] [2]);
  printf ("Times with 3 big endian    %d\n", occupancyHistogram [BIG] [3]);
  printf ("Waiting Histogram\n");
  for (int i=0; i<WAITING_HISTOGRAM_SIZE; i++)
    if (waitingHistogram [i])
      printf ("  Number of times people waited for %d %s to enter: %d\n", i, i==1?"person":"people", waitingHistogram [i]);
  if (waitingHistogramOverflow)
    printf ("  Number of times people waited more than %d entries: %d\n", WAITING_HISTOGRAM_SIZE, waitingHistogramOverflow);
}
