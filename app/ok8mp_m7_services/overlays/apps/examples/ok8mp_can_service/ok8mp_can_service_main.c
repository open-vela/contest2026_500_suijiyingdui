/****************************************************************************
 * apps/examples/ok8mp_can_service/ok8mp_can_service_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single-owner CAN receive router and reliable transmit service for OK8MP.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

#include <ok8mp_can_service.h>

#define CAN_SERVICE_MAX_SUBSCRIBERS  6
#define CAN_SERVICE_QUEUE_DEPTH      16
#define CAN_SERVICE_READ_BUDGET      32
#define CAN_SERVICE_START_TIMEOUT_MS 1000
#define CAN_SERVICE_WAIT_STEP_US     1000

struct can_service_subscriber_s
{
  bool active;
  bool extended;
  uint32_t id;
  uint32_t mask;
  uint8_t head;
  uint8_t count;
  sem_t available;
  struct can_msg_s queue[CAN_SERVICE_QUEUE_DEPTH];
};

enum can_service_command_e
{
  CAN_SERVICE_COMMAND_NONE = 0,
  CAN_SERVICE_COMMAND_SEND,
  CAN_SERVICE_COMMAND_RECOVER
};

struct can_service_command_s
{
  volatile enum can_service_command_e type;
  struct can_msg_s msg;
  uint32_t timeout_ms;
  int result;
  sem_t done;
};

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static mutex_t g_tx_lock = NXMUTEX_INITIALIZER;
static struct can_service_subscriber_s
  g_subscribers[CAN_SERVICE_MAX_SUBSCRIBERS];
static struct can_service_command_s g_command;
static struct ok8mp_can_service_stats_s g_stats;
static bool g_command_sem_initialized;
static volatile bool g_starting;
static volatile bool g_running;
static volatile bool g_stop;
static volatile bool g_ready;
static int g_fd = -1;

static uint32_t can_service_now_ms(void)
{
  return (uint32_t)TICK2MSEC(clock_systime_ticks());
}

static FAR const char *can_service_state_name(uint8_t state)
{
  switch (state)
    {
      case OK8MP_CAN_ERROR_PASSIVE:
        return "error_passive";

      case OK8MP_CAN_BUS_OFF:
        return "bus_off";

      case OK8MP_CAN_ERROR_ACTIVE:
      default:
        return "error_active";
    }
}

static bool can_service_frame_extended(FAR const struct can_msg_s *msg)
{
#ifdef CONFIG_CAN_EXTID
  return msg->cm_hdr.ch_extid;
#else
  return false;
#endif
}

static bool can_service_matches(FAR struct can_service_subscriber_s *sub,
                                FAR const struct can_msg_s *msg)
{
  return sub->extended == can_service_frame_extended(msg) &&
         (msg->cm_hdr.ch_id & sub->mask) == (sub->id & sub->mask);
}

static void can_service_dispatch(FAR const struct can_msg_s *msg)
{
  FAR struct can_service_subscriber_s *sub;
  unsigned int i;
  uint8_t tail;

  nxmutex_lock(&g_lock);
  for (i = 0; i < CAN_SERVICE_MAX_SUBSCRIBERS; i++)
    {
      sub = &g_subscribers[i];
      if (!sub->active || !can_service_matches(sub, msg))
        {
          continue;
        }

      if (sub->count == CAN_SERVICE_QUEUE_DEPTH)
        {
          sub->head = (sub->head + 1) % CAN_SERVICE_QUEUE_DEPTH;
          g_stats.rx_dropped++;
        }
      else
        {
          sub->count++;
          nxsem_post(&sub->available);
        }

      tail = (sub->head + sub->count - 1) % CAN_SERVICE_QUEUE_DEPTH;
      memcpy(&sub->queue[tail], msg, sizeof(*msg));
    }

  nxmutex_unlock(&g_lock);
}

static int can_service_recover_fd(int fd)
{
  int ret;

  /* The upper-half sender may retain frames which failed while the bus was
   * disconnected.  Flush it before resetting the lower-half mailbox.
   */

  ret = ioctl(fd, CANIOC_OFLUSH, 0);
  if (ret < 0)
    {
      return -errno;
    }

  ret = ioctl(fd, OK8MP_CANIOC_RECOVER, 0);
  return ret < 0 ? -errno : OK;
}

