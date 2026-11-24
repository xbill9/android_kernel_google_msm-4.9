// fsveritytest: checks the kernel's fs-verity (FS_IOC_ENABLE_VERITY, FS_IOC_MEASURE_VERITY) on
// /data from the adb shell, as root. For files of several sizes (empty, one block, block
// boundaries, one/two/three-level Merkle trees, with and without a salt) it writes random data,
// enables verity, and checks:
//   - the measured digest equals the fs-verity digest computed here (SHA-256 Merkle tree over
//     4096-byte blocks, then SHA-256 of the fsverity_descriptor; Documentation/filesystems/
//     fsverity.rst), so the kernel built the tree the format says;
//   - FS_IOC_GETFLAGS reports FS_VERITY_FL, and statx() gives the size (and STATX_ATTR_VERITY
//     when it reports attributes; this kernel's statx reports none, so Android's VerityUtils
//     falls back to FS_IOC_GETFLAGS);
//   - after dropping the page cache the file reads back byte for byte (every block is verified
//     against the tree on the way in);
//   - the file can no longer be opened for writing or truncated (EPERM), and enabling
//     verity again gives EEXIST.
// Also: enabling with a writable fd still open gives ETXTBSY, a 1024-byte block size gives
// EINVAL. Directories: /data/local/tmp and, if present, /data/misc_de/0 (encrypted per-user).
// PASS/FAIL per check, exit 1 on any failure. "fsveritytest list <file>..." lists files with verity;
// "fsveritytest strip <dir>..." makes them plain again (see strip() below).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/fsverity.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <sys/xattr.h>
#include <time.h>

#ifndef STATX_ATTR_VERITY
#define STATX_ATTR_VERITY 0x00100000
#endif
#ifndef FS_VERITY_FL
#define FS_VERITY_FL 0x00100000
#endif

static int failures, passes;
static int n_stripped, n_strip_failed;

static void check(int ok, const char *what, const char *name) {
  printf("%s %s: %s\n", ok ? "PASS" : "FAIL", name, what);
  if (ok) passes++; else failures++;
}

// ---- SHA-256 (FIPS 180-4) ----
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } sha256_ctx;
static const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(sha256_ctx *c, const uint8_t *p) {
  uint32_t w[64], a, b, d, e, f, g, h, cc, t1, t2;
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
    uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
  for (int i = 0; i < 64; i++) {
    t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
    h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void sha256_init(sha256_ctx *c) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(c->h, iv, sizeof(iv)); c->len = 0; c->n = 0;
}
static void sha256_update(sha256_ctx *c, const void *data, size_t len) {
  const uint8_t *p = data;
  c->len += len;
  while (len) {
    size_t k = 64 - c->n < len ? 64 - c->n : len;
    memcpy(c->buf + c->n, p, k); c->n += k; p += k; len -= k;
    if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; }
  }
}
static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
  uint64_t bits = c->len * 8;
  uint8_t pad = 0x80, z = 0, lenb[8];
  sha256_update(c, &pad, 1);
  while (c->n != 56) sha256_update(c, &z, 1);
  for (int i = 0; i < 8; i++) lenb[i] = bits >> (56 - 8 * i);
  sha256_update(c, lenb, 8);
  for (int i = 0; i < 8; i++) { out[4*i] = c->h[i] >> 24; out[4*i+1] = c->h[i] >> 16; out[4*i+2] = c->h[i] >> 8; out[4*i+3] = c->h[i]; }
}

// ---- fs-verity digest, computed in userspace ----
#define BS 4096
// hash of one block: SHA-256(salt zero-padded to 64 bytes || block)
static void hash_block(const uint8_t *salt, int salt_size, const uint8_t *blk, uint8_t out[32]) {
  sha256_ctx c; sha256_init(&c);
  if (salt_size) { uint8_t ps[64] = {0}; memcpy(ps, salt, salt_size); sha256_update(&c, ps, 64); }
  sha256_update(&c, blk, BS); sha256_final(&c, out);
}
static void expected_digest(const uint8_t *data, size_t size, const uint8_t *salt, int salt_size,
                            uint8_t digest[32]) {
  uint8_t root[64] = {0};
  size_t nblocks = (size + BS - 1) / BS;
  if (nblocks) {
    // level 0 input: the data, zero-padded to whole blocks
    size_t cur_len = nblocks * BS;
    uint8_t *cur = calloc(1, cur_len);
    memcpy(cur, data, size);
    while (nblocks > 1) {
      size_t nh = nblocks, next_blocks = (nh * 32 + BS - 1) / BS;
      uint8_t *next = calloc(1, next_blocks * BS);
      for (size_t i = 0; i < nh; i++) hash_block(salt, salt_size, cur + i * BS, next + i * 32);
      free(cur); cur = next; nblocks = next_blocks;
    }
    hash_block(salt, salt_size, cur, root);
    free(cur);
  }
  struct fsverity_descriptor_v1 {
    uint8_t version, hash_algorithm, log_blocksize, salt_size;
    uint32_t sig_size;
    uint64_t data_size;
    uint8_t root_hash[64], salt[32], reserved[144];
  } __attribute__((packed)) d;
  memset(&d, 0, sizeof(d));
  d.version = 1; d.hash_algorithm = FS_VERITY_HASH_ALG_SHA256; d.log_blocksize = 12;
  d.salt_size = salt_size; d.data_size = size;  // little-endian on arm64
  memcpy(d.root_hash, root, 32); memcpy(d.salt, salt, salt_size);
  sha256_ctx c; sha256_init(&c); sha256_update(&c, &d, sizeof(d)); sha256_final(&c, digest);
}

