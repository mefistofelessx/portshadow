#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#include "bionic_shims.h"
#include "softfp_thunks.h"

/* Assembly implementations in bionic_setjmp.S */
extern int  bionic_setjmp(void *env);
extern void bionic_longjmp(void *env, int val);
extern int  bionic_sigsetjmp(void *env, int savemask);
extern void bionic_siglongjmp(void *env, int val);

/* ── Global data symbols & extra lookup tables ─────────────────────────── */

extern char **environ;

uintptr_t __stack_chk_guard_shim = 0x595a4b3c;
static uintptr_t g_end_marker = 0;

/* Bionic API 19 FILE is 84 bytes per slot: stdin[0], stdout[1], stderr[2] */
#define BIONIC_FILE_SIZE 84
static uint8_t g_bionic_sF[3 * BIONIC_FILE_SIZE];

static const so_default_dynlib *g_extra1 = NULL;
static int g_extra1_bytes = 0;
static const so_default_dynlib *g_extra2 = NULL;
static int g_extra2_bytes = 0;

void bionic_shims_init(void) {
#ifdef AT_RANDOM
    const uint8_t *rnd = (const uint8_t *)getauxval(AT_RANDOM);
    if (rnd) {
        uintptr_t val = 0;
        memcpy(&val, rnd, sizeof(val));
        val &= ~(uintptr_t)0xff; /* keep a NUL byte like Bionic/glibc */
        if (val) __stack_chk_guard_shim = val;
    }
#endif
}

void bionic_shims_set_extra_tables(const so_default_dynlib *extra1, int extra1_bytes,
                                   const so_default_dynlib *extra2, int extra2_bytes) {
    g_extra1 = extra1;
    g_extra1_bytes = extra1_bytes;
    g_extra2 = extra2;
    g_extra2_bytes = extra2_bytes;
}

/* ── __errno / __assert2 / __aeabi_* / __gnu_Unwind_Find_exidx ─────────── */

static int *shim_errno(void) {
    return __errno_location();
}

static void shim_assert2(const char *file, int line, const char *func, const char *expr) {
    fprintf(stderr, "[ASSERT2] %s:%d: %s: assertion \"%s\" failed\n",
            file ? file : "?", line, func ? func : "?", expr ? expr : "?");
    fflush(stderr);
    abort();
}

static void *shim_aeabi_memcpy(void *dst, const void *src, size_t n) {
    return memcpy(dst, src, n);
}

static void *shim_aeabi_memmove(void *dst, const void *src, size_t n) {
    return memmove(dst, src, n);
}

/* NOTE: __aeabi_memset argument order is (dest, count, byte) — swapped vs memset! */
static void *shim_aeabi_memset(void *dst, size_t n, int c) {
    return memset(dst, c, n);
}

static void *shim_aeabi_memclr(void *dst, size_t n) {
    return memset(dst, 0, n);
}

extern uintptr_t __gnu_Unwind_Find_exidx(uintptr_t pc, int *pcount) __attribute__((weak));

static uintptr_t shim_gnu_Unwind_Find_exidx(uintptr_t pc, int *pcount) {
    uintptr_t res = so_find_exidx(pc, pcount);
    if (res) return res;
    if (__gnu_Unwind_Find_exidx)
        return __gnu_Unwind_Find_exidx(pc, pcount);
    if (pcount) *pcount = 0;
    return 0;
}

/* ── Android logging & system properties ───────────────────────────────── */

static int shim_android_log_write(int prio, const char *tag, const char *msg) {
    (void)prio;
    return printf("[ANDR/%s] %s\n", tag ? tag : "?", msg ? msg : "");
}

static int shim_android_log_buf_write(int bufID, int prio, const char *tag, const char *msg) {
    (void)bufID;
    return shim_android_log_write(prio, tag, msg);
}

static int shim_android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    (void)prio;
    printf("[ANDR/%s] ", tag ? tag : "?");
    int r = vprintf(fmt ? fmt : "", ap);
    putchar('\n');
    return r;
}

static int shim_android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = shim_android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return r;
}

typedef struct {
    const char *name;
    const char *value;
} fake_prop_t;

static const fake_prop_t g_fake_props[] = {
    { "ro.build.version.sdk",     "29" },
    { "ro.build.version.release", "10" },
    { "ro.product.model",         "R36UltraX" },
    { "ro.product.manufacturer",  "GameConsole" },
    { "ro.product.brand",         "R36" },
    { "ro.product.device",        "r36ux" },
    { "ro.product.board",         "rk3326" },
    { "ro.product.cpu.abi",       "armeabi-v7a" },
    { "ro.product.cpu.abilist",   "armeabi-v7a,armeabi" },
    { "ro.hardware",              "rk3326" },
    { "debug.egl.hw",             "1" },
    { NULL, NULL }
};

static const void *shim_system_property_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; g_fake_props[i].name; i++) {
        if (strcmp(g_fake_props[i].name, name) == 0)
            return &g_fake_props[i];
    }
    return NULL;
}

static int shim_system_property_read(const void *pi, char *name, char *value) {
    const fake_prop_t *p = (const fake_prop_t *)pi;
    if (!p) return 0;
    if (name) strcpy(name, p->name);
    if (value) {
        strcpy(value, p->value);
        return (int)strlen(p->value);
    }
    return 0;
}

static int shim_system_property_get(const char *name, char *value) {
    const void *pi = shim_system_property_find(name);
    if (!pi) {
        if (value) value[0] = '\0';
        return 0;
    }
    return shim_system_property_read(pi, NULL, value);
}

static int shim_system_property_set(const char *key, const char *value) {
    (void)key; (void)value;
    return 0;
}

/* ── FILE* / stdio shims ───────────────────────────────────────────────── */

static FILE *shim_file(FILE *f) {
    uintptr_t addr = (uintptr_t)f;
    uintptr_t base = (uintptr_t)g_bionic_sF;
    if (addr == base)                        return stdin;
    if (addr == base + BIONIC_FILE_SIZE)     return stdout;
    if (addr == base + 2 * BIONIC_FILE_SIZE) return stderr;
    return f;
}

static FILE *shim_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static int   shim_fclose(FILE *f)                           { return fclose(shim_file(f)); }
static int   shim_vfprintf(FILE *f, const char *fmt, va_list ap) { return vfprintf(shim_file(f), fmt, ap); }

static int shim_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(shim_file(f), fmt, ap);
    va_end(ap);
    return r;
}