/* All accesses to the CAN file descriptor must execute in the daemon task
 * which opened it.  NuttX file descriptors belong to a task group, so an
 * fd opened by can_service_daemon cannot be used directly by an NSH command
 * task.  Callers submit one serialized command and wait for the daemon to
 * return its result through g_command.done.
 */

static int can_service_send_fd(int fd, FAR const struct can_msg_s *msg,
                               uint32_t timeout_ms)
{
  struct ok8mp_can_status_s status;
  uint32_t start;
  size_t msglen;
  ssize_t nbytes;
  int ret;

  memset(&status, 0, sizeof(status));
  if (ioctl(fd, OK8MP_CANIOC_GET_STATUS,
            (unsigned long)((uintptr_t)&status)) == 0 &&
      status.fault_state == OK8MP_CAN_BUS_OFF)
    {
      ret = can_service_recover_fd(fd);
      if (ret < 0)
        {
          return ret;
        }
    }

  msglen = CAN_MSGLEN(can_dlc2bytes(msg->cm_hdr.ch_dlc));
  start = can_service_now_ms();
  do
    {
      nbytes = write(fd, msg, msglen);
      if (nbytes == (ssize_t)msglen)
        {
          g_stats.tx_submitted++;
          return OK;
        }

      if (nbytes >= 0 || (errno != EAGAIN && errno != EBUSY))
        {
          ret = nbytes >= 0 ? -EIO : -errno;
          g_stats.last_error = -ret;
          return ret;
        }

      usleep(CAN_SERVICE_WAIT_STEP_US);
    }
  while (can_service_now_ms() - start < timeout_ms);

  g_stats.tx_timeouts++;
  g_stats.last_error = ETIMEDOUT;
  return -ETIMEDOUT;
}

static void can_service_process_command(int fd)
{
  enum can_service_command_e type = g_command.type;
  int ret;

  if (type == CAN_SERVICE_COMMAND_NONE)
    {
      return;
    }

  if (type == CAN_SERVICE_COMMAND_SEND)
    {
      ret = can_service_send_fd(fd, &g_command.msg,
                                g_command.timeout_ms);
    }
  else if (type == CAN_SERVICE_COMMAND_RECOVER)
    {
      ret = can_service_recover_fd(fd);
    }
  else
    {
      ret = -EINVAL;
    }

  g_command.result = ret;
  g_command.type = CAN_SERVICE_COMMAND_NONE;
  nxsem_post(&g_command.done);
}

