#include "lurk.h"
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL 0
#endif

void setup_signal(void) {
  signal(SIGPIPE, SIG_IGN);
}

// read exactly count bytes or return -1
ssize_t read_exact(int fd, void *buf, size_t count) {
  size_t total = 0;
  while (total < count) {
    ssize_t n = read(fd, (char*)buf + total, (ssize_t)(count - total));
    if (n == 0) { errno = 0; return -1; }
    if (n < 0) return -1;
    total += (size_t)n;
  }
  return (ssize_t)total;
}

// safe send loop using MSG_NOSIGNAL
ssize_t safe_send(int fd, const void *buf, size_t count) {
  const unsigned char *p = (const unsigned char*)buf;
  size_t total = 0;
  while (total < count) {
    ssize_t n = send(fd, p + total, (size_t)(count - total), MSG_NOSIGNAL);
    if (n <= 0) return -1;
    total += (size_t)n;
  }
  return (ssize_t)total;
}