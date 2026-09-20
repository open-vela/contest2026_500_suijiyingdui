/****************************************************************************
 * apps/examples/ok8mp_voice/ok8mp_voice_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/sensors/ok8mp_voice.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The application and future Agent tools share the driver's public API. */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void voice_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s\n", progname);
  printf("  %s result\n", progname);
  printf("  %s buzzer <0|1>\n", progname);
  printf("  %s rgb <r> <g> <b>\n", progname);
  printf("  %s mode <0|1|2>\n", progname);
  printf("  %s gain <0..127>\n", progname);
  printf("  %s hint <0|1>\n", progname);
  printf("  %s clear\n", progname);
  printf("  %s add <id> <pinyin words...>\n", progname);
  printf("  %s init-defaults\n", progname);
}

static int voice_add_word(int fd, uint8_t id, FAR const char *pinyin)
{
  struct ok8mp_voice_word_s word;

  memset(&word, 0, sizeof(word));
  word.id = id;
  strlcpy(word.pinyin, pinyin, sizeof(word.pinyin));

  if (ioctl(fd, OK8MP_VOICEIOC_ADD_WORD, (unsigned long)&word) < 0)
    {
      printf("add word %u '%s' failed: %d\n", id, pinyin, errno);
      return -errno;
    }

  printf("word %u added: %s\n", id, pinyin);
  return 0;
}

