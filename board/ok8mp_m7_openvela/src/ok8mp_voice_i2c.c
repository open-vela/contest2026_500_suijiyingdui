/****************************************************************************
 * boards/arm/mx8mp/ok8mp-m7/src/ok8mp_voice_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/sensors/ok8mp_voice.h>
#include <nuttx/signal.h>

#include "mx8mp_ccm.h"
#include "mx8mp_i2c.h"
#include "mx8mp_iomuxc.h"
#include "hardware/mx8mp_ccm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define OK8MP_VOICE_REG_ADD_WORD   0x01
#define OK8MP_VOICE_REG_MODE       0x02
#define OK8MP_VOICE_REG_RGB        0x03
#define OK8MP_VOICE_REG_GAIN       0x04
#define OK8MP_VOICE_REG_CLEAR      0x05
#define OK8MP_VOICE_REG_KEY_FLAG   0x06
#define OK8MP_VOICE_REG_HINT       0x07
#define OK8MP_VOICE_REG_RESULT     0x08
#define OK8MP_VOICE_REG_BUZZER     0x09
#define OK8MP_VOICE_REG_WORD_COUNT 0x0a
#define OK8MP_VOICE_REG_VERSION    0x0b
#define OK8MP_VOICE_REG_BUSY       0x0c

#define OK8MP_VOICE_BUSY_RETRY     1000
#define OK8MP_VOICE_I2C_RETRIES    5
#define OK8MP_VOICE_RETRY_DELAY_US 1000

/* Linux identifies the module on /dev/i2c-2; on the M7 the same hardware is
 * I2C3 using the dedicated I2C3_SCL/I2C3_SDA pads.  NXP's i.MX8MP EVK DTS
 * uses pad control 0x1c2; SION is passed separately to
 * mx8mp_iomuxc_config(). */

#define OK8MP_VOICE_I2C_PAD_CTRL 0x1c2
#define IOMUX_VOICE_I2C3_SCL \
  IOMUXC_I2C3_SCL_I2C3_SCL, 1, OK8MP_VOICE_I2C_PAD_CTRL
