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
 * We divide the intersection into 4 quadrants:
 *
 *   0 = NW
 *   1 = NE
 *   2 = SE
 *   3 = SW
 *
 * A car must lock all quadrants its path uses.
 */
static pthread_mutex_t quadrant_mutex[4];

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
 * get_quadrants()
 *
 * Returns which quadrants are used by a car entering from (side, direction).
 * The quadrants are always returned in increasing order.
 *
 * Chosen mapping:
 *
 * NORTH:
 *   LEFT      -> 1, 2, 3
 *   STRAIGHT  -> 1, 2
 *   RIGHT     -> 1
 *
 * EAST:
 *   LEFT      -> 2, 3, 0
 *   STRAIGHT  -> 2, 3
 *   RIGHT     -> 2
 *
 * SOUTH:
 *   LEFT      -> 3, 0, 1
 *   STRAIGHT  -> 3, 0
 *   RIGHT     -> 3
 *
 * WEST:
 *   LEFT      -> 0, 1, 2
 *   STRAIGHT  -> 0, 1
 *   RIGHT     -> 0
 */
static int get_quadrants(int side, int direction, int q[])
{
  if (side == NORTH)
  {
    if (direction == LEFT)
    {
      q[0] = 1;
      q[1] = 2;
      q[2] = 3;
      return 3;
    }
    else if (direction == STRAIGHT)
    {
      q[0] = 1;
      q[1] = 2;
      return 2;
    }
    else /* RIGHT */
    {
      q[0] = 1;
      return 1;
    }
  }
  else if (side == EAST)
  {
    if (direction == LEFT)
    {
      q[0] = 0;
      q[1] = 2;
      q[2] = 3;
      return 3;
    }
    else if (direction == STRAIGHT)
    {
      q[0] = 2;
      q[1] = 3;
      return 2;
    }
    else /* RIGHT */
    {
      q[0] = 2;
      return 1;
    }
  }
  else if (side == SOUTH)
  {
    if (direction == LEFT)
    {
      q[0] = 0;
      q[1] = 1;
      q[2] = 3;
      return 3;
    }
    else if (direction == STRAIGHT)
    {
      q[0] = 0;
      q[1] = 3;
      return 2;
    }
    else /* RIGHT */
    {
      q[0] = 3;
      return 1;
    }
  }
  else /* WEST */
  {
    if (direction == LEFT)
    {
      q[0] = 0;
      q[1] = 1;
      q[2] = 2;
      return 3;
    }
    else if (direction == STRAIGHT)
    {
      q[0] = 0;
      q[1] = 1;
      return 2;
    }
    else /* RIGHT */
    {
      q[0] = 0;
      return 1;
    }
  }
}

/*
 * lock_quadrants()
 *
 * Lock all required quadrants in increasing order.
 * This fixed order prevents deadlock.
 */
static void lock_quadrants(int q[], int count)
{
  for (int i = 0; i < count; i++)
  {
    pthread_mutex_lock(&quadrant_mutex[q[i]]);
  }
}

/*
 * unlock_quadrants()
 *
 * Unlock in reverse order.
 */
static void unlock_quadrants(int q[], int count)
{
  for (int i = count - 1; i >= 0; i--)
  {
    pthread_mutex_unlock(&quadrant_mutex[q[i]]);
  }
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

    int q[3];
    int count = get_quadrants(side, direction, q);

    lock_quadrants(q, count);

    printf("traffic light %d %d turns green at time %d for car %d\n",
           side, direction, get_time_passed(), car.id);
    fflush(stdout);

    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n",
           side, direction, get_time_passed());
    fflush(stdout);

    unlock_quadrants(q, count);
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

  // initialize quadrant mutexes
  for (int i = 0; i < 4; i++)
  {
    pthread_mutex_init(&quadrant_mutex[i], NULL);
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

  // destroy mutexes
  for (int i = 0; i < 4; i++)
  {
    pthread_mutex_destroy(&quadrant_mutex[i]);
  }

  return 0;
}