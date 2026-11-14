/* Checks that a qdisc caps an out-of-range skb->queue_mapping set by tc BPF (backport of f19f2b308beb).
 *
 * As root: creates dummy device qmt0 with 4 tx queues and a prio root qdisc, attaches a cls_bpf
 * direct-action program that stores queue_mapping = 7, sends 64 UDP datagrams through it, and
 * checks the kernel log for "qmt0 selects TX queue 7", which dequeue_skb() logs when it caps the
 * mapping. Without the fix the qdisc indexes dev->_tx[7] of a 4-entry array: do not run it on a
 * kernel without the fix. Removes qmt0 at the end. Exit 0 on PASS.
 * Build: build-test.sh qmaptest.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/pkt_cls.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define DEV "qmt0"
#define QUEUE 7

static int sh(const char *cmd) {
  int r = system(cmd);
  if (r) fprintf(stderr, "command failed (%d): %s\n", r, cmd);
  return r;
}

static int load_prog(void) {
  struct bpf_insn insns[] = {
    { .code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = 2, .imm = QUEUE },          /* r2 = 7 */
    { .code = BPF_STX | BPF_MEM | BPF_W, .dst_reg = 1, .src_reg = 2,
      .off = offsetof(struct __sk_buff, queue_mapping) },                         /* skb->queue_mapping = r2 */
    { .code = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = 0, .imm = TC_ACT_OK },      /* r0 = TC_ACT_OK */
    { .code = BPF_JMP | BPF_EXIT },
  };
  static char log[4096];
  union bpf_attr a;
  memset(&a, 0, sizeof(a));
  a.prog_type = BPF_PROG_TYPE_SCHED_CLS;
  a.insns = (unsigned long)insns;
  a.insn_cnt = sizeof(insns) / sizeof(insns[0]);
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

static int attach(int ifindex, int fd) {
  struct { struct nlmsghdr n; struct tcmsg t; char buf[512]; } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
  req.n.nlmsg_type = RTM_NEWTFILTER;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
  req.t.tcm_family = AF_UNSPEC;
  req.t.tcm_ifindex = ifindex;
  req.t.tcm_parent = TC_H_MAKE(1U << 16, 0);                       /* prio root 1: */
  req.t.tcm_info = TC_H_MAKE(1U << 16, htons(ETH_P_ALL));           /* pref 1, all protocols */
  put(&req.n, TCA_KIND, "bpf", 4);
  struct rtattr *opt = (struct rtattr *)((char *)&req.n + NLMSG_ALIGN(req.n.nlmsg_len));
  put(&req.n, TCA_OPTIONS, NULL, 0);
  unsigned int u = fd;
  put(&req.n, TCA_BPF_FD, &u, 4);
  put(&req.n, TCA_BPF_NAME, "qmaptest", 9);
  u = TCA_BPF_FLAG_ACT_DIRECT;
  put(&req.n, TCA_BPF_FLAGS, &u, 4);
  opt->rta_len = (char *)&req.n + req.n.nlmsg_len - (char *)opt;

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
  if (err) fprintf(stderr, "RTM_NEWTFILTER: %s\n", strerror(-err));
  return err;
}

int main(void) {
  int ok = 0;
  system("ip link del " DEV " 2>/dev/null");
  if (sh("ip link add " DEV " numtxqueues 4 type dummy") || sh("ip addr add 10.213.0.1/24 dev " DEV) ||
      sh("ip link set " DEV " up") || sh("tc qdisc add dev " DEV " root handle 1: prio"))
    goto out;
  int fd = load_prog();
  if (fd < 0) goto out;
  printf("PASS  SCHED_CLS program storing queue_mapping = %d loads\n", QUEUE);
  if (attach(if_nametoindex(DEV), fd)) goto out;
  printf("PASS  attached to the prio root of %s (4 tx queues)\n", DEV);

  sh("dmesg -c > /dev/null");
  int u = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in dst = { .sin_family = AF_INET, .sin_port = htons(9) };
  inet_pton(AF_INET, "10.213.0.2", &dst.sin_addr);
  int sent = 0;
  for (int i = 0; i < 64; i++) sent += sendto(u, "x", 1, 0, (void *)&dst, sizeof(dst)) == 1;
  close(u);
  usleep(200000);
  printf("%s  %d of 64 datagrams sent\n", sent == 64 ? "PASS" : "FAIL", sent);

  FILE *k = popen("dmesg | grep -c '" DEV " selects TX queue 7, but real number of TX queues is 4'", "r");
  int hits = 0;
  if (k) { if (fscanf(k, "%d", &hits) != 1) hits = 0; pclose(k); }
  printf("%s  kernel capped the mapping (%d \"selects TX queue 7\" notices; ratelimited)\n",
         hits > 0 ? "PASS" : "FAIL", hits);
  FILE *st = popen("tc -s qdisc show dev " DEV " | grep -m1 -o 'Sent [0-9]* bytes [0-9]* pkt'", "r");
  char line[128] = "";
  if (st) { if (!fgets(line, sizeof(line), st)) line[0] = 0; pclose(st); }
  printf("      prio root: %s", line[0] ? line : "no stats\n");
  ok = sent == 64 && hits > 0;
out:
  system("ip link del " DEV " 2>/dev/null");
  printf("%s\n", ok ? "PASS" : "FAIL");
  return !ok;
}
