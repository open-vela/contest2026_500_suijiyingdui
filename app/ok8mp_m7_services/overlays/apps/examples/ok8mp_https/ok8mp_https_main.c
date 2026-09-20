/****************************************************************************
 * apps/examples/ok8mp_https/ok8mp_https_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "ok8mp_ca_bundle.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HTTPS_PORT             "443"
#define HTTPS_REQUEST_SIZE     4096
#define HTTPS_RESPONSE_SIZE    1024
#define HTTPS_PROMPT_SIZE      1024
#define HTTPS_VALID_TIME_MIN   1704067200L /* 2024-01-01 UTC */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct https_context_s
{
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_context entropy;
  mbedtls_x509_crt ca;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef MBEDTLS_SSL_ALPN
static FAR const char *g_https_protocols[] =
{
  "http/1.1",
  NULL
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void https_print_error(FAR const char *where, int ret)
{
#ifdef MBEDTLS_ERROR_C
  char buffer[128];

  mbedtls_strerror(ret, buffer, sizeof(buffer));
  printf("https_client: %s failed: -0x%04x (%s)\n",
         where, -ret, buffer);
#else
  printf("https_client: %s failed: -0x%04x\n", where, -ret);
#endif
}

static void https_context_init(FAR struct https_context_s *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  mbedtls_net_init(&ctx->net);
  mbedtls_ssl_init(&ctx->ssl);
  mbedtls_ssl_config_init(&ctx->conf);
  mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
  mbedtls_entropy_init(&ctx->entropy);
  mbedtls_x509_crt_init(&ctx->ca);
}

static void https_context_free(FAR struct https_context_s *ctx)
{
  mbedtls_ssl_close_notify(&ctx->ssl);
  mbedtls_net_free(&ctx->net);
  mbedtls_x509_crt_free(&ctx->ca);
  mbedtls_ssl_free(&ctx->ssl);
  mbedtls_ssl_config_free(&ctx->conf);
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
}

static int https_parse_ca(FAR struct https_context_s *ctx,
                          FAR const unsigned char *data, size_t length,
                          FAR const char *name)
{
  int ret;

  ret = mbedtls_x509_crt_parse(&ctx->ca, data, length);
  if (ret != 0)
    {
      https_print_error(name, ret);
      return ret < 0 ? ret : -EINVAL;
    }

  return 0;
}

static int https_load_ca_file(FAR struct https_context_s *ctx,
                              FAR const char *path)
{
  FAR unsigned char *data;
  long length;
  FILE *stream;
  int ret;

  stream = fopen(path, "rb");
  if (stream == NULL)
    {
      printf("https_client: cannot open CA file %s: %d\n", path, errno);
      return -errno;
    }

  if (fseek(stream, 0, SEEK_END) < 0 ||
      (length = ftell(stream)) <= 0 ||
      fseek(stream, 0, SEEK_SET) < 0)
    {
      fclose(stream);
      return -EIO;
    }

  data = malloc((size_t)length + 1);
  if (data == NULL)
    {
      fclose(stream);
      return -ENOMEM;
    }

  if (fread(data, 1, (size_t)length, stream) != (size_t)length)
    {
      free(data);
      fclose(stream);
      return -EIO;
    }

  fclose(stream);
  data[length] = '\0';
  ret = https_parse_ca(ctx, data, (size_t)length + 1, "CA file parse");
  free(data);
  return ret;
}

static int https_check_time(void)
{
#ifdef CONFIG_EXAMPLES_OK8MP_HTTPS_REQUIRE_TIME
  time_t now = time(NULL);

  if (now < (time_t)HTTPS_VALID_TIME_MIN)
    {
      printf("https_client: system time is not valid (%ld)\n", (long)now);
      printf("https_client: run ntpcstart <NTP-server> before TLS\n");
      return -ETIME;
    }
#endif

  return 0;
}

static int https_probe(FAR const char *host, FAR const char *port)
{
  struct addrinfo hints;
  FAR struct addrinfo *result;
  FAR struct addrinfo *addr;
  struct timeval timeout;
  char numeric[INET6_ADDRSTRLEN];
  FAR void *raw;
  int family;
  int sockfd;
  int ret;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  printf("https_client: resolving %s\n", host);
  ret = getaddrinfo(host, port, &hints, &result);
  if (ret != 0)
    {
      printf("https_client: DNS failed for %s: %d\n", host, ret);
      return -EHOSTUNREACH;
    }

  ret = -ECONNREFUSED;
  for (addr = result; addr != NULL; addr = addr->ai_next)
    {
      family = addr->ai_family;
      if (family == AF_INET)
        {
          raw = &((FAR struct sockaddr_in *)addr->ai_addr)->sin_addr;
        }
      else if (family == AF_INET6)
        {
          raw = &((FAR struct sockaddr_in6 *)addr->ai_addr)->sin6_addr;
        }
      else
        {
          continue;
        }

      if (inet_ntop(family, raw, numeric, sizeof(numeric)) == NULL)
        {
          strlcpy(numeric, "?", sizeof(numeric));
        }

      printf("https_client: DNS %s -> %s\n", host, numeric);
      sockfd = socket(family, SOCK_STREAM, 0);
      if (sockfd < 0)
        {
          ret = -errno;
          continue;
        }

      timeout.tv_sec = CONFIG_EXAMPLES_OK8MP_HTTPS_TIMEOUT;
      timeout.tv_usec = 0;
      setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout));
      setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout));

      if (connect(sockfd, addr->ai_addr, addr->ai_addrlen) == 0)
        {
          printf("https_client: TCP connected to %s:%s\n", numeric, port);
          close(sockfd);
          ret = 0;
          break;
        }

      ret = -errno;
      printf("https_client: TCP connect %s:%s failed: %d\n",
             numeric, port, errno);
      close(sockfd);
    }

  freeaddrinfo(result);
  return ret;
}

