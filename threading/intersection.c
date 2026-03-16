#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include "arrivals.h"
#include "intersection_time.h"
#include "input.h"

/* 
 * curr_arrivals[][][]
 *
 * A 3D array that stores the arrivals that have occurred
 * The first two indices determine the entry lane: first index is Side, second index is Direction
 * curr_arrivals[s][d] returns an array of all arrivals for the entry lane on side s for direction d,
 *   ordered in the same order as they arrived
 */
static Arrival curr_arrivals[4][3][20];

/*
 * semaphores[][]
 *
 * A 2D array that defines a semaphore for each entry lane,
 *   which are used to signal the corresponding traffic light that a car has arrived
 * The two indices determine the entry lane: first index is Side, second index is Direction
 */
static sem_t semaphores[4][3];

/* One mutex for the whole intersection: basic solution */
static pthread_mutex_t intersection_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Thread args for each traffic light */
typedef struct {
  int side;
  int direction;
} LightArgs;

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void* supply_arrivals()
{
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < (int)(sizeof(input_arrivals) / sizeof(Arrival)); i++)
  {
    // get the next arrival in the list
    Arrival arrival = input_arrivals[i];
    // wait until this arrival is supposed to arrive
    sleep_until_arrival(arrival.time);
    // store the new arrival in curr_arrivals
    curr_arrivals[arrival.side][arrival.direction][num_curr_arrivals[arrival.side][arrival.direction]] = arrival;
    num_curr_arrivals[arrival.side][arrival.direction] += 1;
    // increment the semaphore for the traffic light that the arrival is for
    sem_post(&semaphores[arrival.side][arrival.direction]);
  }

  return 0;
}


/*
 * manage_light(void* arg)
 *
 * A function that implements the behaviour of a traffic light
 */
static void* manage_light(void* arg)
{
  LightArgs* light = (LightArgs*) arg;
  int side = light->side;
  int direction = light->direction;
  int next_arrival = 0;

  while (true)
  {
    if (get_time_passed() >= END_TIME)
    {
      break;
    }

    /* Wait for a car, but do not wait forever past END_TIME */
    int remaining = END_TIME - get_time_passed();
    if (remaining <= 0)
    {
      break;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += remaining;

    int result = sem_timedwait(&semaphores[side][direction], &ts);

    if (result == -1)
    {
      if (errno == ETIMEDOUT)
      {
        break;
      }
      else
      {
        perror("sem_timedwait");
        break;
      }
    }

    if (get_time_passed() >= END_TIME)
    {
      break;
    }

    Arrival car = curr_arrivals[side][direction][next_arrival];
    next_arrival++;

    pthread_mutex_lock(&intersection_mutex);

    printf("traffic light %d %d turns green at time %d for car %d\n",
           side, direction, get_time_passed(), car.id);
    fflush(stdout);

    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n",
           side, direction, get_time_passed());
    fflush(stdout);

    pthread_mutex_unlock(&intersection_mutex);
  }

  return 0;
}


int main(int argc, char * argv[])
{
  (void)argc;
  (void)argv;

  pthread_t supply_thread;
  pthread_t light_threads[4][3];
  LightArgs light_args[4][3];

  // create semaphores to wait/signal for arrivals
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

  // start the timer
  start_time();

  // create a thread per traffic light that executes manage_light
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      light_args[i][j].side = i;
      light_args[i][j].direction = j;
      pthread_create(&light_threads[i][j], NULL, manage_light, &light_args[i][j]);
    }
  }

  // create a thread that executes supply_arrivals
  pthread_create(&supply_thread, NULL, supply_arrivals, NULL);

  // wait for all threads to finish
  pthread_join(supply_thread, NULL);

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      pthread_join(light_threads[i][j], NULL);
    }
  }

  // destroy semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }

  pthread_mutex_destroy(&intersection_mutex);

  return 0;
}