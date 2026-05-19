/* Socket trace: connect to a running yaAGC, send a configurable key sequence,
 * log every channel packet to/from with timestamps, and write a trace file.
 *
 * Usage:
 *   trace_yaagc <port> <out.trace> [key:HOLD_MS]...
 *
 * Where each "key:HOLD_MS" is a keystroke + how long to hold before release.
 * Special keys: V (verb), N (noun), E (enter), R (reset), P (proceed), KR
 * (key release). Digits 0-9 as themselves. Examples:
 *   trace_yaagc 29991 v35e.trace V:100 3:100 5:100 E:100
 *
 * Each trace line is: <ms_since_start> <DIR> <chan_octal> <val_octal>
 *   DIR = OUT (yaAGC sent us) or IN (we sent yaAGC, on channel 015 etc.)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h>

static long
now_ms(struct timespec *base)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (t.tv_sec - base->tv_sec) * 1000 + (t.tv_nsec - base->tv_nsec) / 1000000;
}

static void
send_pkt(int sock, int ch, int val)
{
  unsigned char p[4];
  p[0] = ch >> 3;
  p[1] = 0x40 | ((ch << 3) & 0x38) | ((val >> 12) & 7);
  p[2] = 0x80 | ((val >> 6) & 0x3F);
  p[3] = 0xc0 | (val & 0x3F);
  send(sock, p, 4, 0);
}

/* Map character to AGC keypad code, or -1. */
static int
key_to_code(const char *k)
{
  if (!strcmp(k, "V") || !strcmp(k, "v")) return 021;
  if (!strcmp(k, "N") || !strcmp(k, "n")) return 037;
  if (!strcmp(k, "E") || !strcmp(k, "e")) return 034;
  if (!strcmp(k, "R") || !strcmp(k, "r")) return 022;  /* RESET / ERROR */
  if (!strcmp(k, "C") || !strcmp(k, "c")) return 036;  /* CLR */
  if (!strcmp(k, "+")) return 032;
  if (!strcmp(k, "-")) return 033;
  if (!strcmp(k, "KR") || !strcmp(k, "kr")) return 031;
  if (!strcmp(k, "0")) return 020;
  if (k[0] >= '1' && k[0] <= '9' && k[1] == 0) return k[0] - '0';
  return -1;
}

int
main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: %s <port> <out.trace> [key:hold_ms]...\n", argv[0]);
    fprintf(stderr, "  e.g. %s 29991 v35e.trace V:100 3:100 5:100 E:100\n", argv[0]);
    return 2;
  }
  int port = atoi(argv[1]);
  FILE *out = fopen(argv[2], "w");
  if (!out) { perror(argv[2]); return 1; }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(sock, (struct sockaddr *)&addr, sizeof addr)) { perror("connect"); return 1; }

  /* Non-blocking so we can interleave keypresses + capture. */
  fcntl(sock, F_SETFL, O_NONBLOCK);

  struct timespec base;
  clock_gettime(CLOCK_MONOTONIC, &base);
  fprintf(out, "# yaAGC socket trace, t=ms_since_connect\n");
  fprintf(out, "# DIR=OUT means yaAGC->us  (AGC software channel write)\n");
  fprintf(out, "# DIR=IN  means us->yaAGC  (key/event injection)\n");

  /* Phase 0: drain 3s of output to capture the boot sequence. */
  unsigned char rbuf[4096];
  int total_idx = 0;
  long phase_end = 3000;
  long t;
  while ((t = now_ms(&base)) < phase_end) {
    int n = recv(sock, rbuf + (total_idx % 4096), 4096 - (total_idx % 4096), 0);
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        if ((rbuf[(total_idx + i) % 4096] & 0xC0) == 0x00) {
          /* Possible packet start. Need 4 bytes total. */
          if ((total_idx + i + 3) < total_idx + n) {
            unsigned char *p = &rbuf[(total_idx + i) % 4096];
            if ((p[1] & 0xC0) == 0x40 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0xC0) {
              int ch = (p[0] << 3) | ((p[1] >> 3) & 7);
              int val = ((p[1] & 7) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
              fprintf(out, "%6ld OUT %03o %06o\n", t, ch, val);
              i += 3;
            }
          }
        }
      }
      total_idx += n;
    } else {
      usleep(2000);
    }
  }
  fflush(out);

  /* Phase 1: send each key, draining output between. */
  for (int i = 3; i < argc; i++) {
    char k[32]; int hold = 100;
    char *colon = strchr(argv[i], ':');
    if (colon) { *colon = 0; hold = atoi(colon + 1); }
    snprintf(k, sizeof k, "%s", argv[i]);
    int code = key_to_code(k);
    if (code < 0) { fprintf(stderr, "skip unknown key %s\n", k); continue; }

    /* press */
    t = now_ms(&base);
    fprintf(out, "%6ld IN  015 %06o   # press %s\n", t, code, k);
    send_pkt(sock, 015, code);

    /* drain during hold */
    long until = t + hold;
    while ((t = now_ms(&base)) < until) {
      int n = recv(sock, rbuf, sizeof rbuf, 0);
      if (n > 0) {
        for (int j = 0; j + 3 < n; ) {
          unsigned char *p = &rbuf[j];
          if ((p[0] & 0xC0) == 0x00 && (p[1] & 0xC0) == 0x40 &&
              (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0xC0) {
            int ch = (p[0] << 3) | ((p[1] >> 3) & 7);
            int val = ((p[1] & 7) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            fprintf(out, "%6ld OUT %03o %06o\n", t, ch, val);
            j += 4;
          } else {
            j++;
          }
        }
      } else {
        usleep(2000);
      }
    }

    /* release */
    t = now_ms(&base);
    fprintf(out, "%6ld IN  015 000000   # release %s\n", t, k);
    send_pkt(sock, 015, 0);
  }

  /* Phase 2: drain another 6s to catch post-keypress display refresh. */
  long final_end = now_ms(&base) + 6000;
  while ((t = now_ms(&base)) < final_end) {
    int n = recv(sock, rbuf, sizeof rbuf, 0);
    if (n > 0) {
      for (int j = 0; j + 3 < n; ) {
        unsigned char *p = &rbuf[j];
        if ((p[0] & 0xC0) == 0x00 && (p[1] & 0xC0) == 0x40 &&
            (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0xC0) {
          int ch = (p[0] << 3) | ((p[1] >> 3) & 7);
          int val = ((p[1] & 7) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
          fprintf(out, "%6ld OUT %03o %06o\n", t, ch, val);
          j += 4;
        } else {
          j++;
        }
      }
    } else {
      usleep(2000);
    }
  }

  fclose(out);
  close(sock);
  fprintf(stderr, "trace written to %s\n", argv[2]);
  return 0;
}
