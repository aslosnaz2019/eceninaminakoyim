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

static Arrival curr_arrivals[4][3][20];
static sem_t semaphores[4][3];

static pthread_mutex_t zone_mutex[4] = {
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER,
  PTHREAD_MUTEX_INITIALIZER
};

typedef struct {
  int side;
  int direction;
} LightArgs;

static void get_required_zones(int side, int direction, int zones[3], int* count)
{
  *count = 0;

  switch (side)
  {
    case NORTH:
      if (direction == RIGHT)
      {
        zones[0] = 1;
        *count = 1;
      }
      else if (direction == STRAIGHT)
      {
        zones[0] = 1;
        zones[1] = 2;
        *count = 2;
      }
      else
      {
        zones[0] = 1;
        zones[1] = 2;
        zones[2] = 3;
        *count = 3;
      }
      break;

    case EAST:
      if (direction == RIGHT)
      {
        zones[0] = 2;
        *count = 1;
      }
      else if (direction == STRAIGHT)
      {
        zones[0] = 2;
        zones[1] = 3;
        *count = 2;
      }
      else
      {
        zones[0] = 0;
        zones[1] = 2;
        zones[2] = 3;
        *count = 3;
      }
      break;

    case SOUTH:
      if (direction == RIGHT)
      {
        zones[0] = 3;
        *count = 1;
      }
      else if (direction == STRAIGHT)
      {
        zones[0] = 0;
        zones[1] = 3;
        *count = 2;
      }
      else
      {
        zones[0] = 0;
        zones[1] = 1;
        zones[2] = 3;
        *count = 3;
      }
      break;

    case WEST:
      if (direction == RIGHT)
      {
        zones[0] = 0;
        *count = 1;
      }
      else if (direction == STRAIGHT)
      {
        zones[0] = 0;
        zones[1] = 1;
        *count = 2;
      }
      else
      {
        zones[0] = 0;
        zones[1] = 1;
        zones[2] = 2;
        *count = 3;
      }
      break;
  }
}

static void lock_zones(const int zones[3], int count)
{
  for (int i = 0; i < count; i++)
  {
    pthread_mutex_lock(&zone_mutex[zones[i]]);
  }
}

static void unlock_zones(const int zones[3], int count)
{
  for (int i = count - 1; i >= 0; i--)
  {
    pthread_mutex_unlock(&zone_mutex[zones[i]]);
  }
}

static void* supply_arrivals()
{
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

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

    int zones[3];
    int zone_count = 0;
    get_required_zones(side, direction, zones, &zone_count);

    lock_zones(zones, zone_count);

    printf("traffic light %d %d turns green at time %d for car %d\n",
           side, direction, get_time_passed(), car.id);
    fflush(stdout);

    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n",
           side, direction, get_time_passed());
    fflush(stdout);

    unlock_zones(zones, zone_count);
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

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

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
  {
    for (int j = 0; j < 3; j++)
    {
      pthread_join(light_threads[i][j], NULL);
    }
  }

  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }

  for (int i = 0; i < 4; i++)
  {
    pthread_mutex_destroy(&zone_mutex[i]);
  }

  return 0;
}