static int    shim_fputs(const char *s, FILE *f)                   { return fputs(s, shim_file(f)); }
static int    shim_fputc(int c, FILE *f)                           { return fputc(c, shim_file(f)); }
static size_t shim_fwrite(const void *p, size_t s, size_t n, FILE *f) { return fwrite(p, s, n, shim_file(f)); }
static size_t shim_fread(void *p, size_t s, size_t n, FILE *f)     { return fread(p, s, n, shim_file(f)); }
static char  *shim_fgets(char *s, int n, FILE *f)                  { return fgets(s, n, shim_file(f)); }
static int    shim_fflush(FILE *f)                                 { return fflush(f ? shim_file(f) : NULL); }
static int    shim_fseek(FILE *f, long off, int w)                 { return fseek(shim_file(f), off, w); }
static int    shim_fseeko(FILE *f, off_t off, int w)               { return fseeko(shim_file(f), off, w); }
static long   shim_ftell(FILE *f)                                  { return ftell(shim_file(f)); }
static off_t  shim_ftello(FILE *f)                                 { return ftello(shim_file(f)); }
static int    shim_feof(FILE *f)                                   { return feof(shim_file(f)); }
static int    shim_ferror(FILE *f)                                 { return ferror(shim_file(f)); }
static void   shim_clearerr(FILE *f)                               { clearerr(shim_file(f)); }
static FILE  *shim_popen(const char *cmd, const char *type)        { return popen(cmd, type); }
static int    shim_pclose(FILE *f)                                 { return pclose(shim_file(f)); }

static int shim_fscanf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(shim_file(f), fmt, ap);
    va_end(ap);
    return r;
}

/* ── stat / statfs / dirent / nftw / getpwuid / lseek64 ────────────────── */

/* Bionic ARM32 API 19 struct stat (104 bytes) */
typedef struct {
    uint64_t st_dev;
    uint8_t  __pad0[4];
    uint32_t __st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint8_t  __pad3[4];
    int32_t  __pad4;
    int64_t  st_size;
    uint32_t st_blksize;
    uint32_t __pad5;
    uint64_t st_blocks;
    uint32_t st_atime_sec;
    uint32_t st_atime_nsec;
    uint32_t st_mtime_sec;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime_sec;
    uint32_t st_ctime_nsec;
    uint64_t st_ino;
} bionic_stat_t;

static void stat64_to_bionic(const struct stat64 *src, bionic_stat_t *dst) {
    memset(dst, 0, sizeof(*dst));
    dst->st_dev        = (uint64_t)src->st_dev;
    dst->__st_ino      = (uint32_t)src->st_ino;
    dst->st_mode       = (uint32_t)src->st_mode;
    dst->st_nlink      = (uint32_t)src->st_nlink;
    dst->st_uid        = (uint32_t)src->st_uid;
    dst->st_gid        = (uint32_t)src->st_gid;
    dst->st_rdev       = (uint64_t)src->st_rdev;
    dst->st_size       = (int64_t)src->st_size;
    dst->st_blksize    = (uint32_t)src->st_blksize;
    dst->st_blocks     = (uint64_t)src->st_blocks;
    dst->st_atime_sec  = (uint32_t)src->st_atim.tv_sec;
    dst->st_atime_nsec = (uint32_t)src->st_atim.tv_nsec;
    dst->st_mtime_sec  = (uint32_t)src->st_mtim.tv_sec;
    dst->st_mtime_nsec = (uint32_t)src->st_mtim.tv_nsec;
    dst->st_ctime_sec  = (uint32_t)src->st_ctim.tv_sec;
    dst->st_ctime_nsec = (uint32_t)src->st_ctim.tv_nsec;
    dst->st_ino        = (uint64_t)src->st_ino;
}

static int shim_stat(const char *path, bionic_stat_t *buf) {
    struct stat64 st;
    int r = stat64(path, &st);
    if (r == 0 && buf) stat64_to_bionic(&st, buf);
    return r;
}

static int shim_lstat(const char *path, bionic_stat_t *buf) {
    struct stat64 st;
    int r = lstat64(path, &st);
    if (r == 0 && buf) stat64_to_bionic(&st, buf);
    return r;
}

static int shim_fstat(int fd, bionic_stat_t *buf) {
    struct stat64 st;
    int r = fstat64(fd, &st);
    if (r == 0 && buf) stat64_to_bionic(&st, buf);
    return r;
}

typedef struct {
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    uint32_t f_namelen;
    uint32_t f_frsize;
    uint32_t f_flags;
    uint32_t f_spare[4];
} bionic_statfs_t;

static int shim_statfs(const char *path, bionic_statfs_t *buf) {
    struct statfs64 s;
    int r = statfs64(path, &s);
    if (r == 0 && buf) {
        memset(buf, 0, sizeof(*buf));
        buf->f_type    = (uint32_t)s.f_type;
        buf->f_bsize   = (uint32_t)s.f_bsize;
        buf->f_blocks  = (uint64_t)s.f_blocks;
        buf->f_bfree   = (uint64_t)s.f_bfree;
        buf->f_bavail  = (uint64_t)s.f_bavail;
        buf->f_files   = (uint64_t)s.f_files;
        buf->f_ffree   = (uint64_t)s.f_ffree;
        memcpy(buf->f_fsid, &s.f_fsid, sizeof(buf->f_fsid));
        buf->f_namelen = (uint32_t)s.f_namelen;
        buf->f_frsize  = (uint32_t)s.f_frsize;
        buf->f_flags   = (uint32_t)s.f_flags;
    }
    return r;
}

typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
} bionic_dirent_t;

typedef struct {
    DIR             *host_dir;
    bionic_dirent_t  ent;
} bionic_dir_t;

static void *shim_opendir(const char *name) {
    DIR *d = opendir(name);
    if (!d) return NULL;
    bionic_dir_t *bd = calloc(1, sizeof(*bd));
    if (!bd) { closedir(d); errno = ENOMEM; return NULL; }
    bd->host_dir = d;
    return bd;
}

static bionic_dirent_t *shim_readdir(void *dirp) {
    bionic_dir_t *bd = (bionic_dir_t *)dirp;
    if (!bd) { errno = EBADF; return NULL; }
    struct dirent64 *e = readdir64(bd->host_dir);
    if (!e) return NULL;
    bd->ent.d_ino    = (uint64_t)e->d_ino;
    bd->ent.d_off    = (int64_t)e->d_off;
    bd->ent.d_reclen = (uint16_t)sizeof(bd->ent);
    bd->ent.d_type   = e->d_type;
    strncpy(bd->ent.d_name, e->d_name, sizeof(bd->ent.d_name) - 1);
    bd->ent.d_name[sizeof(bd->ent.d_name) - 1] = '\0';
    return &bd->ent;
}

