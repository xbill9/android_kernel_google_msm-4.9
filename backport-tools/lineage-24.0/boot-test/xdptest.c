/* Checks that generic (skb mode) XDP re-parses the Ethernet header after a program rewrites it
 * (backports of 297249569932 and 22b6034323fd;
 * f8414a8d886b, which pulls the header a second time, was reverted upstream by 7f9bf6e82461).
 *
 * As root: creates veth pair xdt0/xdt1, xdt1 with MAC 02:00:00:00:00:02 and 10.214.0.2/24, and a
 * UDP socket on 10.214.0.2:5555. Frames are written to xdt0 through an AF_PACKET socket, so they
 * arrive on xdt1:
 *   A: destination MAC 02:00:00:00:00:99 (another host), EtherType IPv4
 *   B: destination MAC of xdt1, EtherType 0x88b5 (local experimental)
 * Control, with a generic XDP program that only returns XDP_PASS: neither reaches the socket
 * (A is for another host, B is not IPv4). Then with a program that sets the destination MAC to
 * xdt1's and the EtherType to IPv4 before XDP_PASS: both must arrive. A needs the pkt_type update
 * (22b6034323fd); B needs skb->protocol recomputed (297249569932). Removes xdt0 at the end. Exit 0 on PASS.
 * Build: build-test.sh xdptest.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PORT 5555

static int sh(const char *cmd) {
  int r = system(cmd);
  if (r) fprintf(stderr, "command failed (%d): %s\n", r, cmd);
  return r;
}

static int load(struct bpf_insn *insns, int n) {
  static char log[4096];
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.prog_type = BPF_PROG_TYPE_XDP;
  a.insns = (unsigned long)insns;
  a.insn_cnt = n;
  a.license = (unsigned long)"GPL";
  a.log_buf = (unsigned long)log; a.log_size = sizeof(log); a.log_level = 1;
  int fd = syscall(__NR_bpf, BPF_PROG_LOAD, &a, sizeof(a));
  if (fd < 0) fprintf(stderr, "BPF_PROG_LOAD: %s\n%s\n", strerror(errno), log);
  return fd;
}

static void put(struct nlmsghdr *n, unsigned short type, const void *data, int len) {
  struct rtattr *rta = (struct rtattr *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));
  rta->rta_type = type; rta->rta_len = RTA_LENGTH(len);
  if (len) memcpy(RTA_DATA(rta), data, len);
  n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

static int attach(int ifindex, int fd) {  /* generic XDP; fd -1 detaches */
  struct { struct nlmsghdr n; struct ifinfomsg i; char buf[256]; } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_SETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = ifindex;
  struct rtattr *x = (struct rtattr *)((char *)&req.n + NLMSG_ALIGN(req.n.nlmsg_len));
  put(&req.n, IFLA_XDP, NULL, 0);
  put(&req.n, IFLA_XDP_FD, &fd, 4);
  unsigned int fl = XDP_FLAGS_SKB_MODE;
  put(&req.n, IFLA_XDP_FLAGS, &fl, 4);
  x->rta_len = (char *)&req.n + req.n.nlmsg_len - (char *)x;

  int s = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (s < 0 || send(s, &req, req.n.nlmsg_len, 0) < 0) { perror("netlink"); return -1; }
  char ack[1024];
  int len = recv(s, ack, sizeof(ack), 0);
  close(s);
  struct nlmsghdr *h = (struct nlmsghdr *)ack;
  if (len < (int)NLMSG_LENGTH(sizeof(struct nlmsgerr)) || h->nlmsg_type != NLMSG_ERROR) {
    fprintf(stderr, "netlink: no ack\n"); return -1;
  }
  int err = ((struct nlmsgerr *)NLMSG_DATA(h))->error;
  if (err) fprintf(stderr, "RTM_SETLINK IFLA_XDP: %s\n", strerror(-err));
  return err;
}

static unsigned short csum(const unsigned char *p, int n) {
  unsigned long s = 0;
  for (int i = 0; i < n; i += 2) s += (p[i] << 8) | p[i + 1];
  while (s >> 16) s = (s & 0xffff) + (s >> 16);
  return htons(~s & 0xffff);
}

