/****************************************************************************
 * apps/examples/ok8mp_voice_uorb/ok8mp_voice_uorb_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/sensors/ok8mp_voice.h>
#include <sensor/voice_command.h>
#include <uORB/uORB.h>

static volatile bool g_running;
static volatile bool g_stop;
static volatile uint8_t g_last_result = 0xff;
static volatile unsigned long g_publish_count;

static void voice_uorb_feedback(int fd, uint8_t command_id)
{
#ifdef CONFIG_EXAMPLES_OK8MP_VOICE_UORB_RGB_FEEDBACK
  uint8_t rgb[3];

  memset(rgb, 0, sizeof(rgb));
  if (command_id == 1)
    {
      rgb[0] = 255;
    }
  else if (command_id == 2)
    {
      rgb[1] = 255;
    }
  else if (command_id == 3)
    {
      rgb[2] = 255;
    }
  else if (command_id != 4)
    {
      return;
    }

  if (ioctl(fd, OK8MP_VOICEIOC_SET_RGB, (unsigned long)rgb) < 0)
    {
      printf("voice_uorb: RGB feedback failed: %d\n", errno);
    }
#else
  (void)fd;
  (void)command_id;
#endif
}

static int voice_uorb_daemon(int argc, FAR char *argv[])
{
  struct sensor_voice_command sample;
  bool latched = false;
  uint8_t result;
  int afd;
  int fd;

  (void)argc;
  (void)argv;

  fd = open(OK8MP_VOICE_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      printf("voice_uorb: open %s failed: %d\n",
             OK8MP_VOICE_DEVPATH, errno);
      g_running = false;
      return EXIT_FAILURE;
    }

  memset(&sample, 0, sizeof(sample));
  sample.timestamp = orb_absolute_time();
  sample.command_id = 0xff;
  ioctl(fd, OK8MP_VOICEIOC_GET_BUSY, (unsigned long)&sample.busy);
  ioctl(fd, OK8MP_VOICEIOC_GET_COUNT, (unsigned long)&sample.word_count);

  afd = orb_advertise(ORB_ID(sensor_voice_command), &sample);
  if (afd < 0)
    {
      printf("voice_uorb: advertise failed: %d\n", errno);
      close(fd);
      g_running = false;
      return EXIT_FAILURE;
    }

  g_running = true;
  printf("voice_uorb: publishing sensor_voice_command every %d ms\n",
         CONFIG_EXAMPLES_OK8MP_VOICE_UORB_POLL_MS);

  while (!g_stop)
    {
      result = 0xff;
      if (ioctl(fd, OK8MP_VOICEIOC_GET_RESULT,
                (unsigned long)&result) == 0)
        {
          if (result == 0xff)
            {
              latched = false;
            }
          else if (!latched || result != g_last_result)
            {
              sample.timestamp = orb_absolute_time();
              sample.command_id = result;
              sample.valid = 1;
              ioctl(fd, OK8MP_VOICEIOC_GET_BUSY,
                    (unsigned long)&sample.busy);
              ioctl(fd, OK8MP_VOICEIOC_GET_COUNT,
                    (unsigned long)&sample.word_count);

              if (orb_publish(ORB_ID(sensor_voice_command), afd,
                              &sample) == 0)
                {
                  g_last_result = result;
                  g_publish_count++;
                  printf("voice_uorb: command=%u published=%lu\n",
                         result, g_publish_count);
                  voice_uorb_feedback(fd, result);
                }

              latched = true;
            }
        }

      usleep(CONFIG_EXAMPLES_OK8MP_VOICE_UORB_POLL_MS * 1000);
    }

  orb_unadvertise(afd);
  close(fd);
  g_running = false;
  g_stop = false;
  printf("voice_uorb: stopped\n");
  return EXIT_SUCCESS;
}

static int voice_uorb_listen(int count)
{
  struct sensor_voice_command sample;
  struct pollfd pfd;
  int seen = 0;
  int sfd;
  int ret;

  sfd = orb_subscribe(ORB_ID(sensor_voice_command));
  if (sfd < 0)
    {
      printf("voice_uorb: topic unavailable; run 'voice_uorb start' first\n");
      return EXIT_FAILURE;
    }

  pfd.fd = sfd;
  pfd.events = POLLIN;
  while (seen < count)
    {
      ret = poll(&pfd, 1, 10000);
      if (ret <= 0)
        {
          printf("voice_uorb: listen timeout\n");
          break;
        }

      if (orb_copy(ORB_ID(sensor_voice_command), sfd, &sample) == 0)
        {
          printf("voice_uorb: timestamp=%llu command=%u valid=%u "
                 "busy=%u words=%u\n",
                 (unsigned long long)sample.timestamp, sample.command_id,
                 sample.valid, sample.busy, sample.word_count);
          seen++;
        }
    }

  orb_close(sfd);
  return seen == count ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, FAR char *argv[])
{
  int count;
  int pid;
  int retry;

  if (argc < 2)
    {
      printf("Usage: voice_uorb start|stop|status|listen [count]\n");
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "start") == 0)
    {
      if (g_running)
        {
          printf("voice_uorb: already running\n");
          return EXIT_SUCCESS;
        }

      g_stop = false;
      pid = task_create("voice_uorb",
                        CONFIG_EXAMPLES_OK8MP_VOICE_UORB_PRIORITY,
                        CONFIG_EXAMPLES_OK8MP_VOICE_UORB_STACKSIZE,
                        voice_uorb_daemon, NULL);
      if (pid < 0)
        {
          printf("voice_uorb: task_create failed: %d\n", errno);
          return EXIT_FAILURE;
        }

      return EXIT_SUCCESS;
    }
  else if (strcmp(argv[1], "stop") == 0)
    {
      g_stop = true;
      for (retry = 0; retry < 20 && g_running; retry++)
        {
          usleep(100000);
        }

      return g_running ? EXIT_FAILURE : EXIT_SUCCESS;
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      printf("voice_uorb: running=%u last=0x%02x published=%lu\n",
             g_running, g_last_result, g_publish_count);
      return EXIT_SUCCESS;
    }
  else if (strcmp(argv[1], "listen") == 0)
    {
      count = argc > 2 ? atoi(argv[2]) : 1;
      return voice_uorb_listen(count > 0 ? count : 1);
    }

  printf("Usage: voice_uorb start|stop|status|listen [count]\n");
  return EXIT_FAILURE;
}
