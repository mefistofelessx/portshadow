/* m0_main.c -- Shadow Fight 2 loader, milestone M0 ("diagnostico").
 *
 * NAO executa codigo do jogo. Apenas:
 *   1. testa se as funcoes de tempo da glibc deste firmware funcionam (ou dao SIGSEGV/SIGILL);
 *   2. checa quais simbolos de unwind/aeabi/glibc existem neste sistema;
 *   3. carrega e relocaliza libmain.so, libunity.so e libil2cpp.so na memoria (sem rodar init);
 *   4. mede a RAM usada e lista cada simbolo importado e se o sistema o resolve.
 *
 * Uso: sf2loader --m0 <pasta com os .so>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <elf.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include "so_util.h"

/* ---------- utilidades ---------- */

static long vm_kb(const char *key) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long v = -1;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl)) { v = atol(line + kl); break; }
    fclose(f);
    return v;
}

static sigjmp_buf g_jb;
static volatile sig_atomic_t g_sig;
static void on_fault(int s) { g_sig = s; siglongjmp(g_jb, 1); }

/* roda fn sob guarda; devolve 0 = ok, senao o numero do sinal */
static int guarded(void (*fn)(void)) {
    struct sigaction sa, o1, o2, o3;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &o1);
    sigaction(SIGILL,  &sa, &o2);
    sigaction(SIGBUS,  &sa, &o3);
    g_sig = 0;
    if (sigsetjmp(g_jb, 1) == 0) fn();
    int r = g_sig;
    sigaction(SIGSEGV, &o1, NULL);
    sigaction(SIGILL,  &o2, NULL);
    sigaction(SIGBUS,  &o3, NULL);
    return r;
}

/* ---------- 1) funcoes de tempo ---------- */

static void t_mono(void) { struct timespec t; int r = clock_gettime(CLOCK_MONOTONIC, &t);
    printf("   clock_gettime(MONOTONIC) r=%d sec=%ld nsec=%ld\n", r, (long)t.tv_sec, (long)t.tv_nsec); }
static void t_real(void) { struct timespec t; int r = clock_gettime(CLOCK_REALTIME, &t);
    printf("   clock_gettime(REALTIME)  r=%d sec=%lld\n", r, (long long)t.tv_sec); }
static void t_cpu(void)  { struct timespec t; int r = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    printf("   clock_gettime(PROC_CPU)  r=%d nsec=%ld\n", r, (long)t.tv_nsec); }
static void t_gtod(void) { struct timeval tv; int r = gettimeofday(&tv, NULL);
    printf("   gettimeofday             r=%d sec=%lld\n", r, (long long)tv.tv_sec); }
static void t_time(void) { time_t t = time(NULL); printf("   time()                   %lld\n", (long long)t); }
static void t_nsl(void)  { struct timespec d = {0, 1000000}; int r = nanosleep(&d, NULL);
    printf("   nanosleep(1ms)           r=%d\n", r); }
static void t_clock(void){ clock_t c = clock(); printf("   clock()                  %ld\n", (long)c); }

static void probe_time(void) {
    printf("\n=== 1) Funcoes de tempo (glibc deste firmware) ===\n");
    struct { const char *n; void (*f)(void); } T[] = {
        {"clock_gettime MONOTONIC", t_mono}, {"clock_gettime REALTIME", t_real},
        {"clock_gettime PROC_CPU", t_cpu},   {"gettimeofday", t_gtod},
        {"time", t_time}, {"nanosleep", t_nsl}, {"clock", t_clock},
    };
    for (unsigned i = 0; i < sizeof T / sizeof *T; i++) {
        int s = guarded(T[i].f);
        if (s) printf("   >>> %s: FALHOU com sinal %d (%s)\n", T[i].n, s,
                      s == SIGSEGV ? "SIGSEGV" : s == SIGILL ? "SIGILL" : "SIGBUS");
        fflush(stdout);
    }
}