static int shim_closedir(void *dirp) {
    bionic_dir_t *bd = (bionic_dir_t *)dirp;
    if (!bd) { errno = EBADF; return -1; }
    int r = closedir(bd->host_dir);
    free(bd);
    return r;
}

typedef int (*bionic_nftw_cb)(const char *, const bionic_stat_t *, int, struct FTW *);
static __thread bionic_nftw_cb tls_nftw_cb = NULL;

static int nftw_trampoline(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    bionic_stat_t bs;
    if (sb) {
        struct stat64 st64;
        memset(&st64, 0, sizeof(st64));
        st64.st_dev     = sb->st_dev;
        st64.st_ino     = sb->st_ino;
        st64.st_mode    = sb->st_mode;
        st64.st_nlink   = sb->st_nlink;
        st64.st_uid     = sb->st_uid;
        st64.st_gid     = sb->st_gid;
        st64.st_rdev    = sb->st_rdev;
        st64.st_size    = sb->st_size;
        st64.st_blksize = sb->st_blksize;
        st64.st_blocks  = sb->st_blocks;
        st64.st_atim    = sb->st_atim;
        st64.st_mtim    = sb->st_mtim;
        st64.st_ctim    = sb->st_ctim;
        stat64_to_bionic(&st64, &bs);
    }
    return tls_nftw_cb ? tls_nftw_cb(fpath, sb ? &bs : NULL, typeflag, ftwbuf) : 0;
}

static int shim_nftw(const char *dirpath, bionic_nftw_cb fn, int nopenfd, int flags) {
    bionic_nftw_cb prev = tls_nftw_cb;
    tls_nftw_cb = fn;
    int r = nftw(dirpath, nftw_trampoline, nopenfd, flags);
    tls_nftw_cb = prev;
    return r;
}

/* Bionic passwd omits pw_gecos */
typedef struct {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_dir;
    char *pw_shell;
} bionic_passwd_t;

static __thread bionic_passwd_t tls_passwd;

static bionic_passwd_t *shim_getpwuid(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (!pw) return NULL;
    tls_passwd.pw_name   = pw->pw_name;
    tls_passwd.pw_passwd = pw->pw_passwd;
    tls_passwd.pw_uid    = pw->pw_uid;
    tls_passwd.pw_gid    = pw->pw_gid;
    tls_passwd.pw_dir    = pw->pw_dir;
    tls_passwd.pw_shell  = pw->pw_shell;
    return &tls_passwd;
}

static int64_t shim_lseek64(int fd, int64_t offset, int whence) {
    return (int64_t)lseek64(fd, (off64_t)offset, whence);
}

/* ── Signals & setjmp/longjmp ──────────────────────────────────────────── */

/* Bionic ARM32 API 19 sigset_t is 32-bit */
static void bionic_mask_to_sigset(uint32_t mask, sigset_t *out) {
    sigemptyset(out);
    for (int sig = 1; sig <= 32; sig++) {
        if (mask & (1U << (sig - 1)))
            sigaddset(out, sig);
    }
}

static uint32_t sigset_to_bionic_mask(const sigset_t *in) {
    uint32_t mask = 0;
    for (int sig = 1; sig <= 32; sig++) {
        if (sigismember(in, sig) == 1)
            mask |= (1U << (sig - 1));
    }
    return mask;
}

void bionic_save_sigmask(uint32_t *out_mask) {
    sigset_t cur;
    sigemptyset(&cur);
    sigprocmask(SIG_SETMASK, NULL, &cur);
    if (out_mask) *out_mask = sigset_to_bionic_mask(&cur);
}

void bionic_restore_sigmask(uint32_t mask) {
    sigset_t set;
    bionic_mask_to_sigset(mask, &set);
    sigprocmask(SIG_SETMASK, &set, NULL);
}

typedef struct {
    union {
        void (*b_handler)(int);
        void (*b_sigaction)(int, siginfo_t *, void *);
    };
    uint32_t b_mask;
    int      b_flags;
    void   (*b_restorer)(void);
} bionic_sigaction_t;

static int shim_sigaction(int signum, const bionic_sigaction_t *act, bionic_sigaction_t *oldact) {
    struct sigaction ha, hoa;
    if (act) {
        memset(&ha, 0, sizeof(ha));
        if (act->b_flags & SA_SIGINFO)
            ha.sa_sigaction = act->b_sigaction;
        else
            ha.sa_handler = act->b_handler;
        bionic_mask_to_sigset(act->b_mask, &ha.sa_mask);
        ha.sa_flags = act->b_flags;
    }
    int r = sigaction(signum, act ? &ha : NULL, oldact ? &hoa : NULL);
    if (r == 0 && oldact) {
        if (hoa.sa_flags & SA_SIGINFO)
            oldact->b_sigaction = hoa.sa_sigaction;
        else
            oldact->b_handler = hoa.sa_handler;
        oldact->b_mask     = sigset_to_bionic_mask(&hoa.sa_mask);
        oldact->b_flags    = hoa.sa_flags;
        oldact->b_restorer = NULL;
    }
    return r;
}

typedef struct {
    void  *ss_sp;
    int    ss_flags;
    size_t ss_size;
} bionic_stack_t;

static int shim_sigaltstack(const bionic_stack_t *ss, bionic_stack_t *oss) {
    stack_t hss, hoss;
    if (ss) {
        hss.ss_sp    = ss->ss_sp;
        hss.ss_flags = ss->ss_flags;
        hss.ss_size  = ss->ss_size;
    }
    int r = sigaltstack(ss ? &hss : NULL, oss ? &hoss : NULL);
    if (r == 0 && oss) {
        oss->ss_sp    = hoss.ss_sp;
        oss->ss_flags = hoss.ss_flags;
        oss->ss_size  = hoss.ss_size;
    }
    return r;
}

static int shim_sigsuspend(const uint32_t *mask) {
    sigset_t set;
    bionic_mask_to_sigset(mask ? *mask : 0, &set);
    return sigsuspend(&set);
}

static int shim_pthread_sigmask(int how, const uint32_t *set, uint32_t *oset) {
    sigset_t hs, hos;
    if (set) bionic_mask_to_sigset(*set, &hs);
    int r = pthread_sigmask(how, set ? &hs : NULL, oset ? &hos : NULL);
    if (r == 0 && oset) *oset = sigset_to_bionic_mask(&hos);
    return r;
}