static int voice_init_defaults(int fd)
{
  static const struct
  {
    uint8_t id;
    FAR const char *pinyin;
  } defaults[] =
  {
    {0, "xiao ya"},
    {1, "hong deng"},
    {2, "lv deng"},
    {3, "lan deng"},
    {4, "guan deng"}
  };
  uint8_t value;
  int ret;
  int i;

  ret = ioctl(fd, OK8MP_VOICEIOC_CLEAR, 0);
  if (ret < 0)
    {
      printf("clear failed: %d\n", errno);
      return -errno;
    }

  for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
    {
      ret = voice_add_word(fd, defaults[i].id, defaults[i].pinyin);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = ioctl(fd, OK8MP_VOICEIOC_SET_MODE, 1);
  if (ret < 0)
    {
      printf("set mode failed: %d\n", errno);
      return -errno;
    }

  ioctl(fd, OK8MP_VOICEIOC_SET_GAIN, 0x48);
  ioctl(fd, OK8MP_VOICEIOC_SET_HINT, 1);

  value = 0xff;
  ioctl(fd, OK8MP_VOICEIOC_GET_COUNT, (unsigned long)&value);
  printf("default words ready: count=%u mode=1 gain=0x48\n", value);
  return 0;
}

static int voice_get(int fd, int cmd, FAR const char *name)
{
  uint8_t value = 0;
  int ret;

  ret = ioctl(fd, cmd, (unsigned long)&value);
  if (ret < 0)
    {
      printf("%s: ioctl failed: %d\n", name, errno);
      return -errno;
    }

  printf("%s: 0x%02x (%u)\n", name, value, value);
  return 0;
}

static int voice_set_u8(int fd, int cmd, FAR const char *name,
                        FAR const char *arg)
{
  long value;
  FAR char *endptr;
  int ret;

  value = strtol(arg, &endptr, 0);
  if (*endptr != '\0' || value < 0 || value > 255)
    {
      printf("%s: invalid value: %s\n", name, arg);
      return -EINVAL;
    }

  ret = ioctl(fd, cmd, (unsigned long)value);
  if (ret < 0)
    {
      printf("%s: ioctl failed: %d\n", name, errno);
      return -errno;
    }

  printf("%s set to %ld\n", name, value);
  return 0;
}

static int voice_set_rgb(int fd, FAR char *r, FAR char *g, FAR char *b)
{
  uint8_t rgb[3];
  FAR char *endptr;
  long value;
  int i;
  FAR char *args[3] =
  {
    r, g, b
  };

  for (i = 0; i < 3; i++)
    {
      value = strtol(args[i], &endptr, 0);
      if (*endptr != '\0' || value < 0 || value > 255)
        {
          printf("rgb: invalid value: %s\n", args[i]);
          return -EINVAL;
        }

      rgb[i] = value;
    }

  if (ioctl(fd, OK8MP_VOICEIOC_SET_RGB, (unsigned long)rgb) < 0)
    {
      printf("rgb: ioctl failed: %d\n", errno);
      return -errno;
    }

  printf("rgb set to %u,%u,%u\n", rgb[0], rgb[1], rgb[2]);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct ok8mp_voice_word_s word;
  uint8_t result;
  long id;
  FAR char *endptr;
  int fd;
  int i;
  int ret = 0;

  fd = open(OK8MP_VOICE_DEVPATH, O_RDWR);
  if (fd < 0)
    {
      printf("open %s failed: %d\n", OK8MP_VOICE_DEVPATH, errno);
      return EXIT_FAILURE;
    }

  if (argc == 1)
    {
      voice_get(fd, OK8MP_VOICEIOC_GET_VERSION, "version");
      voice_get(fd, OK8MP_VOICEIOC_GET_BUSY, "busy");
      voice_get(fd, OK8MP_VOICEIOC_GET_COUNT, "word_count");
      ret = voice_get(fd, OK8MP_VOICEIOC_GET_RESULT, "result");
    }
  else if (strcmp(argv[1], "result") == 0)
    {
      ret = read(fd, &result, 1);
      if (ret == 1)
        {
          printf("result: 0x%02x (%u)\n", result, result);
          ret = 0;
        }
      else
        {
          printf("read failed: %d\n", ret < 0 ? errno : 0);
          ret = -EIO;
        }
    }
  else if (strcmp(argv[1], "buzzer") == 0 && argc == 3)
    {
      ret = voice_set_u8(fd, OK8MP_VOICEIOC_SET_BUZZER, "buzzer", argv[2]);
    }
  else if (strcmp(argv[1], "rgb") == 0 && argc == 5)
    {
      ret = voice_set_rgb(fd, argv[2], argv[3], argv[4]);
    }
  else if (strcmp(argv[1], "mode") == 0 && argc == 3)
    {
      ret = voice_set_u8(fd, OK8MP_VOICEIOC_SET_MODE, "mode", argv[2]);
    }
  else if (strcmp(argv[1], "gain") == 0 && argc == 3)
    {
      ret = voice_set_u8(fd, OK8MP_VOICEIOC_SET_GAIN, "gain", argv[2]);
    }
  else if (strcmp(argv[1], "hint") == 0 && argc == 3)
    {
      ret = voice_set_u8(fd, OK8MP_VOICEIOC_SET_HINT, "hint", argv[2]);
    }
  else if (strcmp(argv[1], "clear") == 0)
    {
      ret = ioctl(fd, OK8MP_VOICEIOC_CLEAR, 0);
      if (ret < 0)
        {
          printf("clear: ioctl failed: %d\n", errno);
          ret = -errno;
        }
      else
        {
          printf("clear done\n");
        }
    }
  else if (strcmp(argv[1], "add") == 0 && argc >= 4)
    {
      id = strtol(argv[2], &endptr, 0);
      if (*endptr != '\0' || id < 0 || id > 254)
        {
          printf("add: invalid id: %s\n", argv[2]);
          ret = -EINVAL;
        }
      else
        {
          memset(&word, 0, sizeof(word));
          word.id = (uint8_t)id;
          for (i = 3; i < argc; i++)
            {
              if (i > 3)
                {
                  strlcat(word.pinyin, " ", sizeof(word.pinyin));
                }

              strlcat(word.pinyin, argv[i], sizeof(word.pinyin));
            }

          ret = voice_add_word(fd, word.id, word.pinyin);
        }
    }
  else if (strcmp(argv[1], "init-defaults") == 0)
    {
      ret = voice_init_defaults(fd);
    }
  else
    {
      voice_usage(argv[0]);
      ret = -EINVAL;
    }

  close(fd);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