/* ---------- 2) simbolos do sistema ---------- */

static void probe_syms(void) {
    printf("\n=== 2) Simbolos do sistema (unwind / aeabi / glibc novos) ===\n");
    const char *N[] = {
        "__gnu_Unwind_Find_exidx", "dl_unwind_find_exidx", "dl_iterate_phdr",
        "_Unwind_RaiseException", "_Unwind_Resume", "_Unwind_VRS_Get", "_Unwind_VRS_Set",
        "__gnu_unwind_frame", "__aeabi_unwind_cpp_pr0", "__aeabi_unwind_cpp_pr1",
        "__aeabi_memcpy", "__aeabi_memcpy4", "__aeabi_memset", "__aeabi_memclr",
        "strlcpy", "gettid", "__errno_location", "readdir64", "fstat64", "stat64",
        "__clock_gettime64", "__cxa_atexit", "pthread_cond_timedwait", "sigsetjmp",
        "mkdtemp", "memalign", "bsd_signal", "sysv_signal", "__pthread_cleanup_push",
        "_pthread_cleanup_push", "inflate", "SDL_Init", "eglGetDisplay", "glClear",
    };
    for (unsigned i = 0; i < sizeof N / sizeof *N; i++) {
        void *p = dlsym(RTLD_DEFAULT, N[i]);
        Dl_info di;
        if (p && dladdr(p, &di) && di.dli_fname)
            printf("   %-28s SIM   (%s)\n", N[i], di.dli_fname);
        else if (p) printf("   %-28s SIM\n", N[i]);
        else        printf("   %-28s nao\n", N[i]);
    }
    /* libs de 32 bits que o jogo vai precisar */
    const char *L[] = { "libEGL.so.1", "libGLESv2.so.2", "libSDL2-2.0.so.0", "libasound.so.2", "libz.so.1", "libgcc_s.so.1" };
    printf("   -- bibliotecas (dlopen):\n");
    for (unsigned i = 0; i < sizeof L / sizeof *L; i++) {
        void *h = dlopen(L[i], RTLD_LAZY);
        printf("   %-22s %s\n", L[i], h ? "SIM" : dlerror());
        if (h) dlclose(h);
    }
}

/* ---------- 3/4) modulos e imports ---------- */

#define MAX_IMP 1024
static char *imp_name[MAX_IMP];
static unsigned char imp_libs[MAX_IMP];   /* bitmask: 1=main 2=unity 4=il2cpp */
static int imp_n;

static void add_import(const char *n, int bit) {
    for (int i = 0; i < imp_n; i++)
        if (!strcmp(imp_name[i], n)) { imp_libs[i] |= bit; return; }
    if (imp_n < MAX_IMP) { imp_name[imp_n] = strdup(n); imp_libs[imp_n++] = bit; }
}

static void collect_imports(so_module *m, int bit) {
    int count = m->num_reldyn + m->num_relplt;
    for (int i = 0; i < count; i++) {
        Elf32_Rel *r = i < m->num_reldyn ? &m->reldyn[i] : &m->relplt[i - m->num_reldyn];
        int type = ELF32_R_TYPE(r->r_info);
        if (type != R_ARM_ABS32 && type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT) continue;
        Elf32_Sym *s = &m->dynsym[ELF32_R_SYM(r->r_info)];
        if (s->st_shndx != SHN_UNDEF) continue;
        const char *n = m->dynstr + s->st_name;
        const char *at = strchr(n, '@');
        if (at) { char tmp[256]; size_t l = at - n; if (l > 255) l = 255; memcpy(tmp, n, l); tmp[l] = 0; add_import(tmp, bit); }
        else add_import(n, bit);
    }
}

