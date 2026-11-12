/* Does a zero-length UDP datagram reach a socket bound to the wildcard address? */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static int one(int fam, const char *dst, int len) {
  int r = socket(fam, SOCK_DGRAM, 0), s = socket(fam, SOCK_DGRAM, 0);
  struct sockaddr_storage a; socklen_t al = sizeof(a);
  memset(&a, 0, sizeof(a)); a.ss_family = fam;
  bind(r, (void *)&a, fam == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
  getsockname(r, (void *)&a, &al);
  if (fam == AF_INET) inet_pton(AF_INET, dst, &((struct sockaddr_in *)&a)->sin_addr);
  else inet_pton(AF_INET6, dst, &((struct sockaddr_in6 *)&a)->sin6_addr);
  char buf[16] = "x";
  ssize_t n = sendto(s, buf, len, 0, (void *)&a, al);
  int se = errno;
  struct pollfd p = { r, POLLIN, 0 };
  int pr = poll(&p, 1, 1000);
  struct sockaddr_storage from; socklen_t fl = sizeof(from); char fs[64] = "-";
  ssize_t m = pr > 0 ? recvfrom(r, buf, sizeof(buf), MSG_DONTWAIT, (void *)&from, &fl) : -1;
  if (m >= 0) inet_ntop(from.ss_family, from.ss_family == AF_INET ? (void *)&((struct sockaddr_in *)&from)->sin_addr
                        : (void *)&((struct sockaddr_in6 *)&from)->sin6_addr, fs, sizeof(fs));
  printf("%-5s to %-10s len %d: sendto=%zd%s%s, %s, from %s\n", fam == AF_INET ? "IPv4" : "IPv6", dst, len, n,
         n < 0 ? " " : "", n < 0 ? strerror(se) : "", pr > 0 ? (m >= 0 ? "received" : "poll ok, recv failed") : "NOT RECEIVED in 1 s", fs);
  close(r); close(s);
  return pr > 0 && m == len;
}
int main(void) {
  int ok = 0;
  ok += one(AF_INET, "0.0.0.0", 0);   ok += one(AF_INET, "127.0.0.1", 0);
  ok += one(AF_INET6, "::", 0);       ok += one(AF_INET6, "::1", 0);
  ok += one(AF_INET6, "::", 1);       ok += one(AF_INET6, "::1", 1);
  ok += one(AF_INET6, "::ffff:127.0.0.1", 0);
  printf("%d of 7 received\n", ok);
  return ok != 7;
}