static int can_service_daemon(int argc, FAR char *argv[])
{
  struct ok8mp_can_status_s status;
  struct can_msg_s msg;
  uint8_t previous_state = OK8MP_CAN_ERROR_ACTIVE;
  uint32_t last_recovery = 0;
  uint32_t now;
  ssize_t nbytes;
  unsigned int reads;
  int ret;

  (void)argc;
  (void)argv;

  g_fd = open(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_DEVPATH,
              O_RDWR | O_NONBLOCK);
  if (g_fd < 0)
    {
      g_stats.last_error = errno;
      g_starting = false;
      g_ready = false;
      return EXIT_FAILURE;
    }

  g_running = true;
  g_starting = false;
  g_ready = true;
  g_stats.running = true;

  while (!g_stop)
    {
      can_service_process_command(g_fd);

      for (reads = 0; reads < CAN_SERVICE_READ_BUDGET; reads++)
        {
          nbytes = read(g_fd, &msg, sizeof(msg));
          if (nbytes < 0)
            {
              if (errno != EAGAIN)
                {
                  g_stats.read_errors++;
                  g_stats.last_error = errno;
                }

              break;
            }

          if (nbytes < CAN_MSGLEN(0) || nbytes > sizeof(msg))
            {
              g_stats.read_errors++;
              g_stats.last_error = EMSGSIZE;
              continue;
            }

          g_stats.rx_frames++;
          can_service_dispatch(&msg);
        }

      memset(&status, 0, sizeof(status));
      ret = ioctl(g_fd, OK8MP_CANIOC_GET_STATUS,
                  (unsigned long)((uintptr_t)&status));
      if (ret == 0)
        {
          memcpy(&g_stats.controller, &status, sizeof(status));
          if (status.fault_state != previous_state)
            {
              printf("can_service: controller=%s txerr=%u rxerr=%u\n",
                     can_service_state_name(status.fault_state),
                     status.tx_error_counter, status.rx_error_counter);
              previous_state = status.fault_state;
            }

          now = can_service_now_ms();
          if (status.fault_state == OK8MP_CAN_BUS_OFF &&
              now - last_recovery >=
              CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_AUTO_RECOVER_MS)
            {
              ret = can_service_recover_fd(g_fd);
              last_recovery = now;

              if (ret == OK)
                {
                  g_stats.auto_recoveries++;
                  printf("can_service: controller=recovering count=%lu\n",
                         (unsigned long)g_stats.auto_recoveries);
                }
              else
                {
                  g_stats.last_error = -ret;
                }
            }
        }
      else
        {
          g_stats.last_error = errno;
        }

      usleep(CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_POLL_MS * 1000);
    }

  close(g_fd);
  g_fd = -1;
  g_running = false;
  g_ready = false;
  g_stop = false;
  g_stats.running = false;
  printf("can_service: stopped\n");
  return EXIT_SUCCESS;
}

int ok8mp_can_service_start(void)
{
  uint32_t start;
  int pid;
  int ret;

  if (g_running && g_ready)
    {
      return OK;
    }

  if (!g_starting)
    {
      if (!g_command_sem_initialized)
        {
          ret = nxsem_init(&g_command.done, 0, 0);
          if (ret < 0)
            {
              return ret;
            }

          g_command_sem_initialized = true;
        }

      g_command.type = CAN_SERVICE_COMMAND_NONE;
      g_stop = false;
      g_ready = false;
      g_starting = true;
      pid = task_create("can_service",
                        CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_PRIORITY,
                        CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_STACKSIZE,
                        can_service_daemon, NULL);
      if (pid < 0)
        {
          g_starting = false;
          return -errno;
        }
    }

  start = can_service_now_ms();
  while (g_starting && !g_ready)
    {
      if (can_service_now_ms() - start >= CAN_SERVICE_START_TIMEOUT_MS)
        {
          return -ETIMEDOUT;
        }

      usleep(CAN_SERVICE_WAIT_STEP_US);
    }

  return g_running && g_ready ? OK : -EIO;
}

int ok8mp_can_service_stop(uint32_t timeout_ms)
{
  uint32_t start;
  unsigned int i;
  bool busy = false;

  if (!g_running)
    {
      return OK;
    }

  /* Do not close /dev/can0 while a monitor or diagnostic client still owns
   * a subscription.  This keeps the daemon, wait semaphore and device
   * lifetime ordered and avoids a close/read race.
   */

  nxmutex_lock(&g_lock);
  for (i = 0; i < CAN_SERVICE_MAX_SUBSCRIBERS; i++)
    {
      if (g_subscribers[i].active)
        {
          busy = true;
          break;
        }
    }

  nxmutex_unlock(&g_lock);
  if (busy)
    {
      return -EBUSY;
    }

  /* Serialize shutdown against a caller waiting for a daemon command. */

  nxmutex_lock(&g_tx_lock);
  g_stop = true;
  nxmutex_unlock(&g_tx_lock);
  start = can_service_now_ms();
  while (g_running)
    {
      if (can_service_now_ms() - start >= timeout_ms)
        {
          return -ETIMEDOUT;
        }

      usleep(CAN_SERVICE_WAIT_STEP_US);
    }

  return OK;
}