static int load_one(const char *dir, const char *file, int bit) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    so_module *m = calloc(1, sizeof *m);
    long rss0 = vm_kb("VmRSS:");
    printf("\n--- %s\n", path);
    if (so_load(m, path) != 0) { printf("   >>> ERRO ao carregar (arquivo existe? e um ELF 32 bits?)\n"); return -1; }
    so_relocate(m);
    long rss1 = vm_kb("VmRSS:");
    int needed = 0;
    for (int i = 0; i < m->num_dynamic; i++) if (m->dynamic[i].d_tag == DT_NEEDED) needed++;
    printf("   base=0x%08lx  mapeado=%.1f MB  text=%.1f MB  dados=%.1f MB\n",
           (unsigned long)m->map_base, m->map_size / 1048576.0,
           m->text_size / 1048576.0, m->data_size / 1048576.0);
    printf("   dynsym=%d  rel.dyn=%d  rel.plt=%d  init_array=%d  exidx=%s (%lu entradas)\n",
           m->num_dynsym, m->num_reldyn, m->num_relplt, m->num_init_array,
           m->exidx_base ? "SIM" : "NAO", (unsigned long)(m->exidx_size / 8));
    printf("   RAM (RSS) +%ld KB\n", rss1 - rss0);
    printf("   DT_NEEDED (%d):", needed);
    for (int i = 0; i < m->num_dynamic; i++)
        if (m->dynamic[i].d_tag == DT_NEEDED) printf(" %s", m->dynstr + m->dynamic[i].d_un.d_val);
    printf("\n");
    uintptr_t jni = so_symbol(m, "JNI_OnLoad");
    printf("   JNI_OnLoad: %s\n", jni ? "presente" : "ausente");
    collect_imports(m, bit);
    return 0;
}

/* simbolos que existem na glibc mas tem ABI/layout diferente do bionic (precisam de shim) */
static int needs_shim(const char *n) {
    static const char *exact[] = {
        "stat","lstat","fstat","readdir","opendir","closedir","sigaction","sigaltstack","setjmp","longjmp",
        "sigsetjmp","siglongjmp","__end__","fopen","fclose","fprintf","vfprintf","fputs","fputc","fwrite","fread",
        "fgets","fflush","fseek","fseeko","ftell","ftello","feof","ferror","clearerr","fscanf","popen","pclose",
        "dlopen","dlsym","dlclose","dlerror","dladdr","gettid","getpwuid","environ","bsd_signal",
        "eglGetProcAddress","qsort","lseek64","nftw","mkdtemp","syscall","prctl","ptrace","fork","execl",
        "__aeabi_memcpy","__aeabi_memcpy4","__aeabi_memcpy8","__aeabi_memmove","__aeabi_memmove4","__aeabi_memmove8",
        "__aeabi_memset","__aeabi_memset4","__aeabi_memset8","__aeabi_memclr","__aeabi_memclr4","__aeabi_memclr8",
        "__sF","__errno","__stack_chk_guard","__gnu_Unwind_Find_exidx","__pthread_cleanup_push","__pthread_cleanup_pop",
        "pthread_cond_timedwait_relative_np","__assert2",
    };
    for (unsigned i = 0; i < sizeof exact / sizeof *exact; i++) if (!strcmp(n, exact[i])) return 1;
    if (!strncmp(n, "pthread_", 8) || !strncmp(n, "sem_", 4)) return 1;       /* tamanhos de mutex/cond/sem diferem */
    if (!strncmp(n, "__system_property", 17) || !strncmp(n, "__android", 9)) return 1;
    return 0;
}
static int is_android_api(const char *n) {
    return !strncmp(n, "ALooper_", 8) || !strncmp(n, "ANativeWindow_", 14) || !strncmp(n, "ASensor", 7) ||
           !strncmp(n, "AAsset", 6) || !strncmp(n, "AInput", 6) || !strncmp(n, "AConfiguration", 14);
}
static int is_float_abi(const char *n) {   /* funcoes libm com float/double: softfp x hardfp */
    static const char *m[] = {"acosf","asinf","atan2f","atanf","ceil","ceilf","cosf","exp2f","expf","floor","floorf",
        "fmax","fmaxf","fminf","fmod","fmodf","ldexp","ldexpf","log10f","modf","modff","nearbyintf","pow","powf",
        "remainderf","rintf","round","roundf","scalbn","sinf","sqrtf","tanf","truncf","difftime","strtod","atof"};
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) if (!strcmp(n, m[i])) return 1;
    return 0;
}

