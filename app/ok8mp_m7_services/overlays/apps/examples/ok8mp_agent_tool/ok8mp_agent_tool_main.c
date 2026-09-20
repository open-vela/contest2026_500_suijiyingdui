/****************************************************************************
 * apps/examples/ok8mp_agent_tool/ok8mp_agent_tool_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/sensors/ok8mp_voice.h>
#include <sensor/voice_command.h>
#include <uORB/uORB.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef int (*agent_tool_handler_t)(int argc, FAR char *argv[]);

struct agent_tool_s
{
  FAR const char *name;
  FAR const char *description;
  FAR const char *arguments;
  agent_tool_handler_t handler;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int tool_voice_status(int argc, FAR char *argv[]);
static int tool_set_rgb(int argc, FAR char *argv[]);
static int tool_set_buzzer(int argc, FAR char *argv[]);
static int tool_wait_voice(int argc, FAR char *argv[]);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct agent_tool_s g_tools[] =
{
  {
    "voice_status",
    "Read voice module version, busy state, word count and latest command",
    "{}",
    tool_voice_status
  },
  {
    "set_rgb",
    "Set the voice module RGB feedback LED",
    "{red:0..255, green:0..255, blue:0..255}",
    tool_set_rgb
  },
  {
    "set_buzzer",
    "Enable or disable the voice module buzzer",
    "{enabled:0|1}",
    tool_set_buzzer
  },
  {
    "wait_voice",
    "Wait for one sensor_voice_command event from openvela uORB",
    "{timeout_ms:1..60000}",
    tool_wait_voice
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int agent_open_voice(int flags)
{
  int fd = open(OK8MP_VOICE_DEVPATH, flags);

  if (fd < 0)
    {
      printf("{\"ok\":false,\"error\":\"open voice\",\"errno\":%d}\n",
             errno);
    }

  return fd;
}

static int tool_voice_status(int argc, FAR char *argv[])
{
  uint8_t version = 0xff;
  uint8_t busy = 0xff;
  uint8_t count = 0xff;
  uint8_t result = 0xff;
  int fd;

  (void)argc;
  (void)argv;
  fd = agent_open_voice(O_RDONLY);
  if (fd < 0)
    {
      return EXIT_FAILURE;
    }

  if (ioctl(fd, OK8MP_VOICEIOC_GET_VERSION,
            (unsigned long)&version) < 0 ||
      ioctl(fd, OK8MP_VOICEIOC_GET_BUSY,
            (unsigned long)&busy) < 0 ||
      ioctl(fd, OK8MP_VOICEIOC_GET_COUNT,
            (unsigned long)&count) < 0 ||
      ioctl(fd, OK8MP_VOICEIOC_GET_RESULT,
            (unsigned long)&result) < 0)
    {
      printf("{\"ok\":false,\"error\":\"voice ioctl\",\"errno\":%d}\n",
             errno);
      close(fd);
      return EXIT_FAILURE;
    }

  close(fd);
  printf("{\"ok\":true,\"version\":%u,\"busy\":%u,"
         "\"word_count\":%u,\"result\":%u}\n",
         version, busy, count, result);
  return EXIT_SUCCESS;
}

static int agent_parse_byte(FAR const char *text, FAR uint8_t *result)
{
  FAR char *endptr;
  long value = strtol(text, &endptr, 0);

  if (*text == '\0' || *endptr != '\0' || value < 0 || value > 255)
    {
      return -EINVAL;
    }

  *result = (uint8_t)value;
  return OK;
}

static int tool_set_rgb(int argc, FAR char *argv[])
{
  uint8_t rgb[3];
  int fd;
  int index;

  if (argc != 3)
    {
      printf("{\"ok\":false,\"error\":\"expected r g b\"}\n");
      return EXIT_FAILURE;
    }

  for (index = 0; index < 3; index++)
    {
      if (agent_parse_byte(argv[index], &rgb[index]) < 0)
        {
          printf("{\"ok\":false,\"error\":\"RGB range is 0..255\"}\n");
          return EXIT_FAILURE;
        }
    }

  fd = agent_open_voice(O_RDWR);
  if (fd < 0)
    {
      return EXIT_FAILURE;
    }

  if (ioctl(fd, OK8MP_VOICEIOC_SET_RGB, (unsigned long)rgb) < 0)
    {
      printf("{\"ok\":false,\"error\":\"set RGB\",\"errno\":%d}\n",
             errno);
      close(fd);
      return EXIT_FAILURE;
    }

  close(fd);
  printf("{\"ok\":true,\"red\":%u,\"green\":%u,\"blue\":%u}\n",
         rgb[0], rgb[1], rgb[2]);
  return EXIT_SUCCESS;
}

static int tool_set_buzzer(int argc, FAR char *argv[])
{
  uint8_t enabled;
  int fd;

  if (argc != 1 || agent_parse_byte(argv[0], &enabled) < 0 || enabled > 1)
    {
      printf("{\"ok\":false,\"error\":\"expected 0 or 1\"}\n");
      return EXIT_FAILURE;
    }

  fd = agent_open_voice(O_RDWR);
  if (fd < 0)
    {
      return EXIT_FAILURE;
    }

  if (ioctl(fd, OK8MP_VOICEIOC_SET_BUZZER,
            (unsigned long)enabled) < 0)
    {
      printf("{\"ok\":false,\"error\":\"set buzzer\",\"errno\":%d}\n",
             errno);
      close(fd);
      return EXIT_FAILURE;
    }

  close(fd);
  printf("{\"ok\":true,\"enabled\":%u}\n", enabled);
  return EXIT_SUCCESS;
}

static int tool_wait_voice(int argc, FAR char *argv[])
{
  struct sensor_voice_command sample;
  struct pollfd pfd;
  int timeout = 10000;
  int fd;
  int ret;

  if (argc > 1)
    {
      printf("{\"ok\":false,\"error\":\"expected timeout_ms\"}\n");
      return EXIT_FAILURE;
    }

  if (argc == 1)
    {
      timeout = atoi(argv[0]);
      if (timeout <= 0 || timeout > 60000)
        {
          printf("{\"ok\":false,\"error\":\"timeout range 1..60000\"}\n");
          return EXIT_FAILURE;
        }
    }

  fd = orb_subscribe(ORB_ID(sensor_voice_command));
  if (fd < 0)
    {
      printf("{\"ok\":false,\"error\":\"voice topic unavailable\","
             "\"hint\":\"run voice_uorb start\"}\n");
      return EXIT_FAILURE;
    }

  pfd.fd = fd;
  pfd.events = POLLIN;
  ret = poll(&pfd, 1, timeout);
  if (ret <= 0)
    {
      printf("{\"ok\":false,\"error\":\"timeout\"}\n");
      orb_close(fd);
      return EXIT_FAILURE;
    }

  ret = orb_copy(ORB_ID(sensor_voice_command), fd, &sample);
  orb_close(fd);
  if (ret < 0)
    {
      printf("{\"ok\":false,\"error\":\"uORB copy\",\"errno\":%d}\n",
             errno);
      return EXIT_FAILURE;
    }

  printf("{\"ok\":true,\"timestamp\":%llu,\"command_id\":%u,"
         "\"valid\":%u,\"busy\":%u,\"word_count\":%u}\n",
         (unsigned long long)sample.timestamp, sample.command_id,
         sample.valid, sample.busy, sample.word_count);
  return EXIT_SUCCESS;
}

static FAR const struct agent_tool_s *agent_find_tool(FAR const char *name)
{
  unsigned int index;

  for (index = 0; index < sizeof(g_tools) / sizeof(g_tools[0]); index++)
    {
      if (strcmp(name, g_tools[index].name) == 0)
        {
          return &g_tools[index];
        }
    }

  return NULL;
}

static void agent_list_tools(bool schema)
{
  unsigned int index;

  for (index = 0; index < sizeof(g_tools) / sizeof(g_tools[0]); index++)
    {
      if (schema)
        {
          printf("{\"name\":\"%s\",\"description\":\"%s\","
                 "\"arguments\":\"%s\"}\n",
                 g_tools[index].name, g_tools[index].description,
                 g_tools[index].arguments);
        }
      else
        {
          printf("%-14s %s\n", g_tools[index].name,
                 g_tools[index].description);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR const struct agent_tool_s *tool;

  if (argc == 2 && strcmp(argv[1], "tools") == 0)
    {
      agent_list_tools(false);
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "schema") == 0)
    {
      agent_list_tools(true);
      return EXIT_SUCCESS;
    }

  if (argc >= 3 && strcmp(argv[1], "call") == 0)
    {
      tool = agent_find_tool(argv[2]);
      if (tool == NULL)
        {
          printf("agent_tool: unknown tool '%s'\n", argv[2]);
          return EXIT_FAILURE;
        }

      return tool->handler(argc - 3, &argv[3]);
    }

  printf("Usage:\n");
  printf("  %s tools\n", argv[0]);
  printf("  %s schema\n", argv[0]);
  printf("  %s call <tool> [args...]\n", argv[0]);
  return EXIT_FAILURE;
}
