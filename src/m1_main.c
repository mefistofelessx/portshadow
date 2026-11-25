/* m1_main.c -- Shadow Fight 2 loader, milestone M1 ("shims + init_array + JNI_OnLoad").
 *
 * Carrega libmain.so, libunity.so e libil2cpp.so, resolve todos os simbolos
 * usando os shims Bionic->glibc, thunks softfp->hardfp e stubs NDK/EGL,
 * executa os construtores (.init_array) e chama JNI_OnLoad de cada modulo.
 */
#define _GNU_SOURCE

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <ucontext.h>
#include <unistd.h>

#include "bionic_shims.h"
#include "fake_jni.h"
#include "ndk_stubs.h"
#include "softfp_thunks.h"
#include "so_util.h"

typedef int (*jni_onload_fn)(void *vm, void *reserved);

static so_module g_mods[3];
static const char *g_mod_names[3] = { "libmain.so", "libunity.so", "libil2cpp.so" };
static int g_mods_loaded = 0;

static void crash_handler(int sig, siginfo_t *si, void *ucv) {
    ucontext_t *uc = (ucontext_t *)ucv;
    uintptr_t pc = 0, lr = 0, sp = 0;
#ifdef __arm__
    if (uc) {
        pc = (uintptr_t)uc->uc_mcontext.arm_pc;
        lr = (uintptr_t)uc->uc_mcontext.arm_lr;
        sp = (uintptr_t)uc->uc_mcontext.arm_sp;
    }
#endif
    fprintf(stderr, "\n[CRASH] sinal=%d addr=%p pc=0x%08lx lr=0x%08lx sp=0x%08lx\n",
            sig, si ? si->si_addr : NULL,
            (unsigned long)pc, (unsigned long)lr, (unsigned long)sp);
    for (int i = 0; i < g_mods_loaded; i++) {
        so_module *m = &g_mods[i];
        if (pc >= m->map_base && pc < m->map_base + m->map_size) {
            fprintf(stderr, "[CRASH] PC dentro de %s + 0x%08lx\n",
                    g_mod_names[i], (unsigned long)(pc - m->load_bias));
        }
        if (lr >= m->map_base && lr < m->map_base + m->map_size) {
            fprintf(stderr, "[CRASH] LR dentro de %s + 0x%08lx\n",
                    g_mod_names[i], (unsigned long)(lr - m->load_bias));
        }
    }
    fflush(stderr);
    _exit(128 + sig);
}

static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

static so_default_dynlib *build_combined_table(int *out_bytes) {
    int b1 = 0, b2 = 0, b3 = 0;
    const so_default_dynlib *t1 = softfp_get_thunks(&b1);
    const so_default_dynlib *t2 = bionic_get_shims(&b2);
    const so_default_dynlib *t3 = ndk_get_stubs(&b3);

    bionic_shims_set_extra_tables(t3, b3, NULL, 0);

    int total = b1 + b2 + b3;
    uint8_t *buf = malloc((size_t)total);
    if (!buf) return NULL;
    memcpy(buf, t1, (size_t)b1);
    memcpy(buf + b1, t2, (size_t)b2);
    memcpy(buf + b1 + b2, t3, (size_t)b3);
    if (out_bytes) *out_bytes = total;
    return (so_default_dynlib *)buf;
}

int m1_run(const char *dir) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    if (!dir) {
        fprintf(stderr, "uso: sf2loader --m1 <pasta com libmain.so libunity.so libil2cpp.so>\n");
        return 2;
    }

    struct utsname u;
    uname(&u);
    printf("sf2loader M1\n%s %s %s\nPID %d\n", u.sysname, u.release, u.machine, getpid());

    bionic_shims_init();

    const char *pre[] = { "libgcc_s.so.1", "libz.so.1" };
    for (unsigned i = 0; i < 2; i++) {
        if (!dlopen(pre[i], RTLD_NOW | RTLD_GLOBAL))
            printf("AVISO: nao abriu %s: %s\n", pre[i], dlerror());
    }

    int table_bytes = 0;
    so_default_dynlib *table = build_combined_table(&table_bytes);
    if (!table) {
        fprintf(stderr, "erro: sem memoria para tabela dynlib\n");
        return 1;
    }
    printf("Tabela dynlib: %d entradas\n", table_bytes / (int)sizeof(so_default_dynlib));

    install_crash_handler();

    printf("\n=== 1) Carregando e relocalizando modulos ===\n");
    for (int i = 0; i < 3; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, g_mod_names[i]);
        printf("-> %s\n", path);
        if (so_load(&g_mods[i], path) != 0) {
            fprintf(stderr, "ERRO ao carregar %s\n", path);
            free(table);
            return 1;
        }
        if (!g_mods[i].soname)
            g_mods[i].soname = strdup(g_mod_names[i]);
        so_relocate(&g_mods[i]);
        g_mods_loaded = i + 1;
        printf("   base=0x%08lx  bias=0x%08lx  exidx=0x%08lx (%lu)\n",
               (unsigned long)g_mods[i].map_base,
               (unsigned long)g_mods[i].load_bias,
               (unsigned long)g_mods[i].exidx_base,
               (unsigned long)(g_mods[i].exidx_size / 8));
    }

    printf("\n=== 2) Resolvendo simbolos ===\n");
    int total_unresolved = 0;
    for (int i = 0; i < 3; i++) {
        int unres = so_resolve(&g_mods[i], table, table_bytes, 0);
        so_flush_caches(&g_mods[i]);
        printf("   %s: nao resolvidos = %d\n", g_mod_names[i], unres);
        total_unresolved += unres;
    }

    if (total_unresolved > 0) {
        fprintf(stderr, "M1 ABORTADO: %d simbolos nao resolvidos\n", total_unresolved);
        free(table);
        return 1;
    }

    printf("\n=== 3) Executando .init_array ===\n");
    for (int i = 0; i < 3; i++) {
        printf("   init_array %s (%d funcoes)...\n", g_mod_names[i], g_mods[i].num_init_array);
        so_initialize(&g_mods[i]);
        printf("   init_array %s OK\n", g_mod_names[i]);
    }

    printf("\n=== 4) Chamando JNI_OnLoad ===\n");
    void *vm = fake_jni_get_vm();
    for (int i = 0; i < 3; i++) {
        uintptr_t fn_addr = so_symbol(&g_mods[i], "JNI_OnLoad");
        if (!fn_addr) {
            printf("   %s: JNI_OnLoad ausente\n", g_mod_names[i]);
            continue;
        }
        jni_onload_fn fn = (jni_onload_fn)fn_addr;
        printf("   chamando %s JNI_OnLoad (%p)...\n", g_mod_names[i], (void *)fn_addr);
        int ver = fn(vm, NULL);
        printf("   %s JNI_OnLoad retornou 0x%08x\n", g_mod_names[i], (unsigned)ver);
    }

    free(table);
    printf("\nM1 OK\n");
    return 0;
}