static void report_imports(void) {
    printf("\n=== 4) Imports (%d simbolos unicos) ===\n", imp_n);
    int host = 0, miss = 0, shim = 0, andr = 0, fl = 0, gl = 0;
    char *missing[MAX_IMP]; int nm = 0;
    for (int i = 0; i < imp_n; i++) {
        const char *n = imp_name[i];
        if (is_android_api(n)) { andr++; continue; }
        if (!strncmp(n, "egl", 3) || (!strncmp(n, "gl", 2) && n[2] >= 'A' && n[2] <= 'Z')) { gl++; continue; }
        if (is_float_abi(n)) { fl++; continue; }
        if (needs_shim(n)) { shim++; continue; }
        if (dlsym(RTLD_DEFAULT, n)) host++; else missing[nm++] = imp_name[i];
    }
    miss = nm;
    printf("   resolvidos direto pela glibc (sem diferenca de ABI conhecida): %d\n", host);
    printf("   precisam de shim por layout/ABI bionic x glibc:                %d\n", shim);
    printf("   libm com float/double (softfp -> hardfp, precisam de thunk):    %d\n", fl);
    printf("   API Android NDK (ALooper/ANativeWindow/ASensor...):            %d\n", andr);
    printf("   EGL/GLES (ponte para o driver Mali):                           %d\n", gl);
    printf("   NAO encontrados no sistema e sem categoria:                    %d\n", miss);
    if (miss) {
        printf("   >>> faltando:");
        for (int i = 0; i < nm; i++) printf(" %s", missing[i]);
        printf("\n");
    }
}

int m0_run(const char *dir) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!dir) { fprintf(stderr, "uso: sf2loader --m0 <pasta com libmain.so libunity.so libil2cpp.so>\n"); return 2; }

    /* o jogo importa inflate* (libz) e _Unwind_* (libgcc_s): abrir globalmente para o teste enxerga-los */
    {   const char *pre[] = { "libgcc_s.so.1", "libz.so.1" };
        for (unsigned i = 0; i < 2; i++)
            if (!dlopen(pre[i], RTLD_NOW | RTLD_GLOBAL)) printf("AVISO: nao abriu %s: %s\n", pre[i], dlerror()); }

    struct utsname u; uname(&u);
    printf("sf2loader M0\n%s %s %s\nPID %d  VmRSS inicial %ld KB\n", u.sysname, u.release, u.machine, getpid(), vm_kb("VmRSS:"));
    {   FILE *f = fopen("/proc/meminfo", "r"); char l[128];
        for (int i = 0; f && i < 3 && fgets(l, sizeof l, f); i++) printf("%s", l);
        if (f) fclose(f); }

    probe_time();
    probe_syms();

    printf("\n=== 3) Modulos do jogo ===\n");
    long rss0 = vm_kb("VmRSS:");
    int bad = 0;
    bad += load_one(dir, "libmain.so",   1) != 0;
    bad += load_one(dir, "libunity.so",  2) != 0;
    bad += load_one(dir, "libil2cpp.so", 4) != 0;
    printf("\n   RAM total usada pelos 3 modulos: +%ld KB (%.1f MB)  | VmSize agora %ld KB\n",
           vm_kb("VmRSS:") - rss0, (vm_kb("VmRSS:") - rss0) / 1024.0, vm_kb("VmSize:"));

    report_imports();
    printf("\nM0 %s\n", bad ? "TERMINOU COM ERROS" : "OK");
    return bad ? 1 : 0;
}