static void *shim_bsd_signal(int signum, void (*handler)(int)) {
    struct sigaction sa, osa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(signum, &sa, &osa) < 0)
        return (void *)SIG_ERR;
    return (void *)osa.sa_handler;
}

/* ── pthread_* and sem_* wrappers ──────────────────────────────────────── */

#define BIONIC_MUTEX_NORMAL_INIT     0x0000U
#define BIONIC_MUTEX_RECURSIVE_INIT  0x4000U
#define BIONIC_MUTEX_ERRORCHECK_INIT 0x8000U
#define BIONIC_STATIC_THRESHOLD      0x10000U

static pthread_mutex_t *unwrap_mutex(uintptr_t *slot, int create) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val >= BIONIC_STATIC_THRESHOLD)
        return (pthread_mutex_t *)val;
    if (!create)
        return NULL;

    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m) return NULL;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (val == BIONIC_MUTEX_RECURSIVE_INIT)
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    else if (val == BIONIC_MUTEX_ERRORCHECK_INIT)
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    else
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);

    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);

    uintptr_t expected = val;
    if (!__atomic_compare_exchange_n(slot, &expected, (uintptr_t)m, 0,
                                     __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        pthread_mutex_destroy(m);
        free(m);
        return (pthread_mutex_t *)expected;
    }
    return m;
}

static int shim_pthread_mutexattr_init(uintptr_t *attr) {
    pthread_mutexattr_t *a = malloc(sizeof(*a));
    if (!a) return ENOMEM;
    int r = pthread_mutexattr_init(a);
    if (r) { free(a); return r; }
    *attr = (uintptr_t)a;
    return 0;
}

static int shim_pthread_mutexattr_destroy(uintptr_t *attr) {
    if (!attr || !*attr) return EINVAL;
    pthread_mutexattr_t *a = (pthread_mutexattr_t *)(*attr);
    int r = pthread_mutexattr_destroy(a);
    free(a);
    *attr = 0;
    return r;
}

static int shim_pthread_mutexattr_settype(uintptr_t *attr, int kind) {
    if (!attr || !*attr) return EINVAL;
    pthread_mutexattr_t *a = (pthread_mutexattr_t *)(*attr);
    int gkind = PTHREAD_MUTEX_NORMAL;
    switch (kind) {
        case 0: gkind = PTHREAD_MUTEX_NORMAL; break;
        case 1: gkind = PTHREAD_MUTEX_RECURSIVE; break;
        case 2: gkind = PTHREAD_MUTEX_ERRORCHECK; break;
        default: gkind = PTHREAD_MUTEX_DEFAULT; break;
    }
    return pthread_mutexattr_settype(a, gkind);
}

static int shim_pthread_mutex_init(uintptr_t *slot, const uintptr_t *bionic_attr) {
    pthread_mutex_t *m = malloc(sizeof(*m));
    if (!m) return ENOMEM;
    pthread_mutexattr_t *ha = (bionic_attr && *bionic_attr) ? (pthread_mutexattr_t *)(*bionic_attr) : NULL;
    int r = pthread_mutex_init(m, ha);
    if (r) { free(m); return r; }
    __atomic_store_n(slot, (uintptr_t)m, __ATOMIC_RELEASE);
    return 0;
}

static int shim_pthread_mutex_destroy(uintptr_t *slot) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val < BIONIC_STATIC_THRESHOLD) {
        *slot = 0;
        return 0;
    }
    pthread_mutex_t *m = (pthread_mutex_t *)val;
    int r = pthread_mutex_destroy(m);
    free(m);
    __atomic_store_n(slot, 0, __ATOMIC_RELEASE);
    return r;
}

static int shim_pthread_mutex_lock(uintptr_t *slot) {
    pthread_mutex_t *m = unwrap_mutex(slot, 1);
    return m ? pthread_mutex_lock(m) : ENOMEM;
}

static int shim_pthread_mutex_trylock(uintptr_t *slot) {
    pthread_mutex_t *m = unwrap_mutex(slot, 1);
    return m ? pthread_mutex_trylock(m) : ENOMEM;
}

static int shim_pthread_mutex_unlock(uintptr_t *slot) {
    pthread_mutex_t *m = unwrap_mutex(slot, 0);
    return m ? pthread_mutex_unlock(m) : EINVAL;
}

static pthread_cond_t *unwrap_cond(uintptr_t *slot, int create) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val >= BIONIC_STATIC_THRESHOLD)
        return (pthread_cond_t *)val;
    if (!create)
        return NULL;

    pthread_cond_t *c = malloc(sizeof(*c));
    if (!c) return NULL;
    pthread_cond_init(c, NULL);

    uintptr_t expected = val;
    if (!__atomic_compare_exchange_n(slot, &expected, (uintptr_t)c, 0,
                                     __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        pthread_cond_destroy(c);
        free(c);
        return (pthread_cond_t *)expected;
    }
    return c;
}

static int shim_pthread_cond_init(uintptr_t *slot, const void *attr) {
    (void)attr;
    pthread_cond_t *c = malloc(sizeof(*c));
    if (!c) return ENOMEM;
    int r = pthread_cond_init(c, NULL);
    if (r) { free(c); return r; }
    __atomic_store_n(slot, (uintptr_t)c, __ATOMIC_RELEASE);
    return 0;
}

static int shim_pthread_cond_destroy(uintptr_t *slot) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val < BIONIC_STATIC_THRESHOLD) {
        *slot = 0;
        return 0;
    }
    pthread_cond_t *c = (pthread_cond_t *)val;
    int r = pthread_cond_destroy(c);
    free(c);
    __atomic_store_n(slot, 0, __ATOMIC_RELEASE);
    return r;
}

static int shim_pthread_cond_wait(uintptr_t *cslot, uintptr_t *mslot) {
    pthread_cond_t  *c = unwrap_cond(cslot, 1);
    pthread_mutex_t *m = unwrap_mutex(mslot, 1);
    return (c && m) ? pthread_cond_wait(c, m) : EINVAL;
}

static int shim_pthread_cond_timedwait(uintptr_t *cslot, uintptr_t *mslot, const struct timespec *abstime) {
    pthread_cond_t  *c = unwrap_cond(cslot, 1);
    pthread_mutex_t *m = unwrap_mutex(mslot, 1);
    return (c && m) ? pthread_cond_timedwait(c, m, abstime) : EINVAL;
}

