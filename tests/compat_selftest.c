/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * compat_selftest.c -- on-device regression test for hd900b_compat.o (Linux 2.6.34, uClibc-ng 1.0.50).
 *
 * Build:  see tests/build_selftest.sh   Run on the player: ./compat_selftest ; exit code = number of FAILs.
 * Covers: stat/lstat/fstat/stat64/fstatat, getrlimit/setrlimit/prlimit, glob/ftw/sysconf (libc-internal callers),
 *         getrandom, the time64 repair (time/gettimeofday/clock_gettime/nanosleep/select/usleep), pthread + TLS.
 */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <glob.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#include <sys/types.h>
extern ssize_t getrandom(void *, size_t, unsigned int);   /* no sysroot header */
static int fails;
static void out(const char *f, ...)
{
    char b[512]; va_list a; va_start(a, f);
    int n = vsnprintf(b, sizeof b, f, a); va_end(a);
    if (n > 0) { if (n > (int)sizeof b - 1) n = sizeof b - 1; write(1, b, n); }
}
#define CHECK(name, cond, ...) do { int ok_ = !!(cond); if (!ok_) fails++; \
    out("%s %-34s ", ok_ ? "PASS" : "FAIL", name); out(__VA_ARGS__); out("\n"); } while (0)

static __thread int tls_val = 7;
static int thr_seen, thr_tls_ok;
static void *thr(void *p)
{
    (void)p;
    tls_val = 99; thr_seen = 1; thr_tls_ok = (tls_val == 99);
    return (void *)5;
}
static int ftw_count;
static int ftw_cb(const char *p, const struct stat *s, int t) { (void)p; (void)s; (void)t; ftw_count++; return 0; }