static int https_connect(FAR struct https_context_s *ctx,
                         FAR const char *host, FAR const char *ca_path)
{
  static FAR const unsigned char personal[] = "ok8mp_https";
  struct timeval timeout;
  uint32_t flags;
  int ret;

  ret = https_check_time();
  if (ret < 0)
    {
      return ret;
    }

  ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                              &ctx->entropy, personal,
                              sizeof(personal) - 1);
  if (ret != 0)
    {
      https_print_error("ctr_drbg_seed", ret);
      printf("https_client: verify that hardware /dev/urandom works\n");
      return ret;
    }

  ret = https_parse_ca(ctx,
                       (FAR const unsigned char *)g_ok8mp_ca_bundle,
                       sizeof(g_ok8mp_ca_bundle), "built-in CA parse");
  if (ret < 0)
    {
      return ret;
    }

  if (ca_path != NULL)
    {
      ret = https_load_ca_file(ctx, ca_path);
      if (ret < 0)
        {
          return ret;
        }
    }

  printf("https_client: connecting %s:%s\n", host, HTTPS_PORT);
  ret = mbedtls_net_connect(&ctx->net, host, HTTPS_PORT,
                            MBEDTLS_NET_PROTO_TCP);
  if (ret != 0)
    {
      https_print_error("net_connect", ret);
      return ret;
    }

  timeout.tv_sec = CONFIG_EXAMPLES_OK8MP_HTTPS_TIMEOUT;
  timeout.tv_usec = 0;
  setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO,
             &timeout, sizeof(timeout));
  setsockopt(ctx->net.fd, SOL_SOCKET, SO_SNDTIMEO,
             &timeout, sizeof(timeout));

  ret = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      https_print_error("ssl_config_defaults", ret);
      return ret;
    }

#ifdef MBEDTLS_SSL_ALPN
  mbedtls_ssl_conf_alpn_protocols(&ctx->conf, g_https_protocols);
#endif

  mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random,
                       &ctx->ctr_drbg);
  mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->ca, NULL);
  mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);

  ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
  if (ret != 0)
    {
      https_print_error("ssl_setup", ret);
      return ret;
    }

  ret = mbedtls_ssl_set_hostname(&ctx->ssl, host);
  if (ret != 0)
    {
      https_print_error("ssl_set_hostname", ret);
      return ret;
    }

  mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net, mbedtls_net_send,
                      mbedtls_net_recv, NULL);

  do
    {
      ret = mbedtls_ssl_handshake(&ctx->ssl);
    }
  while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
         ret == MBEDTLS_ERR_SSL_WANT_WRITE);

  if (ret != 0)
    {
      https_print_error("ssl_handshake", ret);
      return ret;
    }

  flags = mbedtls_ssl_get_verify_result(&ctx->ssl);
  if (flags != 0)
    {
      printf("https_client: certificate verification failed: 0x%08lx\n",
             (unsigned long)flags);
      return -EACCES;
    }

  printf("https_client: TLS established: %s / %s / certificate verified\n",
         mbedtls_ssl_get_version(&ctx->ssl),
         mbedtls_ssl_get_ciphersuite(&ctx->ssl));
  return 0;
}