bool ok8mp_can_service_is_running(void)
{
  return g_running && g_ready;
}

int ok8mp_can_service_subscribe(uint32_t id, uint32_t mask,
                                bool extended,
                                FAR ok8mp_can_subscription_t *subscription)
{
  FAR struct can_service_subscriber_s *sub;
  unsigned int i;
  int ret;

  if (subscription == NULL)
    {
      return -EINVAL;
    }

  ret = ok8mp_can_service_start();
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_lock);
  for (i = 0; i < CAN_SERVICE_MAX_SUBSCRIBERS; i++)
    {
      sub = &g_subscribers[i];
      if (!sub->active)
        {
          memset(sub, 0, sizeof(*sub));
          nxsem_init(&sub->available, 0, 0);
          sub->id = id;
          sub->mask = mask;
          sub->extended = extended;
          sub->active = true;
          *subscription = (int)i;
          nxmutex_unlock(&g_lock);
          return OK;
        }
    }

  nxmutex_unlock(&g_lock);
  return -ENOSPC;
}

int ok8mp_can_service_unsubscribe(ok8mp_can_subscription_t subscription)
{
  FAR struct can_service_subscriber_s *sub;

  if (subscription < 0 ||
      subscription >= CAN_SERVICE_MAX_SUBSCRIBERS)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  sub = &g_subscribers[subscription];
  if (!sub->active)
    {
      nxmutex_unlock(&g_lock);
      return -ENOENT;
    }

  sub->active = false;
  sub->count = 0;
  sub->head = 0;
  nxsem_destroy(&sub->available);
  nxmutex_unlock(&g_lock);
  return OK;
}

int ok8mp_can_service_receive(ok8mp_can_subscription_t subscription,
                              FAR struct can_msg_s *msg,
                              uint32_t timeout_ms)
{
  FAR struct can_service_subscriber_s *sub;
  int ret;

  if (msg == NULL || subscription < 0 ||
      subscription >= CAN_SERVICE_MAX_SUBSCRIBERS)
    {
      return -EINVAL;
    }

  sub = &g_subscribers[subscription];
  if (!sub->active)
    {
      return -ENOENT;
    }

  ret = timeout_ms == 0 ? nxsem_trywait(&sub->available) :
        nxsem_tickwait_uninterruptible(&sub->available,
                                      MSEC2TICK(timeout_ms));
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_lock);
  if (!sub->active || sub->count == 0)
    {
      nxmutex_unlock(&g_lock);
      return -EAGAIN;
    }

  memcpy(msg, &sub->queue[sub->head], sizeof(*msg));
  sub->head = (sub->head + 1) % CAN_SERVICE_QUEUE_DEPTH;
  sub->count--;
  nxmutex_unlock(&g_lock);
  return OK;
}

int ok8mp_can_service_send(FAR const struct can_msg_s *msg,
                           uint32_t timeout_ms)
{
  int ret;

  if (msg == NULL || msg->cm_hdr.ch_dlc > 8)
    {
      return -EINVAL;
    }

  ret = ok8mp_can_service_start();
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_tx_lock);
  memcpy(&g_command.msg, msg, sizeof(*msg));
  g_command.timeout_ms = timeout_ms;
  g_command.type = CAN_SERVICE_COMMAND_SEND;
  ret = nxsem_wait_uninterruptible(&g_command.done);
  if (ret == OK)
    {
      ret = g_command.result;
    }

  nxmutex_unlock(&g_tx_lock);
  return ret;
}