int main(void)
{
    struct stat st, st1; struct stat64 s64, f64; struct rlimit rl; struct rlimit64 rl64;
    int fd; long v;
    out("compat_selftest (sizeof stat=%d stat64=%d rlimit=%d)\n",
        (int)sizeof st, (int)sizeof s64, (int)sizeof rl);

    errno = 0; v = stat("/tmp", &st);
    CHECK("stat(/tmp)", v == 0 && S_ISDIR(st.st_mode), "rc=%ld errno=%d mode=%o ino=%lu", v, errno, st.st_mode, (unsigned long)st.st_ino);
    errno = 0; v = lstat("/tmp", &st1);
    CHECK("lstat(/tmp)", v == 0 && S_ISDIR(st1.st_mode), "rc=%ld errno=%d", v, errno);
    CHECK("stat==lstat ino/dev", st.st_ino == st1.st_ino && st.st_dev == st1.st_dev, "ino=%lu dev=%lu", (unsigned long)st.st_ino, (unsigned long)st.st_dev);
    errno = 0; v = fstat(1, &st1);
    CHECK("fstat(1)", v == 0, "rc=%ld errno=%d mode=%o", v, errno, st1.st_mode);
    errno = 0; v = stat("/nonexistent", &st1);
    CHECK("stat(/nonexistent)=ENOENT", v == -1 && errno == ENOENT, "rc=%ld errno=%d", v, errno);
    errno = 0; v = lstat("/nonexistent", &st1);
    CHECK("lstat(/nonexistent)=ENOENT", v == -1 && errno == ENOENT, "rc=%ld errno=%d", v, errno);
    errno = 0; v = fstat(-1, &st1);
    CHECK("fstat(-1)=EBADF", v == -1 && errno == EBADF, "rc=%ld errno=%d", v, errno);

    /* cross-check my stat64 against libc's own (old-syscall based) fstat64 */
    errno = 0; v = stat64("/tmp", &s64);
    fd = open("/tmp", O_RDONLY);
    if (fd >= 0) fstat64(fd, &f64); else memset(&f64, 0xee, sizeof f64);
    CHECK("stat64(/tmp)", v == 0 && S_ISDIR(s64.st_mode), "rc=%ld errno=%d", v, errno);
    CHECK("stat64 == libc fstat64", s64.st_dev == f64.st_dev && s64.st_ino == f64.st_ino &&
          s64.st_mode == f64.st_mode && s64.st_nlink == f64.st_nlink && s64.st_uid == f64.st_uid &&
          s64.st_gid == f64.st_gid && s64.st_size == f64.st_size && s64.st_mtim.tv_sec == f64.st_mtim.tv_sec &&
          s64.st_blksize == f64.st_blksize,
          "ino=%llu size=%lld mtime=%ld", (unsigned long long)s64.st_ino, (long long)s64.st_size, (long)s64.st_mtim.tv_sec);
    CHECK("stat(32) == stat64 fields", (unsigned long long)st.st_ino == s64.st_ino && st.st_mode == s64.st_mode &&
          st.st_nlink == s64.st_nlink && st.st_uid == s64.st_uid && st.st_mtim.tv_sec == s64.st_mtim.tv_sec,
          "mtime=%ld", (long)st.st_mtim.tv_sec);
    errno = 0; v = lstat64("/tmp", &s64);
    CHECK("lstat64(/tmp)", v == 0, "rc=%ld errno=%d", v, errno);
    errno = 0; v = fstatat(AT_FDCWD, "/tmp", &st1, 0);
    CHECK("fstatat(CWD,/tmp)", v == 0 && st1.st_ino == st.st_ino, "rc=%ld errno=%d", v, errno);
    errno = 0; v = fstatat64(AT_FDCWD, "/tmp", &s64, AT_SYMLINK_NOFOLLOW);
    CHECK("fstatat64(NOFOLLOW)", v == 0, "rc=%ld errno=%d  (newfstatat 4293: first device test)", v, errno);
    if (fd >= 0) {
        errno = 0; v = fstatat64(fd, "", &s64, AT_EMPTY_PATH);
        CHECK("fstatat64(fd,'',EMPTY)", v == 0 && s64.st_ino == f64.st_ino, "rc=%ld errno=%d", v, errno);
        close(fd);
    }

    errno = 0; v = getrlimit(RLIMIT_STACK, &rl);
    CHECK("getrlimit(STACK)", v == 0, "rc=%ld errno=%d cur=%lu max=%lu (INF=%lu)", v, errno, (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max, (unsigned long)RLIM_INFINITY);
    errno = 0; v = getrlimit(RLIMIT_NOFILE, &rl);
    CHECK("getrlimit(NOFILE)", v == 0 && rl.rlim_cur > 0, "rc=%ld errno=%d cur=%lu max=%lu", v, errno, (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    errno = 0; v = getrlimit64(RLIMIT_STACK, &rl64);
    CHECK("getrlimit64(STACK)", v == 0, "rc=%ld errno=%d cur=%llu max=%llu", v, errno, (unsigned long long)rl64.rlim_cur, (unsigned long long)rl64.rlim_max);
    errno = 0; v = setrlimit(RLIMIT_NOFILE, &rl);   /* write back the same value */
    CHECK("setrlimit(NOFILE same)", v == 0, "rc=%ld errno=%d (setrlimit 4075: first device test)", v, errno);
    errno = 0; v = prlimit(0, RLIMIT_NOFILE, NULL, &rl);
    CHECK("prlimit(0,NOFILE)", v == 0, "rc=%ld errno=%d cur=%lu", v, errno, (unsigned long)rl.rlim_cur);
    errno = 0; v = prlimit(1, RLIMIT_NOFILE, NULL, &rl);
    CHECK("prlimit(pid1)=ENOSYS(by design)", v == -1 && errno == ENOSYS, "rc=%ld errno=%d", v, errno);

    /* libc-internal callers (these go through __GI_* aliases) */
    errno = 0; v = sysconf(_SC_OPEN_MAX);
    CHECK("sysconf(_SC_OPEN_MAX)", v > 0, "val=%ld errno=%d", v, errno);
    errno = 0; v = getdtablesize();
    CHECK("getdtablesize", v > 0, "val=%ld errno=%d", v, errno);
    { glob_t g; memset(&g, 0, sizeof g); errno = 0; v = glob("/tmp/*", 0, NULL, &g);
      CHECK("glob(/tmp/*) [internal stat]", v == 0 || v == GLOB_NOMATCH, "rc=%ld matches=%d errno=%d", v, (int)g.gl_pathc, errno); globfree(&g); }
    errno = 0; ftw_count = 0; v = ftw("/tmp", ftw_cb, 8);
    CHECK("ftw(/tmp) [internal stat]", v == 0, "rc=%ld nodes=%d errno=%d", v, ftw_count, errno);
    errno = 0; v = pathconf("/tmp", _PC_NAME_MAX);
    CHECK("pathconf(/tmp,NAME_MAX)", v > 0, "val=%ld errno=%d", v, errno);
    { DIR *d = opendir("/tmp"); CHECK("opendir(/tmp)", d != NULL, "errno=%d", errno); if (d) closedir(d); }

    { unsigned char r[16]; ssize_t n; memset(r, 0, sizeof r); errno = 0; n = getrandom(r, sizeof r, 0);
      CHECK("getrandom(16) [syscall or /dev/urandom]", n == 16, "n=%ld errno=%d first=%02x%02x", (long)n, errno, r[0], r[1]); }


    /* ---- time64 repair checks (uClibc-ng time_t is 64-bit; kernel 2.6.34 timespec is 32-bit) ---- */
    { struct timespec rt, mo, m2; struct timeval tv; time_t t1; struct tm g; long long d1, d2; long ms;
      t1 = time(NULL); gettimeofday(&tv, NULL); clock_gettime(CLOCK_REALTIME, &rt); clock_gettime(CLOCK_MONOTONIC, &mo);
      CHECK("time() high word clean", ((long long)t1 >> 32) == 0 && t1 > 1700000000LL, "time=%lld", (long long)t1);
      d1 = (long long)tv.tv_sec - (long long)t1; d2 = (long long)rt.tv_sec - (long long)t1;
      CHECK("gettimeofday/clock_gettime agree with time", d1 >= 0 && d1 <= 2 && d2 >= 0 && d2 <= 2, "tv=%lld rt=%lld", (long long)tv.tv_sec, (long long)rt.tv_sec);
      CHECK("tv_usec/tv_nsec in range", tv.tv_usec >= 0 && tv.tv_usec < 1000000 && rt.tv_nsec >= 0 && rt.tv_nsec < 1000000000L && mo.tv_nsec >= 0 && mo.tv_nsec < 1000000000L, "usec=%ld nsec=%ld mono_nsec=%ld", (long)tv.tv_usec, (long)rt.tv_nsec, (long)mo.tv_nsec);
      CHECK("monotonic high word clean", ((long long)mo.tv_sec >> 32) == 0, "mono_sec=%lld", (long long)mo.tv_sec);
      memset(&g, 0, sizeof g); CHECK("gmtime_r(time()) year", gmtime_r(&t1, &g) != NULL && g.tm_year + 1900 >= 2026 && g.tm_year + 1900 < 2100, "year=%d", g.tm_year + 1900);
      { struct timespec rq = { 0, 300000000L }; clock_gettime(CLOCK_MONOTONIC, &mo); nanosleep(&rq, NULL); clock_gettime(CLOCK_MONOTONIC, &m2);
        ms = (m2.tv_sec - mo.tv_sec) * 1000 + (m2.tv_nsec - mo.tv_nsec) / 1000000; CHECK("nanosleep(300ms) duration", ms >= 250 && ms <= 700, "measured=%ld ms", ms); }
      { struct timeval to = { 0, 300000 }; clock_gettime(CLOCK_MONOTONIC, &mo); select(0, NULL, NULL, NULL, &to); clock_gettime(CLOCK_MONOTONIC, &m2);
        ms = (m2.tv_sec - mo.tv_sec) * 1000 + (m2.tv_nsec - mo.tv_nsec) / 1000000; CHECK("select(300ms) duration", ms >= 250 && ms <= 700, "measured=%ld ms", ms); }
      { clock_gettime(CLOCK_MONOTONIC, &mo); usleep(200000); clock_gettime(CLOCK_MONOTONIC, &m2);
        ms = (m2.tv_sec - mo.tv_sec) * 1000 + (m2.tv_nsec - mo.tv_nsec) / 1000000; CHECK("usleep(200ms) duration", ms >= 150 && ms <= 600, "measured=%ld ms", ms); }
    }

    { pthread_t t; void *rv = 0; pthread_attr_t a; size_t ss = 0; int rc;
      pthread_attr_init(&a); pthread_attr_getstacksize(&a, &ss);
      out("INFO default pthread stacksize=%lu\n", (unsigned long)ss);
      rc = pthread_create(&t, NULL, thr, NULL);
      CHECK("pthread_create", rc == 0, "rc=%d", rc);
      if (rc == 0) { rc = pthread_join(t, &rv);
        CHECK("pthread_join+TLS", rc == 0 && (long)rv == 5 && thr_seen && thr_tls_ok && tls_val == 7, "rc=%d ret=%ld main_tls=%d", rc, (long)rv, tls_val); }
      { pthread_attr_t ga; memset(&ga, 0, sizeof ga); errno = 0;
        rc = pthread_getattr_np(pthread_self(), &ga);   /* calls getrlimit */
        CHECK("pthread_getattr_np(self)", rc == 0, "rc=%d (reads /proc/self/maps)", rc); } }

    out("RESULT: fails=%d\n", fails);
    return fails > 100 ? 100 : fails;
}