/* Ethernet + IPv4 + UDP 10.214.0.3 -> 10.214.0.2:PORT, payload one byte */
static int frame(unsigned char *f, const unsigned char *dst, unsigned short type, char tag) {
  static const unsigned char src[6] = { 0x02, 0, 0, 0, 0, 0x01 };
  memset(f, 0, 64);
  memcpy(f, dst, 6); memcpy(f + 6, src, 6);
  f[12] = type >> 8; f[13] = type & 0xff;
  unsigned char *ip = f + 14;
  ip[0] = 0x45; ip[3] = 29; ip[8] = 64; ip[9] = 17;            /* total length 29, TTL 64, UDP */
  ip[12] = 10; ip[13] = 214; ip[14] = 0; ip[15] = 3;
  ip[16] = 10; ip[17] = 214; ip[18] = 0; ip[19] = 2;
  memcpy(ip + 10, &(unsigned short){ csum(ip, 20) }, 2);
  unsigned char *udp = ip + 20;
  udp[0] = 0x30; udp[1] = 0x39; udp[2] = PORT >> 8; udp[3] = PORT & 0xff; udp[5] = 9;  /* csum 0 */
  udp[8] = tag;
  return 64;
}

/* sends A and B on xdt0, returns a bit mask of the tags received (1 = A, 2 = B) */
static int round_trip(int ps, int us, int ifindex) {
  static const unsigned char other[6] = { 0x02, 0, 0, 0, 0, 0x99 };
  static const unsigned char host[6] = { 0x02, 0, 0, 0, 0, 0x02 };
  struct sockaddr_ll ll = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_IP),
                            .sll_ifindex = ifindex, .sll_halen = ETH_ALEN };
  unsigned char f[64];
  char buf[16];
  while (recv(us, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
  if (sendto(ps, f, frame(f, other, ETH_P_IP, 'A'), 0, (void *)&ll, sizeof(ll)) != 64 ||
      sendto(ps, f, frame(f, host, 0x88b5, 'B'), 0, (void *)&ll, sizeof(ll)) != 64 ||
      sendto(ps, f, frame(f, host, ETH_P_IP, 'C'), 0, (void *)&ll, sizeof(ll)) != 64)
    perror("AF_PACKET sendto");
  int got = 0;
  struct pollfd p = { .fd = us, .events = POLLIN };
  while (poll(&p, 1, 500) > 0 && recv(us, buf, sizeof(buf), 0) == 1)
    got |= buf[0] == 'A' ? 1 : buf[0] == 'B' ? 2 : buf[0] == 'C' ? 4 : 0;
  return got;
}

int main(void) {
  int ok = 0, rpf = -1;
  struct bpf_insn pass[] = {
    { .code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = 0, .imm = XDP_PASS },
    { .code = BPF_JMP | BPF_EXIT },
  };
  struct bpf_insn drop[] = {
    { .code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = 0, .imm = XDP_DROP },
    { .code = BPF_JMP | BPF_EXIT },
  };
  struct bpf_insn rewrite[] = {   /* little-endian immediates */
    { .code = BPF_LDX | BPF_MEM | BPF_W, .dst_reg = 2, .src_reg = 1, .off = 0 },   /* r2 = data */
    { .code = BPF_LDX | BPF_MEM | BPF_W, .dst_reg = 3, .src_reg = 1, .off = 4 },   /* r3 = data_end */
    { .code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = 4, .src_reg = 2 },
    { .code = BPF_ALU64 | BPF_ADD | BPF_K, .dst_reg = 4, .imm = 14 },
    { .code = BPF_JMP | BPF_JGT | BPF_X, .dst_reg = 4, .src_reg = 3, .off = 3 },   /* short: pass */
    { .code = BPF_ST | BPF_MEM | BPF_W, .dst_reg = 2, .off = 0, .imm = 0x00000002 },  /* 02 00 00 00 */
    { .code = BPF_ST | BPF_MEM | BPF_H, .dst_reg = 2, .off = 4, .imm = 0x0200 },      /* 00 02 */
    { .code = BPF_ST | BPF_MEM | BPF_H, .dst_reg = 2, .off = 12, .imm = 0x0008 },     /* 08 00: IPv4 */
    { .code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = 0, .imm = XDP_PASS },
    { .code = BPF_JMP | BPF_EXIT },
  };
  int ps = -1, us = -1;

  system("ip link del xdt0 2>/dev/null");
  FILE *r = fopen("/proc/sys/net/ipv4/conf/all/rp_filter", "r");
  if (r) { if (fscanf(r, "%d", &rpf) != 1) rpf = -1; fclose(r); }
  if (sh("ip link add xdt0 type veth peer name xdt1") ||
      sh("ip link set xdt0 address 02:00:00:00:00:01") || sh("ip link set xdt1 address 02:00:00:00:00:02") ||
      sh("ip addr add 10.214.0.2/24 dev xdt1") ||
      sh("echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter") ||
      sh("echo 0 > /proc/sys/net/ipv4/conf/xdt1/rp_filter") ||
      sh("ip link set xdt1 up") || sh("ip link set xdt0 up"))
    goto out;
  usleep(300000);
  int in = if_nametoindex("xdt1"), outi = if_nametoindex("xdt0");

  /* protocol 0: a packet socket with a protocol is also a tap, and a tapped skb is cloned,
   * which generic XDP skips */
  ps = socket(AF_PACKET, SOCK_RAW, 0);
  us = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(PORT) };
  inet_pton(AF_INET, "10.214.0.2", &sa.sin_addr);
  if (ps < 0 || us < 0 || bind(us, (void *)&sa, sizeof(sa))) { perror("socket"); goto out; }

  int pfd = load(pass, 2), rfd = load(rewrite, sizeof(rewrite) / sizeof(rewrite[0]));
  int dfd = load(drop, 2);
  if (pfd < 0 || rfd < 0 || dfd < 0) goto out;
  printf("PASS  XDP programs load\n");

  if (attach(in, dfd)) goto out;
  int gd = round_trip(ps, us, outi);
  printf("%s  control: XDP_DROP program drops C (generic XDP runs on xdt1): %s\n", gd == 0 ? "PASS" : "FAIL",
         gd & 4 ? "received" : "dropped");

  if (attach(in, pfd)) goto out;
  int g0 = round_trip(ps, us, outi);
  printf("%s  control (XDP_PASS only): A %s, B %s (expected: neither)\n", (g0 & 3) == 0 ? "PASS" : "FAIL",
         g0 & 1 ? "received" : "dropped", g0 & 2 ? "received" : "dropped");
  printf("%s  control: C (xdt1's MAC, IPv4, not rewritten) %s\n", g0 & 4 ? "PASS" : "FAIL",
         g0 & 4 ? "received" : "dropped");

  if (attach(in, rfd)) goto out;
  int g1 = round_trip(ps, us, outi);
  printf("%s  C still received under the rewriting program\n", g1 & 4 ? "PASS" : "FAIL");
  printf("%s  A, destination MAC rewritten to xdt1's: %s (pkt_type update, 22b6034323fd)\n",
         g1 & 1 ? "PASS" : "FAIL", g1 & 1 ? "received" : "dropped");
  printf("%s  B, EtherType rewritten to IPv4: %s (protocol recomputed, 297249569932)\n",
         g1 & 2 ? "PASS" : "FAIL", g1 & 2 ? "received" : "dropped");
  attach(in, -1);
  ok = gd == 0 && g0 == 4 && g1 == 7;
out:
  if (ps >= 0) close(ps);
  if (us >= 0) close(us);
  system("ip link del xdt0 2>/dev/null");
  if (rpf >= 0) {
    char c[96];
    snprintf(c, sizeof(c), "echo %d > /proc/sys/net/ipv4/conf/all/rp_filter", rpf);
    system(c);
  }
  printf("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