static int shim_pthread_cond_timedwait_relative_np(uintptr_t *cslot, uintptr_t *mslot, const struct timespec *rel) {
    if (!rel) return shim_pthread_cond_wait(cslot, mslot);
    struct timespec abs;
    clock_gettime(CLOCK_REALTIME, &abs);
    abs.tv_sec  += rel->tv_sec;
    abs.tv_nsec += rel->tv_nsec;
    if (abs.tv_nsec >= 1000000000L) {
        abs.tv_sec += abs.tv_nsec / 1000000000L;
        abs.tv_nsec %= 1000000000L;
    }
    return shim_pthread_cond_timedwait(cslot, mslot, &abs);
}

static int shim_pthread_cond_signal(uintptr_t *cslot) {
    pthread_cond_t *c = unwrap_cond(cslot, 1);
    return c ? pthread_cond_signal(c) : ENOMEM;
}

static int shim_pthread_cond_broadcast(uintptr_t *cslot) {
    pthread_cond_t *c = unwrap_cond(cslot, 1);
    return c ? pthread_cond_broadcast(c) : ENOMEM;
}

static pthread_rwlock_t *unwrap_rwlock(uintptr_t *slot, int create) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val >= BIONIC_STATIC_THRESHOLD)
        return (pthread_rwlock_t *)val;
    if (!create)
        return NULL;

    pthread_rwlock_t *rw = malloc(sizeof(*rw));
    if (!rw) return NULL;
    pthread_rwlock_init(rw, NULL);

    uintptr_t expected = val;
    if (!__atomic_compare_exchange_n(slot, &expected, (uintptr_t)rw, 0,
                                     __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        pthread_rwlock_destroy(rw);
        free(rw);
        return (pthread_rwlock_t *)expected;
    }
    return rw;
}

static int shim_pthread_rwlock_init(uintptr_t *slot, const void *attr) {
    (void)attr;
    pthread_rwlock_t *rw = malloc(sizeof(*rw));
    if (!rw) return ENOMEM;
    int r = pthread_rwlock_init(rw, NULL);
    if (r) { free(rw); return r; }
    __atomic_store_n(slot, (uintptr_t)rw, __ATOMIC_RELEASE);
    return 0;
}

static int shim_pthread_rwlock_destroy(uintptr_t *slot) {
    uintptr_t val = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (val < BIONIC_STATIC_THRESHOLD) { *slot = 0; return 0; }
    pthread_rwlock_t *rw = (pthread_rwlock_t *)val;
    int r = pthread_rwlock_destroy(rw);
    free(rw);
    __atomic_store_n(slot, 0, __ATOMIC_RELEASE);
    return r;
}

static int shim_pthread_rwlock_rdlock(uintptr_t *slot) {
    pthread_rwlock_t *rw = unwrap_rwlock(slot, 1);
    return rw ? pthread_rwlock_rdlock(rw) : ENOMEM;
}

static int shim_pthread_rwlock_wrlock(uintptr_t *slot) {
    pthread_rwlock_t *rw = unwrap_rwlock(slot, 1);
    return rw ? pthread_rwlock_wrlock(rw) : ENOMEM;
}

static int shim_pthread_rwlock_unlock(uintptr_t *slot) {
    pthread_rwlock_t *rw = unwrap_rwlock(slot, 0);
    return rw ? pthread_rwlock_unlock(rw) : EINVAL;
}

/* Bionic pthread_attr_t is 24 bytes; glibc pthread_attr_t is 36 bytes. */
static int shim_pthread_attr_init(uintptr_t *bionic_attr) {
    pthread_attr_t *ha = malloc(sizeof(*ha));
    if (!ha) return ENOMEM;
    int r = pthread_attr_init(ha);
    if (r) { free(ha); return r; }
    *bionic_attr = (uintptr_t)ha;
    return 0;
}

static int shim_pthread_attr_destroy(uintptr_t *bionic_attr) {
    if (!bionic_attr || !*bionic_attr) return EINVAL;
    pthread_attr_t *ha = (pthread_attr_t *)(*bionic_attr);
    int r = pthread_attr_destroy(ha);
    free(ha);
    *bionic_attr = 0;
    return r;
}

static int shim_pthread_attr_getdetachstate(const uintptr_t *bionic_attr, int *state) {
    if (!bionic_attr || !*bionic_attr) return EINVAL;
    int hs = 0;
    int r = pthread_attr_getdetachstate((const pthread_attr_t *)(*bionic_attr), &hs);
    if (r == 0 && state)
        *state = (hs == PTHREAD_CREATE_DETACHED) ? 1 : 0;
    return r;
}

static int shim_pthread_attr_setdetachstate(uintptr_t *bionic_attr, int state) {
    if (!bionic_attr || !*bionic_attr) return EINVAL;
    int hs = (state == 1) ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    return pthread_attr_setdetachstate((pthread_attr_t *)(*bionic_attr), hs);
}

static int shim_pthread_attr_getstack(const uintptr_t *bionic_attr, void **addr, size_t *size) {
    if (!bionic_attr || !*bionic_attr) return EINVAL;
    return pthread_attr_getstack((const pthread_attr_t *)(*bionic_attr), addr, size);
}

static int shim_pthread_attr_setstacksize(uintptr_t *bionic_attr, size_t size) {
    if (!bionic_attr || !*bionic_attr) return EINVAL;
    return pthread_attr_setstacksize((pthread_attr_t *)(*bionic_attr), size);
}

static int shim_pthread_getattr_np(pthread_t thread, uintptr_t *bionic_attr) {
    pthread_attr_t *ha = malloc(sizeof(*ha));
    if (!ha) return ENOMEM;
    int r = pthread_getattr_np(thread, ha);
    if (r) { free(ha); return r; }
    *bionic_attr = (uintptr_t)ha;
    return 0;
}

/* Bionic cleanup stack */
typedef struct bionic_cleanup {
    struct bionic_cleanup *prev;
    void (*routine)(void *);
    void *arg;
} bionic_cleanup_t;

static __thread bionic_cleanup_t *tls_cleanup_head = NULL;

static void shim_pthread_cleanup_push(bionic_cleanup_t *c, void (*routine)(void *), void *arg) {
    if (!c) return;
    c->routine = routine;
    c->arg = arg;
    c->prev = tls_cleanup_head;
    tls_cleanup_head = c;
}