static int https_write_all(FAR mbedtls_ssl_context *ssl,
                           FAR const unsigned char *data, size_t length)
{
  size_t sent = 0;
  int ret;

  while (sent < length)
    {
      ret = mbedtls_ssl_write(ssl, data + sent, length - sent);
      if (ret > 0)
        {
          sent += (size_t)ret;
        }
      else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
               ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          https_print_error("ssl_write", ret);
          return ret;
        }
    }

  return 0;
}

static FAR FILE *https_log_open(FAR const char *operation,
                                FAR const char *host,
                                FAR const char *path,
                                FAR const char *body)
{
  FAR const char *log_path = getenv("LLM_LOG_PATH");
  FAR FILE *stream;
  time_t now;

  if (log_path == NULL || log_path[0] == '\0')
    {
      log_path = CONFIG_EXAMPLES_OK8MP_HTTPS_LOG_PATH;
    }

  if (log_path[0] == '\0' || strcmp(log_path, "none") == 0)
    {
      return NULL;
    }

  stream = fopen(log_path, "a");
  if (stream == NULL)
    {
      printf("https_client: cannot open log %s: %d\n", log_path, errno);
      return NULL;
    }

  now = time(NULL);
  fprintf(stream, "\n=== %ld %s https://%s%s ===\n",
          (long)now, operation, host, path);
  if (body != NULL)
    {
      fprintf(stream, "REQUEST-BODY %s\n", body);
    }

  fprintf(stream, "RESPONSE\n");
  fflush(stream);
  printf("https_client: logging request/response to %s (API key omitted)\n",
         log_path);
  return stream;
}

static int https_exchange(FAR struct https_context_s *ctx,
                          FAR const char *request, FAR FILE *log)
{
  unsigned char response[HTTPS_RESPONSE_SIZE + 1];
  bool status_seen = false;
  int status = 0;
  int total = 0;
  int ret;

  ret = https_write_all(&ctx->ssl, (FAR const unsigned char *)request,
                        strlen(request));
  if (ret < 0)
    {
      return ret;
    }

  printf("https_client: response follows\n");
  for (; ; )
    {
      ret = mbedtls_ssl_read(&ctx->ssl, response, HTTPS_RESPONSE_SIZE);
      if (ret > 0)
        {
          response[ret] = '\0';
          if (!status_seen)
            {
              if (sscanf((FAR char *)response, "HTTP/%*u.%*u %d", &status)
                  == 1)
                {
                  status_seen = true;
                }
            }

          fwrite(response, 1, (size_t)ret, stdout);
          if (log != NULL)
            {
              fwrite(response, 1, (size_t)ret, log);
              fflush(log);
            }

          total += ret;
        }
      else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
          break;
        }
      else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
               ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          https_print_error("ssl_read", ret);
          return ret;
        }
    }

  printf("\nhttps_client: HTTP status=%d received=%d bytes\n",
         status, total);
  if (log != NULL)
    {
      fprintf(log, "\nRESULT status=%d bytes=%d\n", status, total);
      fflush(log);
    }

  if (total <= 0)
    {
      return -EIO;
    }

  return status >= 200 && status < 300 ? 0 : -EPROTO;
}

static int json_escape(FAR const char *input, FAR char *output,
                       size_t output_size)
{
  static FAR const char hex[] = "0123456789abcdef";
  unsigned char ch;
  size_t used = 0;

  while (*input != '\0')
    {
      ch = (unsigned char)*input++;
      if (ch == '"' || ch == '\\')
        {
          if (used + 2 >= output_size)
            {
              return -E2BIG;
            }

          output[used++] = '\\';
          output[used++] = (char)ch;
        }
      else if (ch < 0x20)
        {
          if (used + 6 >= output_size)
            {
              return -E2BIG;
            }

          output[used++] = '\\';
          output[used++] = 'u';
          output[used++] = '0';
          output[used++] = '0';
          output[used++] = hex[ch >> 4];
          output[used++] = hex[ch & 0x0f];
        }
      else
        {
          if (used + 1 >= output_size)
            {
              return -E2BIG;
            }

          output[used++] = (char)ch;
        }
    }

  output[used] = '\0';
  return 0;
}

