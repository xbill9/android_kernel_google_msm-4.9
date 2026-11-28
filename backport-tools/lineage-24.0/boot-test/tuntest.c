// tuntest: checks that TUNSETLINK drops the multicast list when the address length changes
// (upstream 5677cf0e039b, "tun: flush uc and mc addresses in TUNSETLINK"). Without it, an entry
// added at the old length is dumped at the new one: /proc/net/dev_mcast then shows bytes past
// the end of the address that were never written. Prints PASS/FAIL per case and exits 1 on any
// failure. Run as root from the adb shell.
//
// A TAP device (Ethernet, 6-byte addresses) gets one more multicast address with SIOCADDMULTI, with
// the link down so IPv6 adds none of its own. Changing to another 6-byte type keeps it; changing
// to ARPHRD_IP6GRE (16 bytes) must empty the list.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char* name, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void check(int ok, const char* name, const char* fmt, ...) {
  va_list ap;
  printf("%s %s: ", ok ? "PASS" : "FAIL", name);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  if (!ok) fails++;
}

// The device's entries in /proc/net/dev_mcast: their count, and in addr the one that starts with
// this test's address (hex), as dumped. The kernel adds the IPv4 and IPv6 all-hosts groups to every
// new device, so there are more than one.
static int mcast_entries(const char* ifname, char* addr, size_t len) {
  FILE* f = fopen("/proc/net/dev_mcast", "r");
  char line[256], name[IFNAMSIZ + 1], hex[128];
  int idx, users, global, n = 0;
  addr[0] = 0;
  if (!f) return -1;
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "%d %16s %d %d %127s", &idx, name, &users, &global, hex) == 5 &&
        !strcmp(name, ifname)) {
      n++;
      if (!strncmp(hex, "01005e0000fb", 12)) snprintf(addr, len, "%s", hex);
    }
  fclose(f);
  return n;
}

int main(void) {
  static const unsigned char mc[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb};
  char addr[128];
  struct ifreq ifr;

  int fd = open("/dev/tun", O_RDWR);
  if (fd < 0) fd = open("/dev/net/tun", O_RDWR);
  if (fd < 0) { printf("FAIL open tun: %s\n1 failed\n", strerror(errno)); return 1; }
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  strcpy(ifr.ifr_name, "tuntest%d");
  if (ioctl(fd, TUNSETIFF, &ifr) < 0) { printf("FAIL TUNSETIFF: %s\n1 failed\n", strerror(errno)); return 1; }
  char ifname[IFNAMSIZ];
  snprintf(ifname, sizeof(ifname), "%s", ifr.ifr_name);

  int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  memset(&ifr, 0, sizeof(ifr));
  strcpy(ifr.ifr_name, ifname);
  ifr.ifr_hwaddr.sa_family = AF_UNSPEC;
  memcpy(ifr.ifr_hwaddr.sa_data, mc, sizeof(mc));
  int r = ioctl(s, SIOCADDMULTI, &ifr);
  check(r == 0, "add multicast 01:00:5e:00:00:fb", "%s", r ? strerror(errno) : "ok");

  int n = mcast_entries(ifname, addr, sizeof(addr));
  check(!strcmp(addr, "01005e0000fb"), "listed at 6 bytes", "%d entries, ours %s", n, addr);

  // Another type with 6-byte addresses: the entry is still valid and stays.
  r = ioctl(fd, TUNSETLINK, (unsigned long)ARPHRD_ETHER);
  n = mcast_entries(ifname, addr, sizeof(addr));
  check(r == 0 && !strcmp(addr, "01005e0000fb"), "same length keeps it",
        "TUNSETLINK %d, %d entries, ours %s", r, n, addr);

  // 16-byte addresses: every entry was added at 6 bytes, so all must go.
  r = ioctl(fd, TUNSETLINK, (unsigned long)ARPHRD_IP6GRE);
  n = mcast_entries(ifname, addr, sizeof(addr));
  check(r == 0 && n == 0, "longer address flushes the list", "TUNSETLINK %d, %d entries%s%s", r, n,
        addr[0] ? ", ours dumped as " : "", addr);

  // And back: nothing reappears.
  r = ioctl(fd, TUNSETLINK, (unsigned long)ARPHRD_ETHER);
  n = mcast_entries(ifname, addr, sizeof(addr));
  check(r == 0 && n == 0, "back to 6 bytes, still empty", "TUNSETLINK %d, %d entries", r, n);

  close(s);
  close(fd);
  printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