static void shim_pthread_cleanup_pop(bionic_cleanup_t *c, int execute) {
    if (!c) return;
    if (tls_cleanup_head == c)
        tls_cleanup_head = c->prev;
    if (execute && c->routine)
        c->routine(c->arg);
}

static void shim_pthread_exit(void *retval) {
    while (tls_cleanup_head) {
        bionic_cleanup_t *c = tls_cleanup_head;
        tls_cleanup_head = c->prev;
        if (c->routine) c->routine(c->arg);
    }
    pthread_exit(retval);
}

typedef struct {
    void *(*start)(void *);
    void *arg;
} thread_tramp_t;

static void *thread_trampoline(void *p) {
    thread_tramp_t t = *(thread_tramp_t *)p;
    free(p);
    return t.start(t.arg);
}

static int shim_pthread_create(pthread_t *thread, const uintptr_t *bionic_attr,
                               void *(*start_routine)(void *), void *arg) {
    pthread_attr_t *ha = (bionic_attr && *bionic_attr) ? (pthread_attr_t *)(*bionic_attr) : NULL;
    thread_tramp_t *t = malloc(sizeof(*t));
    if (!t) return ENOMEM;
    t->start = start_routine;
    t->arg   = arg;
    int r = pthread_create(thread, ha, thread_trampoline, t);
    if (r) free(t);
    return r;
}

static int shim_pthread_once(int *once_control, void (*init_routine)(void)) {
    return pthread_once((pthread_once_t *)once_control, init_routine);
}

/* Bionic sem_t is 4 bytes */
static int shim_sem_init(uintptr_t *slot, int pshared, unsigned int value) {
    sem_t *s = malloc(sizeof(*s));
    if (!s) { errno = ENOMEM; return -1; }
    if (sem_init(s, pshared, value) != 0) { free(s); return -1; }
    *slot = (uintptr_t)s;
    return 0;
}

static int shim_sem_destroy(uintptr_t *slot) {
    if (!slot || !*slot) { errno = EINVAL; return -1; }
    sem_t *s = (sem_t *)(*slot);
    int r = sem_destroy(s);
    free(s);
    *slot = 0;
    return r;
}

static int shim_sem_wait(uintptr_t *slot)     { return sem_wait((sem_t *)(*slot)); }
static int shim_sem_trywait(uintptr_t *slot)  { return sem_trywait((sem_t *)(*slot)); }
static int shim_sem_timedwait(uintptr_t *slot, const struct timespec *abs) {
    return sem_timedwait((sem_t *)(*slot), abs);
}
static int shim_sem_post(uintptr_t *slot)     { return sem_post((sem_t *)(*slot)); }
static int shim_sem_getvalue(uintptr_t *slot, int *val) {
    return sem_getvalue((sem_t *)(*slot), val);
}
static void *shim_sem_open(const char *name, int oflag, ...) {
    (void)name; (void)oflag;
    errno = ENOSYS;
    return SEM_FAILED;
}

/* ── Misc syscalls / process ───────────────────────────────────────────── */

static pid_t shim_gettid(void) {
    return (pid_t)syscall(SYS_gettid);
}

static long shim_syscall(long number, long a1, long a2, long a3, long a4, long a5, long a6) {
    return syscall(number, a1, a2, a3, a4, a5, a6);
}

static int shim_prctl(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    return prctl(option, a2, a3, a4, a5);
}

static long shim_ptrace(int request, pid_t pid, void *addr, void *data) {
    (void)request; (void)pid; (void)addr; (void)data;
    errno = EPERM;
    return -1;
}

static pid_t shim_fork(void) {
    errno = ENOSYS;
    return -1;
}

static int shim_execl(const char *path, const char *arg, ...) {
    (void)path; (void)arg;
    errno = ENOSYS;
    return -1;
}

static void shim_qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    qsort(base, nmemb, size, compar);
}

static char *shim_mkdtemp(char *tmpl) {
    return mkdtemp(tmpl);
}

static long shim_sysconf(int name) {
    int gname = -1;
    switch (name) {
        case 0x0000: gname = _SC_ARG_MAX; break;
        case 0x0005: gname = _SC_CHILD_MAX; break;
        case 0x0006: gname = _SC_CLK_TCK; break;
        case 0x000a: gname = _SC_NGROUPS_MAX; break;
        case 0x000b: gname = _SC_OPEN_MAX; break;
        case 0x0027: gname = _SC_PAGESIZE; break;
        case 0x0046: gname = _SC_GETGR_R_SIZE_MAX; break;
        case 0x0047: gname = _SC_GETPW_R_SIZE_MAX; break;
        case 0x004e: gname = _SC_LOGIN_NAME_MAX; break;
        case 0x0059: gname = _SC_THREAD_STACK_MIN; break;
        case 0x005e: gname = _SC_TTY_NAME_MAX; break;
        case 0x0060: gname = _SC_NPROCESSORS_CONF; break;
        case 0x0061: gname = _SC_NPROCESSORS_ONLN; break;
        case 0x0062: gname = _SC_PHYS_PAGES; break;
        case 0x0063: gname = _SC_AVPHYS_PAGES; break;
        case 0x0064: gname = _SC_MONOTONIC_CLOCK; break;
        case 0x0067: gname = _SC_HOST_NAME_MAX; break;
        case 0x006e: gname = _SC_IOV_MAX; break;
        default:
            fprintf(stderr, "[SHIM] sysconf(unmapped=%d) -> EINVAL\n", name);
            errno = EINVAL;
            return -1;
    }
    return sysconf(gname);
}

/* ── dlopen / dlsym / dlclose / dlerror / dladdr ───────────────────────── */

#define BIONIC_RTLD_DEFAULT ((void *)(uintptr_t)0xffffffff)
#define BIONIC_RTLD_NEXT    ((void *)(uintptr_t)0xfffffffe)
static int g_self_handle_sentinel = 0;

typedef struct dl_node {
    so_module *mod;
    struct dl_node *next;
} dl_node_t;

static dl_node_t *g_known_so_handles = NULL;

static void register_so_handle(so_module *m) {
    if (!m) return;
    for (dl_node_t *n = g_known_so_handles; n; n = n->next)
        if (n->mod == m) return;
    dl_node_t *n = malloc(sizeof(*n));
    if (n) { n->mod = m; n->next = g_known_so_handles; g_known_so_handles = n; }
}

static int is_so_handle(void *h) {
    for (dl_node_t *n = g_known_so_handles; n; n = n->next)
        if ((void *)n->mod == h) return 1;
    return 0;
}