#define IOMUX_VOICE_I2C3_SDA \
  IOMUXC_I2C3_SDA_I2C3_SDA, 1, OK8MP_VOICE_I2C_PAD_CTRL

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ok8mp_voice_i2c_s
{
  FAR struct i2c_master_s *i2c;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t ok8mp_voice_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
static ssize_t ok8mp_voice_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
static int ok8mp_voice_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ok8mp_voice_i2c_s g_voice;

static const struct file_operations g_voice_fops =
{
  NULL,              /* open */
  NULL,              /* close */
  ok8mp_voice_read,  /* read */
  ok8mp_voice_write, /* write */
  NULL,              /* seek */
  ok8mp_voice_ioctl, /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool ok8mp_voice_retryable(int ret)
{
  /* MX8MP's I2C driver reports arbitration loss as -EACCES.  The voice
   * uORB bridge polls the same device while Agent tools may access it, so a
   * short-lived arbitration/busy condition must not become a permanent tool
   * failure. */

  return ret == -EACCES || ret == -EBUSY || ret == -EAGAIN ||
         ret == -ETIMEDOUT;
}

static int ok8mp_voice_transfer(FAR struct ok8mp_voice_i2c_s *priv,
                                FAR struct i2c_msg_s *msgs, int count)
{
  int attempt;
  int ret = -EIO;

  for (attempt = 0; attempt < OK8MP_VOICE_I2C_RETRIES; attempt++)
    {
      ret = I2C_TRANSFER(priv->i2c, msgs, count);
      if (ret >= 0 || !ok8mp_voice_retryable(ret))
        {
          break;
        }

      nxsig_usleep(OK8MP_VOICE_RETRY_DELAY_US * (attempt + 1));
    }

  return ret;
}

static int ok8mp_voice_readreg(FAR struct ok8mp_voice_i2c_s *priv,
                               uint8_t reg, FAR uint8_t *value)
{
  struct i2c_msg_s msg[2];
  uint8_t addr = reg;

  msg[0].frequency = CONFIG_OK8MP_M7_VOICE_I2C_FREQ;
  msg[0].addr      = CONFIG_OK8MP_M7_VOICE_I2C_ADDR;
  msg[0].flags     = 0;
  msg[0].buffer    = &addr;
  msg[0].length    = 1;

  msg[1].frequency = CONFIG_OK8MP_M7_VOICE_I2C_FREQ;
  msg[1].addr      = CONFIG_OK8MP_M7_VOICE_I2C_ADDR;
  msg[1].flags     = I2C_M_READ;
  msg[1].buffer    = value;
  msg[1].length    = 1;

  return ok8mp_voice_transfer(priv, msg, 2);
}

static int ok8mp_voice_writereg(FAR struct ok8mp_voice_i2c_s *priv,
                                uint8_t reg, FAR const uint8_t *data,
                                size_t len)
{
  struct i2c_msg_s msg;
  uint8_t buffer[OK8MP_VOICE_WORD_MAX + 4];

  if (len > sizeof(buffer) - 1)
    {
      return -EINVAL;
    }

  buffer[0] = reg;
  if (len > 0)
    {
      memcpy(&buffer[1], data, len);
    }

  msg.frequency = CONFIG_OK8MP_M7_VOICE_I2C_FREQ;
  msg.addr      = CONFIG_OK8MP_M7_VOICE_I2C_ADDR;
  msg.flags     = 0;
  msg.buffer    = buffer;
  msg.length    = len + 1;

  return ok8mp_voice_transfer(priv, &msg, 1);
}

static int ok8mp_voice_writebyte(FAR struct ok8mp_voice_i2c_s *priv,
                                 uint8_t reg, uint8_t value)
{
  return ok8mp_voice_writereg(priv, reg, &value, 1);
}

static int ok8mp_voice_wait_ready(FAR struct ok8mp_voice_i2c_s *priv)
{
  uint8_t busy;
  int retry;
  int ret;

  for (retry = 0; retry < OK8MP_VOICE_BUSY_RETRY; retry++)
    {
      ret = ok8mp_voice_readreg(priv, OK8MP_VOICE_REG_BUSY, &busy);
      if (ret < 0)
        {
          return ret;
        }

      if (busy == 0)
        {
          return 0;
        }
    }

  return -ETIMEDOUT;
}

static int ok8mp_voice_add_word(FAR struct ok8mp_voice_i2c_s *priv,
                                FAR const struct ok8mp_voice_word_s *word)
{
  uint8_t payload[OK8MP_VOICE_WORD_MAX + 3];
  size_t len;
  int ret;

  if (word == NULL)
    {
      return -EINVAL;
    }

  len = strnlen(word->pinyin, OK8MP_VOICE_WORD_MAX + 1);
  if (len == 0 || len > OK8MP_VOICE_WORD_MAX)
    {
      return -EINVAL;
    }

  payload[0] = (uint8_t)(len + 2);
  payload[1] = word->id;
  memcpy(&payload[2], word->pinyin, len);
  payload[len + 2] = 0;

  ret = ok8mp_voice_writereg(priv, OK8MP_VOICE_REG_ADD_WORD,
                             payload, len + 3);
  return ret < 0 ? ret : ok8mp_voice_wait_ready(priv);
}

static ssize_t ok8mp_voice_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen)
{
  FAR struct inode *inode;
  FAR struct ok8mp_voice_i2c_s *priv;
  uint8_t value;
  int ret;

  if (buflen < 1)
    {
      return 0;
    }

  if (filep->f_pos > 0)
    {
      return 0;
    }

  inode = filep->f_inode;
  priv = inode->i_private;

  ret = ok8mp_voice_readreg(priv, CONFIG_OK8MP_M7_VOICE_I2C_REG, &value);
  if (ret < 0)
    {
      return ret;
    }

  buffer[0] = value;
  filep->f_pos++;
  return 1;
}

static ssize_t ok8mp_voice_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen)
{
  FAR struct inode *inode;
  FAR struct ok8mp_voice_i2c_s *priv;
  uint8_t reg;
  int ret;

  if (buflen < 2)
    {
      return -EINVAL;
    }

  inode = filep->f_inode;
  priv = inode->i_private;
  reg = (uint8_t)buffer[0];

  ret = ok8mp_voice_writereg(priv, reg, (FAR const uint8_t *)&buffer[1],
                             buflen - 1);
  if (ret < 0)
    {
      return ret;
    }

  if (reg == OK8MP_VOICE_REG_ADD_WORD || reg == OK8MP_VOICE_REG_MODE ||
      reg == OK8MP_VOICE_REG_CLEAR)
    {
      ret = ok8mp_voice_wait_ready(priv);
      if (ret < 0)
        {
          return ret;
        }
    }

  return buflen;
}