static int enable(int fd, unsigned block_size, const uint8_t *salt, int salt_size) {
  struct fsverity_enable_arg a;
  memset(&a, 0, sizeof(a));
  a.version = 1; a.hash_algorithm = FS_VERITY_HASH_ALG_SHA256; a.block_size = block_size;
  a.salt_size = salt_size; a.salt_ptr = (uintptr_t)salt;
  return ioctl(fd, FS_IOC_ENABLE_VERITY, &a) ? errno : 0;
}

static void drop_caches(void) {
  int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
  if (fd >= 0) { (void)!write(fd, "3", 1); close(fd); }
}

static void one_file(const char *dir, size_t size, int salted) {
  char path[256], name[96];
  uint8_t salt[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  int salt_size = salted ? 8 : 0;
  snprintf(path, sizeof(path), "%s/fsveritytest.%zu%s", dir, size, salted ? ".salt" : "");
  snprintf(name, sizeof(name), "%s %zu bytes%s", dir, size, salted ? " salted" : "");
  unlink(path);

  uint8_t *data = malloc(size ? size : 1);
  for (size_t i = 0; i < size; i++) data[i] = random();
  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0) { check(0, "create", name); free(data); return; }
  size_t off = 0;
  while (off < size) { ssize_t n = write(fd, data + off, size - off); if (n <= 0) break; off += n; }
  fsync(fd); close(fd);

  fd = open(path, O_RDONLY);
  int err = enable(fd, BS, salt, salt_size);
  check(err == 0, err ? strerror(err) : "enable", name);
  if (err) { close(fd); unlink(path); free(data); return; }

  uint64_t mbuf[(sizeof(struct fsverity_digest) + 64) / 8 + 1];
  struct fsverity_digest *md = (struct fsverity_digest *)mbuf;
  md->digest_size = 64;
  uint8_t want[32];
  expected_digest(data, size, salt, salt_size, want);
  int ok = ioctl(fd, FS_IOC_MEASURE_VERITY, md) == 0 && md->digest_algorithm == FS_VERITY_HASH_ALG_SHA256 &&
           md->digest_size == 32 && !memcmp(md->digest, want, 32);
  check(ok, "measured digest = computed digest", name);

  struct statx st;
  // This kernel's statx() reports no attributes (stx_attributes_mask 0); Android then asks
  // FS_IOC_GETFLAGS. If the mask does carry STATX_ATTR_VERITY, the bit must be set.
  ok = statx(AT_FDCWD, path, 0, STATX_ALL, &st) == 0 && st.stx_size == size &&
       (!(st.stx_attributes_mask & STATX_ATTR_VERITY) || (st.stx_attributes & STATX_ATTR_VERITY));
  check(ok, "statx size (and STATX_ATTR_VERITY if reported)", name);
  unsigned int fl = 0;
  check(ioctl(fd, FS_IOC_GETFLAGS, &fl) == 0 && (fl & FS_VERITY_FL), "FS_IOC_GETFLAGS FS_VERITY_FL", name);
  check(enable(fd, BS, salt, salt_size) == EEXIST, "enable again -> EEXIST", name);
  close(fd);

  drop_caches();
  fd = open(path, O_RDONLY);
  uint8_t *back = malloc(size + 1);
  off = 0;
  for (;;) { ssize_t n = read(fd, back + off, size + 1 - off); if (n <= 0) break; off += n; }
  check(off == size && !memcmp(back, data, size), "reads back the same after dropping the cache", name);
  close(fd);
  free(back);

  errno = 0;
  fd = open(path, O_WRONLY);
  check(fd < 0 && errno == EPERM, "open for writing -> EPERM", name);
  if (fd >= 0) close(fd);
  check(truncate(path, 0) != 0 && errno == EPERM, "truncate -> EPERM", name);
  unlink(path);
  free(data);
}