static const char *map_soname(const char *name) {
    if (!name) return NULL;
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    if (strcmp(base, "libEGL.so") == 0)    return "libEGL.so.1";
    if (strcmp(base, "libGLESv2.so") == 0) return "libGLESv2.so.2";
    if (strcmp(base, "libz.so") == 0)      return "libz.so.1";
    if (strcmp(base, "libc.so") == 0)      return "libc.so.6";
    if (strcmp(base, "libm.so") == 0)      return "libm.so.6";
    if (strcmp(base, "libdl.so") == 0)     return "libdl.so.2";
    if (strcmp(base, "libpthread.so") == 0)return "libpthread.so.0";
    return name;
}

static uintptr_t lookup_dynlib_tables(const char *symbol) {
    const so_default_dynlib *tables[4];
    int sizes[4];
    int n = 0;
    tables[n] = softfp_get_thunks(&sizes[n]); n++;
    tables[n] = bionic_get_shims(&sizes[n]);  n++;
    if (g_extra1 && g_extra1_bytes > 0) { tables[n] = g_extra1; sizes[n] = g_extra1_bytes; n++; }
    if (g_extra2 && g_extra2_bytes > 0) { tables[n] = g_extra2; sizes[n] = g_extra2_bytes; n++; }

    for (int t = 0; t < n; t++) {
        int cnt = sizes[t] / (int)sizeof(so_default_dynlib);
        for (int j = 0; j < cnt; j++) {
            if (strcmp(symbol, tables[t][j].symbol) == 0)
                return tables[t][j].func;
        }
    }
    return 0;
}

static void *shim_dlopen(const char *filename, int flags) {
    if (!filename)
        return &g_self_handle_sentinel;

    so_module *m = so_find_module(filename);
    if (m) {
        register_so_handle(m);
        return m;
    }

    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (strcmp(base, "liblog.so") == 0 || strcmp(base, "libandroid.so") == 0)
        return &g_self_handle_sentinel;

    return dlopen(map_soname(filename), flags);
}

static void *shim_dlsym(void *handle, const char *symbol) {
    if (!symbol) return NULL;

    if (handle == &g_self_handle_sentinel ||
        handle == NULL ||
        handle == BIONIC_RTLD_DEFAULT ||
        handle == BIONIC_RTLD_NEXT) {
        uintptr_t p = lookup_dynlib_tables(symbol);
        if (p) return (void *)p;
        p = so_find_symbol_global(symbol);
        if (p) return (void *)p;
        return dlsym(RTLD_DEFAULT, symbol);
    }

    if (is_so_handle(handle)) {
        return (void *)so_symbol((so_module *)handle, symbol);
    }

    return dlsym(handle, symbol);
}

static int shim_dlclose(void *handle) {
    if (!handle || handle == &g_self_handle_sentinel || is_so_handle(handle))
        return 0;
    return dlclose(handle);
}

static char *shim_dlerror(void) {
    return dlerror();
}

static int shim_dladdr(const void *addr, Dl_info *info) {
    return dladdr(addr, info);
}

/* ── Exported Bionic shim table ────────────────────────────────────────── */