static int ok8mp_voice_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct inode *inode;
  FAR struct ok8mp_voice_i2c_s *priv;
  FAR uint8_t *ptr;
  uint8_t value;
  int ret;

  inode = filep->f_inode;
  priv = inode->i_private;

  switch (cmd)
    {
      case OK8MP_VOICEIOC_GET_RESULT:
        value = OK8MP_VOICE_REG_RESULT;
        break;

      case OK8MP_VOICEIOC_GET_VERSION:
        value = OK8MP_VOICE_REG_VERSION;
        break;

      case OK8MP_VOICEIOC_GET_BUSY:
        value = OK8MP_VOICE_REG_BUSY;
        break;

      case OK8MP_VOICEIOC_GET_COUNT:
        value = OK8MP_VOICE_REG_WORD_COUNT;
        break;

      case OK8MP_VOICEIOC_SET_MODE:
        ret = ok8mp_voice_writebyte(priv, OK8MP_VOICE_REG_MODE,
                                    (uint8_t)arg);
        return ret < 0 ? ret : ok8mp_voice_wait_ready(priv);

      case OK8MP_VOICEIOC_SET_GAIN:
        return ok8mp_voice_writebyte(priv, OK8MP_VOICE_REG_GAIN,
                                     (uint8_t)arg);

      case OK8MP_VOICEIOC_SET_HINT:
        return ok8mp_voice_writebyte(priv, OK8MP_VOICE_REG_HINT,
                                     (uint8_t)arg);

      case OK8MP_VOICEIOC_SET_BUZZER:
        return ok8mp_voice_writebyte(priv, OK8MP_VOICE_REG_BUZZER,
                                     (uint8_t)arg);

      case OK8MP_VOICEIOC_SET_RGB:
        if (arg == 0)
          {
            return -EINVAL;
          }

        return ok8mp_voice_writereg(priv, OK8MP_VOICE_REG_RGB,
                                    (FAR const uint8_t *)arg, 3);

      case OK8MP_VOICEIOC_CLEAR:
        ret = ok8mp_voice_writebyte(priv, OK8MP_VOICE_REG_CLEAR, 0);
        return ret < 0 ? ret : ok8mp_voice_wait_ready(priv);

      case OK8MP_VOICEIOC_ADD_WORD:
        return ok8mp_voice_add_word(
          priv, (FAR const struct ok8mp_voice_word_s *)arg);

      default:
        return -ENOTTY;
    }

  if (arg == 0)
    {
      return -EINVAL;
    }

  ptr = (FAR uint8_t *)arg;
  ret = ok8mp_voice_readreg(priv, value, ptr);
  return ret < 0 ? ret : 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ok8mp_voice_i2c_prepare(void)
{
  int ret;

  /* Use the always-available 24 MHz oscillator.  The I2C lower half derives
   * 100 kHz from this root through IFDR. */

  ret = mx8mp_ccm_configure_clock(I2C3_CLK_ROOT, OSC_24M_REF_CLK, 1, 1);
  if (ret < 0)
    {
      return ret;
    }

  mx8mp_ccm_enable_clock(I2C3_CLK_ROOT);
  mx8mp_ccm_gate_clock(CCM_I2C3_CLK_GATE, CLK_ALWAYS_NEEDED);

  mx8mp_iomuxc_config(IOMUX_VOICE_I2C3_SCL);
  mx8mp_iomuxc_config(IOMUX_VOICE_I2C3_SDA);
  return OK;
}

int ok8mp_voice_i2c_initialize(void)
{
  uint8_t version = 0xff;
  int ret;

  g_voice.i2c =
    mx8mp_i2cbus_initialize(CONFIG_OK8MP_M7_VOICE_I2C_BUS);
  if (g_voice.i2c == NULL)
    {
      return -ENODEV;
    }

  ret = register_driver(OK8MP_VOICE_DEVPATH, &g_voice_fops, 0666,
                        &g_voice);
  if (ret < 0)
    {
      return ret;
    }

  ret = ok8mp_voice_readreg(&g_voice, OK8MP_VOICE_REG_VERSION, &version);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "OK8MP voice: I2C3 probe addr=0x%02x failed: %d\n",
             CONFIG_OK8MP_M7_VOICE_I2C_ADDR, ret);
    }
  else
    {
      syslog(LOG_INFO,
             "OK8MP voice: I2C3 addr=0x%02x version=0x%02x ready\n",
             CONFIG_OK8MP_M7_VOICE_I2C_ADDR, version);
    }

  return OK;
}
