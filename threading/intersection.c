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

/*
 * Advanced solution: 4 mutexes, one per quadrant of the intersection.
 */
static pthread_mutex_t zone_mutexes[4] = {
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER
};

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
  int num_curr_arrivals[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};

  for (int i = 0; i < (int)(sizeof(input_arrivals) / sizeof(Arrival)); i++)
  {
    Arrival arrival = input_arrivals[i];
    sleep_until_arrival(arrival.time);
    curr_arrivals[arrival.side][arrival.direction][num_curr_arrivals[arrival.side][arrival.direction]] = arrival;
    num_curr_arrivals[arrival.side][arrival.direction] += 1;
    sem_post(&semaphores[arrival.side][arrival.direction]);
  }

  return 0;
}

static void get_required_zones(int side, int direction, int zones[2], int *count)
{
  *count = 0;

  if (side == NORTH)
  {
    if (direction == RIGHT)
    { zones[0] = 0; *count = 1; }
    else if (direction == STRAIGHT)
    { zones[0] = 0; zones[1] = 2; *count = 2; }
    else 
    { zones[0] = 1; zones[1] = 3; *count = 2; }
  }
  else if (side == EAST)
  {
    if (direction == RIGHT)
    { zones[0] = 1; *count = 1; }
    else if (direction == STRAIGHT)
    { zones[0] = 0; zones[1] = 3; *count = 2; }
    else 
    { zones[0] = 2; zones[1] = 3; *count = 2; }
  }
  else if (side == SOUTH)
  {
    if (direction == RIGHT)
    { zones[0] = 3; *count = 1; }
    else if (direction == STRAIGHT)
    { zones[0] = 1; zones[1] = 3; *count = 2; }
    else
    { zones[0] = 0; zones[1] = 2; *count = 2; }
  }
  else
  {
    if (direction == RIGHT)
    { zones[0] = 2; *count = 1; }
    else if (direction == STRAIGHT)
    { zones[0] = 1; zones[1] = 2; *count = 2; }
    else /* LEFT */
    { zones[0] = 0; zones[1] = 1; *count = 2; }
  }

  if (*count == 2 && zones[0] > zones[1])
  {
    int tmp = zones[0]; zones[0] = zones[1]; zones[1] = tmp;
  }
}

static void lock_zones(int side, int direction)
{
  int zones[2];
  int count = 0;
  get_required_zones(side, direction, zones, &count);
  for (int i = 0; i < count; i++)
    pthread_mutex_lock(&zone_mutexes[zones[i]]);
}

static void unlock_zones(int side, int direction)
{
  int zones[2];
  int count = 0;
  get_required_zones(side, direction, zones, &count);
  for (int i = count - 1; i >= 0; i--)
    pthread_mutex_unlock(&zone_mutexes[zones[i]]);
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
    int remaining = END_TIME - get_time_passed();
    if (remaining <= 0)
      break;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += remaining;

    int result = sem_timedwait(&semaphores[side][direction], &ts);

    if (result == -1)
    {
      if (errno == ETIMEDOUT)
        break;
      perror("sem_timedwait");
      break;
    }

    if (get_time_passed() >= END_TIME)
      break;

    Arrival car = curr_arrivals[side][direction][next_arrival++];

    /*
     * supply_arrivals() already slept until car.time before posting the semaphore,
     * so the car has already arrived. Do NOT sleep again here.
     */

    lock_zones(side, direction);

    printf("traffic light %d %d turns green at time %d for car %d\n",
           side, direction, get_time_passed(), car.id);
    fflush(stdout);

    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n",
           side, direction, get_time_passed());
    fflush(stdout);

    unlock_zones(side, direction);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  (void)argc; (void)argv;

  pthread_t supply_thread;
  pthread_t light_threads[4][3];
  LightArgs light_args[4][3];

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 3; j++)
      sem_init(&semaphores[i][j], 0, 0);

  start_time();

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      light_args[i][j].side = i;
      light_args[i][j].direction = j;
      pthread_create(&light_threads[i][j], NULL, manage_light, &light_args[i][j]);
    }
  }

  pthread_create(&supply_thread, NULL, supply_arrivals, NULL);

  pthread_join(supply_thread, NULL);

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 3; j++)
      pthread_join(light_threads[i][j], NULL);

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 3; j++)
      sem_destroy(&semaphores[i][j]);

  for (int i = 0; i < 4; i++)
    pthread_mutex_destroy(&zone_mutexes[i]);

  return 0;
}