static void https_usage(FAR const char *progname)
{
  printf("Usage:\n");
  printf("  %s probe <host> [port]\n", progname);
  printf("  %s get <host> <path> [extra-ca.pem]\n", progname);
  printf("  %s chat <host> <path> <model> <prompt> [extra-ca.pem]\n",
         progname);
  printf("Set LLM_API_KEY for chat and optional LLM_LOG_PATH for logs.\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR struct https_context_s *ctx = NULL;
  FAR const char *ca_path = NULL;
  FAR const char *api_key;
  FAR char *request = NULL;
  FAR char *escaped = NULL;
  FAR char *body = NULL;
  FAR FILE *log = NULL;
  int ret = EXIT_FAILURE;
  int length;

  if (argc >= 3 && strcmp(argv[1], "probe") == 0)
    {
      return https_probe(argv[2], argc > 3 ? argv[3] : HTTPS_PORT) < 0 ?
             EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc < 4)
    {
      https_usage(argv[0]);
      return EXIT_FAILURE;
    }

  ctx = calloc(1, sizeof(*ctx));
  request = malloc(HTTPS_REQUEST_SIZE);
  escaped = malloc(HTTPS_PROMPT_SIZE * 2);
  body = malloc(HTTPS_REQUEST_SIZE);
  if (ctx == NULL || request == NULL || escaped == NULL || body == NULL)
    {
      printf("https_client: out of memory\n");
      goto out;
    }

  https_context_init(ctx);
  if (strcmp(argv[1], "get") == 0)
    {
      ca_path = argc > 4 ? argv[4] : NULL;
      ret = https_connect(ctx, argv[2], ca_path);
      if (ret < 0)
        {
          goto tls_out;
        }

      length = snprintf(request, HTTPS_REQUEST_SIZE,
                        "GET %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "User-Agent: openvela-ok8mp/1.0\r\n"
                        "Accept: */*\r\n"
                        "Connection: close\r\n\r\n",
                        argv[3], argv[2]);
      log = https_log_open("GET", argv[2], argv[3], NULL);
    }
  else if (strcmp(argv[1], "chat") == 0 && argc >= 6)
    {
      api_key = getenv("LLM_API_KEY");
      if (api_key == NULL || api_key[0] == '\0')
        {
          printf("https_client: LLM_API_KEY is not set\n");
          goto tls_out;
        }

      ca_path = argc > 6 ? argv[6] : NULL;
      if (json_escape(argv[5], escaped, HTTPS_PROMPT_SIZE * 2) < 0)
        {
          printf("https_client: prompt is too long\n");
          goto tls_out;
        }

      length = snprintf(body, HTTPS_REQUEST_SIZE,
                        "{\"model\":\"%s\",\"messages\":["
                        "{\"role\":\"user\",\"content\":\"%s\"}],"
                        "\"stream\":false}",
                        argv[4], escaped);
      if (length < 0 || length >= HTTPS_REQUEST_SIZE)
        {
          printf("https_client: JSON body is too long\n");
          goto tls_out;
        }

      ret = https_connect(ctx, argv[2], ca_path);
      if (ret < 0)
        {
          goto tls_out;
        }

      length = snprintf(request, HTTPS_REQUEST_SIZE,
                        "POST %s HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Authorization: Bearer %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Accept: application/json\r\n"
                        "Content-Length: %u\r\n"
                        "Connection: close\r\n\r\n%s",
                        argv[3], argv[2], api_key,
                        (unsigned int)strlen(body), body);
      log = https_log_open("CHAT", argv[2], argv[3], body);
    }
  else
    {
      https_usage(argv[0]);
      goto tls_out;
    }

  if (length < 0 || length >= HTTPS_REQUEST_SIZE)
    {
      printf("https_client: request is too long\n");
      goto tls_out;
    }

  ret = https_exchange(ctx, request, log) < 0 ?
        EXIT_FAILURE : EXIT_SUCCESS;

tls_out:
  if (log != NULL)
    {
      fclose(log);
    }

  https_context_free(ctx);
out:
  free(body);
  free(escaped);
  free(request);
  free(ctx);
  return ret;
}