static const so_default_dynlib g_bionic_shims[] = {
    /* Runtime & unwind */
    { "__errno",                    (uintptr_t)shim_errno },
    { "__stack_chk_guard",          (uintptr_t)&__stack_chk_guard_shim },
    { "__end__",                    (uintptr_t)&g_end_marker },
    { "environ",                    (uintptr_t)&environ },
    { "__assert2",                  (uintptr_t)shim_assert2 },
    { "__gnu_Unwind_Find_exidx",    (uintptr_t)shim_gnu_Unwind_Find_exidx },

    /* AEABI memory ops */
    { "__aeabi_memcpy",             (uintptr_t)shim_aeabi_memcpy },
    { "__aeabi_memcpy4",            (uintptr_t)shim_aeabi_memcpy },
    { "__aeabi_memcpy8",            (uintptr_t)shim_aeabi_memcpy },
    { "__aeabi_memmove",            (uintptr_t)shim_aeabi_memmove },
    { "__aeabi_memmove4",           (uintptr_t)shim_aeabi_memmove },
    { "__aeabi_memmove8",           (uintptr_t)shim_aeabi_memmove },
    { "__aeabi_memset",             (uintptr_t)shim_aeabi_memset },
    { "__aeabi_memset4",            (uintptr_t)shim_aeabi_memset },
    { "__aeabi_memset8",            (uintptr_t)shim_aeabi_memset },
    { "__aeabi_memclr",             (uintptr_t)shim_aeabi_memclr },
    { "__aeabi_memclr4",            (uintptr_t)shim_aeabi_memclr },
    { "__aeabi_memclr8",            (uintptr_t)shim_aeabi_memclr },

    /* Android logging & properties */
    { "__android_log_write",        (uintptr_t)shim_android_log_write },
    { "__android_log_buf_write",    (uintptr_t)shim_android_log_buf_write },
    { "__android_log_vprint",       (uintptr_t)shim_android_log_vprint },
    { "__android_log_print",        (uintptr_t)shim_android_log_print },
    { "__system_property_find",     (uintptr_t)shim_system_property_find },
    { "__system_property_read",     (uintptr_t)shim_system_property_read },
    { "__system_property_get",      (uintptr_t)shim_system_property_get },
    { "__system_property_set",      (uintptr_t)shim_system_property_set },

    /* stdio / __sF */
    { "__sF",                       (uintptr_t)g_bionic_sF },
    { "fopen",                      (uintptr_t)shim_fopen },
    { "fclose",                     (uintptr_t)shim_fclose },
    { "fprintf",                    (uintptr_t)shim_fprintf },
    { "vfprintf",                   (uintptr_t)shim_vfprintf },
    { "fputs",                      (uintptr_t)shim_fputs },
    { "fputc",                      (uintptr_t)shim_fputc },
    { "fwrite",                     (uintptr_t)shim_fwrite },
    { "fread",                      (uintptr_t)shim_fread },
    { "fgets",                      (uintptr_t)shim_fgets },
    { "fflush",                     (uintptr_t)shim_fflush },
    { "fseek",                      (uintptr_t)shim_fseek },
    { "fseeko",                     (uintptr_t)shim_fseeko },
    { "ftell",                      (uintptr_t)shim_ftell },
    { "ftello",                     (uintptr_t)shim_ftello },
    { "feof",                       (uintptr_t)shim_feof },
    { "ferror",                     (uintptr_t)shim_ferror },
    { "clearerr",                   (uintptr_t)shim_clearerr },
    { "fscanf",                     (uintptr_t)shim_fscanf },
    { "popen",                      (uintptr_t)shim_popen },
    { "pclose",                     (uintptr_t)shim_pclose },

    /* Filesystem / passwd */
    { "stat",                       (uintptr_t)shim_stat },
    { "lstat",                      (uintptr_t)shim_lstat },
    { "fstat",                      (uintptr_t)shim_fstat },
    { "statfs",                     (uintptr_t)shim_statfs },
    { "opendir",                    (uintptr_t)shim_opendir },
    { "readdir",                    (uintptr_t)shim_readdir },
    { "closedir",                   (uintptr_t)shim_closedir },
    { "nftw",                       (uintptr_t)shim_nftw },
    { "getpwuid",                   (uintptr_t)shim_getpwuid },
    { "lseek64",                    (uintptr_t)shim_lseek64 },

    /* Signals & setjmp */
    { "sigaction",                  (uintptr_t)shim_sigaction },
    { "sigaltstack",                (uintptr_t)shim_sigaltstack },
    { "sigsuspend",                 (uintptr_t)shim_sigsuspend },
    { "pthread_sigmask",            (uintptr_t)shim_pthread_sigmask },
    { "bsd_signal",                 (uintptr_t)shim_bsd_signal },
    { "setjmp",                     (uintptr_t)bionic_setjmp },
    { "longjmp",                    (uintptr_t)bionic_longjmp },
    { "sigsetjmp",                  (uintptr_t)bionic_sigsetjmp },
    { "siglongjmp",                 (uintptr_t)bionic_siglongjmp },

    /* pthreads & semaphores */
    { "pthread_mutexattr_init",     (uintptr_t)shim_pthread_mutexattr_init },
    { "pthread_mutexattr_destroy",  (uintptr_t)shim_pthread_mutexattr_destroy },
    { "pthread_mutexattr_settype",  (uintptr_t)shim_pthread_mutexattr_settype },
    { "pthread_mutex_init",         (uintptr_t)shim_pthread_mutex_init },
    { "pthread_mutex_destroy",      (uintptr_t)shim_pthread_mutex_destroy },
    { "pthread_mutex_lock",         (uintptr_t)shim_pthread_mutex_lock },
    { "pthread_mutex_trylock",      (uintptr_t)shim_pthread_mutex_trylock },
    { "pthread_mutex_unlock",       (uintptr_t)shim_pthread_mutex_unlock },
    { "pthread_cond_init",          (uintptr_t)shim_pthread_cond_init },
    { "pthread_cond_destroy",       (uintptr_t)shim_pthread_cond_destroy },
    { "pthread_cond_wait",          (uintptr_t)shim_pthread_cond_wait },
    { "pthread_cond_timedwait",     (uintptr_t)shim_pthread_cond_timedwait },
    { "pthread_cond_timedwait_relative_np", (uintptr_t)shim_pthread_cond_timedwait_relative_np },
    { "pthread_cond_signal",        (uintptr_t)shim_pthread_cond_signal },
    { "pthread_cond_broadcast",     (uintptr_t)shim_pthread_cond_broadcast },
    { "pthread_rwlock_init",        (uintptr_t)shim_pthread_rwlock_init },
    { "pthread_rwlock_destroy",     (uintptr_t)shim_pthread_rwlock_destroy },
    { "pthread_rwlock_rdlock",      (uintptr_t)shim_pthread_rwlock_rdlock },
    { "pthread_rwlock_wrlock",      (uintptr_t)shim_pthread_rwlock_wrlock },
    { "pthread_rwlock_unlock",      (uintptr_t)shim_pthread_rwlock_unlock },
    { "pthread_attr_init",          (uintptr_t)shim_pthread_attr_init },
    { "pthread_attr_destroy",       (uintptr_t)shim_pthread_attr_destroy },
    { "pthread_attr_getdetachstate",(uintptr_t)shim_pthread_attr_getdetachstate },
    { "pthread_attr_setdetachstate",(uintptr_t)shim_pthread_attr_setdetachstate },
    { "pthread_attr_getstack",      (uintptr_t)shim_pthread_attr_getstack },
    { "pthread_attr_setstacksize",  (uintptr_t)shim_pthread_attr_setstacksize },
    { "pthread_getattr_np",         (uintptr_t)shim_pthread_getattr_np },
    { "__pthread_cleanup_push",     (uintptr_t)shim_pthread_cleanup_push },
    { "__pthread_cleanup_pop",      (uintptr_t)shim_pthread_cleanup_pop },
    { "pthread_exit",               (uintptr_t)shim_pthread_exit },
    { "pthread_create",             (uintptr_t)shim_pthread_create },
    { "pthread_once",               (uintptr_t)shim_pthread_once },
    { "sem_init",                   (uintptr_t)shim_sem_init },
    { "sem_destroy",                (uintptr_t)shim_sem_destroy },
    { "sem_wait",                   (uintptr_t)shim_sem_wait },
    { "sem_trywait",                (uintptr_t)shim_sem_trywait },
    { "sem_timedwait",              (uintptr_t)shim_sem_timedwait },
    { "sem_post",                   (uintptr_t)shim_sem_post },
    { "sem_getvalue",               (uintptr_t)shim_sem_getvalue },
    { "sem_open",                   (uintptr_t)shim_sem_open },

    /* Dynamic linker & process */
    { "dlopen",                     (uintptr_t)shim_dlopen },
    { "dlsym",                      (uintptr_t)shim_dlsym },
    { "dlclose",                    (uintptr_t)shim_dlclose },
    { "dlerror",                    (uintptr_t)shim_dlerror },
    { "dladdr",                     (uintptr_t)shim_dladdr },
    { "gettid",                     (uintptr_t)shim_gettid },
    { "syscall",                    (uintptr_t)shim_syscall },
    { "prctl",                      (uintptr_t)shim_prctl },
    { "ptrace",                     (uintptr_t)shim_ptrace },
    { "fork",                       (uintptr_t)shim_fork },
    { "execl",                      (uintptr_t)shim_execl },
    { "qsort",                      (uintptr_t)shim_qsort },
    { "mkdtemp",                    (uintptr_t)shim_mkdtemp },
    { "sysconf",                    (uintptr_t)shim_sysconf },
};

const so_default_dynlib *bionic_get_shims(int *out_bytes) {
    if (out_bytes) *out_bytes = (int)sizeof(g_bionic_shims);
    return g_bionic_shims;
}