int ok8mp_can_service_get_stats(FAR struct ok8mp_can_service_stats_s *stats)
{
  if (stats == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  memcpy(stats, &g_stats, sizeof(*stats));
  stats->running = g_running && g_ready;
  nxmutex_unlock(&g_lock);
  return OK;
}

int ok8mp_can_service_recover(void)
{
  int ret;

  ret = ok8mp_can_service_start();
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_tx_lock);
  g_command.type = CAN_SERVICE_COMMAND_RECOVER;
  ret = nxsem_wait_uninterruptible(&g_command.done);
  if (ret == OK)
    {
      ret = g_command.result;
    }

  nxmutex_unlock(&g_tx_lock);
  return ret;
}

static void can_service_print_stats(bool json)
{
  struct ok8mp_can_service_stats_s stats;

  ok8mp_can_service_get_stats(&stats);
  if (json)
    {
      printf("{\"ok\":%s,\"running\":%s,\"state\":\"%s\","
             "\"tx_error\":%u,\"rx_error\":%u,\"rx_frames\":%lu,"
             "\"tx_submitted\":%lu,\"tx_completed\":%lu,"
             "\"tx_timeouts\":%lu,\"rx_dropped\":%lu,"
             "\"bus_off_count\":%lu,\"recoveries\":%lu,"
             "\"last_error\":%d}\n",
             stats.running &&
             stats.controller.fault_state != OK8MP_CAN_BUS_OFF ?
             "true" : "false",
             stats.running ? "true" : "false",
             can_service_state_name(stats.controller.fault_state),
             stats.controller.tx_error_counter,
             stats.controller.rx_error_counter,
             (unsigned long)stats.rx_frames,
             (unsigned long)stats.tx_submitted,
             (unsigned long)stats.controller.tx_frames,
             (unsigned long)stats.tx_timeouts,
             (unsigned long)stats.rx_dropped,
             (unsigned long)stats.controller.bus_off_count,
             (unsigned long)stats.controller.recoveries,
             stats.last_error);
    }
  else
    {
      printf("can_service: running=%u state=%s txerr=%u rxerr=%u "
             "rx=%lu tx=%lu/%lu dropped=%lu timeouts=%lu "
             "busoff=%lu recoveries=%lu last_error=%d\n",
             stats.running,
             can_service_state_name(stats.controller.fault_state),
             stats.controller.tx_error_counter,
             stats.controller.rx_error_counter,
             (unsigned long)stats.rx_frames,
             (unsigned long)stats.controller.tx_frames,
             (unsigned long)stats.tx_submitted,
             (unsigned long)stats.rx_dropped,
             (unsigned long)stats.tx_timeouts,
             (unsigned long)stats.controller.bus_off_count,
             (unsigned long)stats.controller.recoveries,
             stats.last_error);
    }
}

int main(int argc, FAR char *argv[])
{
  int ret;

  if (argc != 2)
    {
      goto usage;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      ret = ok8mp_can_service_start();
      if (ret < 0)
        {
          printf("can_service: start failed: %d\n", -ret);
          return EXIT_FAILURE;
        }

      printf("can_service: running on %s\n",
             CONFIG_EXAMPLES_OK8MP_CAN_SERVICE_DEVPATH);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "stop") == 0)
    {
      ret = ok8mp_can_service_stop(1000);
      if (ret < 0)
        {
          printf("can_service: stop failed: %d\n", -ret);
          return EXIT_FAILURE;
        }

      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      can_service_print_stats(false);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "health") == 0)
    {
      can_service_print_stats(true);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "recover") == 0)
    {
      ret = ok8mp_can_service_recover();
      printf("can_service: recover=%s result=%d\n",
             ret == OK ? "ok" : "failed", ret);
      return ret == OK ? EXIT_SUCCESS : EXIT_FAILURE;
    }

usage:
  printf("Usage: %s start|stop|status|health|recover\n", argv[0]);
  return EXIT_FAILURE;
}