static void negative(const char *dir) {
  char path[256], name[96];
  snprintf(path, sizeof(path), "%s/fsveritytest.neg", dir);
  snprintf(name, sizeof(name), "%s", dir);
  unlink(path);
  int wfd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  (void)!write(wfd, "hello", 5);
  int rfd = open(path, O_RDONLY);
  check(enable(rfd, BS, NULL, 0) == ETXTBSY, "enable with a writer open -> ETXTBSY", name);
  close(wfd);
  check(enable(rfd, 1024, NULL, 0) == EINVAL, "1024-byte blocks -> EINVAL", name);
  close(rfd);
  unlink(path);
}

// "fsveritytest list <file>...": print the files that have fs-verity (FS_VERITY_FL) and the count.
static int list(int argc, char **argv) {
  int n = 0;
  for (int i = 2; i < argc; i++) {
    int fd = open(argv[i], O_RDONLY);
    unsigned int fl = 0;
    if (fd >= 0 && ioctl(fd, FS_IOC_GETFLAGS, &fl) == 0 && (fl & FS_VERITY_FL)) { printf("%s\n", argv[i]); n++; }
    if (fd >= 0) close(fd);
  }
  printf("%d of %d files have fs-verity\n", n, argc - 2);
  return 0;
}

// "fsveritytest strip <dir>...": replace every fs-verity file under the dirs with a plain copy
// (same owner, mode, SELinux label and times; written next to it, then renamed over it). A
// kernel with the 2018 fs-verity prototype (FS_IOC_ENABLE_VERITY as _IO('f', 133)) refuses to
// open files that this kernel's fs-verity made (EINVAL), and Android then deletes
// packages.xml, so sargo-test runs this before rebooting off a test kernel. Stop Android first.
static int strip_one(const char *path, const struct stat *sb, int type, struct FTW *ftw) {
  (void)ftw;
  if (type != FTW_F || !S_ISREG(sb->st_mode)) return 0;
  int fd = open(path, O_RDONLY | O_NOFOLLOW), out = -1, ok = 0;
  unsigned int fl = 0;
  if (fd < 0) return 0;
  if (ioctl(fd, FS_IOC_GETFLAGS, &fl) || !(fl & FS_VERITY_FL)) { close(fd); return 0; }
  char tmp[4200], label[256];
  snprintf(tmp, sizeof(tmp), "%s.fsvstrip", path);
  ssize_t ln = getxattr(path, "security.selinux", label, sizeof(label));
  out = open(tmp, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, sb->st_mode & 07777);
  if (out >= 0) {
    char buf[65536]; ssize_t n; ok = 1;
    while ((n = read(fd, buf, sizeof(buf))) > 0) if (write(out, buf, n) != n) { ok = 0; break; }
    if (n < 0) ok = 0;
    struct timespec ts[2] = {sb->st_atim, sb->st_mtim};
    ok = ok && fchown(out, sb->st_uid, sb->st_gid) == 0 && fchmod(out, sb->st_mode & 07777) == 0 &&
         (ln <= 0 || fsetxattr(out, "security.selinux", label, ln, 0) == 0) &&
         futimens(out, ts) == 0 && fsync(out) == 0;
    close(out);
  }
  close(fd);
  if (ok && rename(tmp, path) == 0) { printf("stripped %s\n", path); n_stripped++; }
  else { unlink(tmp); printf("FAIL strip %s: %s\n", path, strerror(errno)); n_strip_failed++; }
  return 0;
}

static int strip(int argc, char **argv) {
  for (int i = 2; i < argc; i++) nftw(argv[i], strip_one, 32, FTW_PHYS);
  printf("fsveritytest strip: %d files made plain, %d failed\n", n_stripped, n_strip_failed);
  return n_strip_failed ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "list")) return list(argc, argv);
  if (argc > 1 && !strcmp(argv[1], "strip")) return strip(argc, argv);
  static const size_t sizes[] = {0, 1, 4096, 4097, 128 * 4096, 128 * 4096 + 1, 3000000, 16777216 + 12345};
  const char *dirs[] = {"/data/local/tmp", "/data/misc_de/0"};
  srandom(1);
  for (int d = 0; d < 2; d++) {
    if (access(dirs[d], W_OK)) { printf("SKIP %s: not writable\n", dirs[d]); continue; }
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) one_file(dirs[d], sizes[i], 0);
    one_file(dirs[d], 1000000, 1);
    negative(dirs[d]);
  }
  printf("fsveritytest: %d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
