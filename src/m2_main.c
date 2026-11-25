/* m2_main.c -- Shadow Fight 2 loader, milestone M2 ("janela SDL2 + loop de render Unity").
 *
 * Abre uma janela SDL2 com contexto OpenGL ES 2.0 via Mali EGL, configura o JNI
 * com o caminho real do APK, carrega e inicializa os 3 modulos Unity/IL2CPP,
 * chama initJni + nativeRecreateGfxState e entra no loop de render.
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
#include "sdl_egl.h"
#include "softfp_thunks.h"
#include "so_util.h"

typedef int (*jni_onload_fn)(void *vm, void *reserved);

/* Convecoes JNI: todo metodo nativo registrado via RegisterNatives recebe:
 * r0 = JNIEnv *env
 * r1 = jobject thiz (ou jclass clazz)
 * r2, r3... = parametros
 */
typedef void (*jni_initjni_fn)(void *env, void *clazz_or_thiz, void *context);
typedef void (*jni_gfx_fn)(void *env, void *clazz_or_thiz, int mode, void *surface);
typedef void (*jni_resume_fn)(void *env, void *clazz_or_thiz);
typedef void (*jni_focus_fn)(void *env, void *clazz_or_thiz, int focus);
typedef int  (*jni_render_fn)(void *env, void *clazz_or_thiz);

static so_module g_mods[3];
static const char *g_mod_names[3] = { "libmain.so", "libunity.so", "libil2cpp.so" };
static int g_mods_loaded = 0;
static volatile int        g_cur_mod      = -1;
static volatile int        g_cur_init_idx = -1;
static volatile uintptr_t  g_cur_init_fn  = 0;

/* ------------------------------------------------------------------ */
/* Utilitario: imprime dono do endereco                                */
/* ------------------------------------------------------------------ */
static void print_addr_owner(const char *tag, uintptr_t v) {
    for (int i = 0; i < g_mods_loaded; i++) {
        so_module *m = &g_mods[i];
        if (v >= m->map_base && v < m->map_base + m->map_size) {
            fprintf(stderr, "   %s = 0x%08lx (%s + 0x%08lx)\n",
                    tag, (unsigned long)v, g_mod_names[i],
                    (unsigned long)(v - m->load_bias));
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Handler de crash com dump de registradores e stack                  */
/* ------------------------------------------------------------------ */
static void dump_regs(int sig, siginfo_t *si, void *ucv) {
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

    if (g_cur_mod >= 0 && g_cur_mod < 3) {
        fprintf(stderr, "[CRASH] etapa: %s init_array[%d] = 0x%08lx (off +0x%08lx)\n",
                g_mod_names[g_cur_mod], g_cur_init_idx,
                (unsigned long)g_cur_init_fn,
                (unsigned long)(g_cur_init_fn - g_mods[g_cur_mod].load_bias));
    }

    print_addr_owner("PC", pc);
    print_addr_owner("LR", lr);

#ifdef __arm__
    if (uc) {
        fprintf(stderr,
                "   r0=%08lx r1=%08lx r2=%08lx r3=%08lx\n"
                "   r4=%08lx r5=%08lx r6=%08lx r7=%08lx\n"
                "   r8=%08lx r9=%08lx r10=%08lx fp=%08lx ip=%08lx cpsr=%08lx\n",
                (unsigned long)uc->uc_mcontext.arm_r0,  (unsigned long)uc->uc_mcontext.arm_r1,
                (unsigned long)uc->uc_mcontext.arm_r2,  (unsigned long)uc->uc_mcontext.arm_r3,
                (unsigned long)uc->uc_mcontext.arm_r4,  (unsigned long)uc->uc_mcontext.arm_r5,
                (unsigned long)uc->uc_mcontext.arm_r6,  (unsigned long)uc->uc_mcontext.arm_r7,
                (unsigned long)uc->uc_mcontext.arm_r8,  (unsigned long)uc->uc_mcontext.arm_r9,
                (unsigned long)uc->uc_mcontext.arm_r10, (unsigned long)uc->uc_mcontext.arm_fp,
                (unsigned long)uc->uc_mcontext.arm_ip,  (unsigned long)uc->uc_mcontext.arm_cpsr);
        print_addr_owner("r0", (uintptr_t)uc->uc_mcontext.arm_r0);
        print_addr_owner("r4", (uintptr_t)uc->uc_mcontext.arm_r4);
        print_addr_owner("r6", (uintptr_t)uc->uc_mcontext.arm_r6);
        if (sp >= 0x10000) {
            const uintptr_t *stk = (const uintptr_t *)sp;
            fprintf(stderr, "   stack[0..15]:\n");
            for (int k = 0; k < 16; k++) {
                char tag[16];
                snprintf(tag, sizeof(tag), "sp[%2d]", k);
                fprintf(stderr, "     sp[%2d] = 0x%08lx\n", k, (unsigned long)stk[k]);
                print_addr_owner(tag, stk[k]);
            }
        }
    }
#endif
    fflush(stderr);
}

static void crash_handler(int sig, siginfo_t *si, void *ucv) {
    dump_regs(sig, si, ucv);
    _exit(128 + sig);
}

static void hook_dump(void *ucv) { dump_regs(0, NULL, ucv); }

static void install_crash_handler(void) {
    bionic_crash_dump_hook = hook_dump;
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

/* ------------------------------------------------------------------ */
/* Monta tabela dynlib combinada: softfp + shims + SDL/EGL             */
/* ------------------------------------------------------------------ */
static so_default_dynlib *build_combined_table(int *out_bytes) {
    int b1 = 0, b2 = 0, b3 = 0;
    const so_default_dynlib *t1 = softfp_get_thunks(&b1);
    const so_default_dynlib *t2 = bionic_get_shims(&b2);
    const so_default_dynlib *t3 = sdl_egl_get_dynlib(&b3);

    bionic_shims_set_extra_tables(t3, b3, NULL, 0);

    int total = b1 + b2 + b3;
    uint8_t *buf = malloc((size_t)total);
    if (!buf) return NULL;
    memcpy(buf,           t1, (size_t)b1);
    memcpy(buf + b1,      t2, (size_t)b2);
    memcpy(buf + b1 + b2, t3, (size_t)b3);
    if (out_bytes) *out_bytes = total;
    return (so_default_dynlib *)buf;
}

/* ------------------------------------------------------------------ */
/* Carrega .so extras (Firebase/Crashlytics) se presentes na pasta     */
/* ------------------------------------------------------------------ */
static void try_load_extra_so(const char *libs_dir) {
    static const char *extras[] = {
        "libcrashlytics.so",
        "libcrashlytics-common.so",
        "libcrashlytics-handler.so",
        "libcrashlytics-trampoline.so",
        "libFirebaseCppApp-9_3_0.so",
        "libFirebaseCppCrashlytics.so",
        "libFirebaseCppMessaging.so",
    };
    for (unsigned i = 0; i < sizeof(extras)/sizeof(extras[0]); i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", libs_dir, extras[i]);
        if (access(path, F_OK) == 0) {
            void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
            printf("   dlopen(%s) -> %s\n", extras[i], h ? "OK" : dlerror());
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point do M2                                                    */
/* ------------------------------------------------------------------ */
int m2_run(const char *libs_dir, const char *apk_path, const char *save_dir) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (!libs_dir || !apk_path || !save_dir) {
        fprintf(stderr,
                "uso: sf2loader --m2 <pasta/libs> <caminho/game.apk> <pasta/save>\n");
        return 2;
    }

    struct utsname u;
    uname(&u);
    printf("sf2loader M2\n%s %s %s\nPID %d\n",
           u.sysname, u.release, u.machine, getpid());
    printf("libs_dir  = %s\n", libs_dir);
    printf("apk_path  = %s\n", apk_path);
    printf("save_dir  = %s\n", save_dir);

    /* 1) Inicializa shims Bionic->glibc e pre-carrega deps do sistema */
    bionic_shims_init();
    const char *pre[] = { "libgcc_s.so.1", "libz.so.1" };
    for (unsigned i = 0; i < 2; i++) {
        if (!dlopen(pre[i], RTLD_NOW | RTLD_GLOBAL))
            printf("AVISO: nao abriu %s: %s\n", pre[i], dlerror());
    }

    /* 2) Abre janela SDL2 + contexto OpenGL ES 2.0 via Mali EGL */
    printf("\n=== 1) Inicializando SDL2 + Mali EGL ===\n");
    if (sdl_egl_init() != 0) {
        fprintf(stderr, "ERRO: sdl_egl_init falhou\n");
        return 1;
    }
    int win_w = 0, win_h = 0;
    sdl_egl_get_size(&win_w, &win_h);
    printf("   Janela: %dx%d\n", win_w, win_h);

    /* 3) Monta tabela de simbolos combinada */
    printf("\n=== 2) Montando tabela dynlib ===\n");
    int table_bytes = 0;
    so_default_dynlib *table = build_combined_table(&table_bytes);
    if (!table) {
        fprintf(stderr, "erro: sem memoria para tabela dynlib\n");
        sdl_egl_shutdown();
        return 1;
    }
    printf("   Tabela dynlib: %d entradas\n",
           table_bytes / (int)sizeof(so_default_dynlib));

    /* 4) Configura JNI com caminhos reais e tamanho da janela */
    fake_jni_set_config(apk_path, save_dir, win_w, win_h);
    fake_jni_set_libs_dir(libs_dir);
    printf("   JNI configurado (apk=%s w=%d h=%d)\n", apk_path, win_w, win_h);

    /* 5) Instala handler de crash antes de tocar nos .so */
    install_crash_handler();

    /* 6) Pre-carrega .so extras (Firebase/Crashlytics) se disponiveis */
    printf("\n=== 3) Carregando .so extras ===\n");
    try_load_extra_so(libs_dir);

    /* 7) Carrega, relocaliza e resolve os 3 modulos principais */
    printf("\n=== 4) Carregando modulos principais ===\n");
    for (int i = 0; i < 3; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", libs_dir, g_mod_names[i]);
        printf("-> %s\n", path);
        if (so_load(&g_mods[i], path) != 0) {
            fprintf(stderr, "ERRO ao carregar %s\n", path);
            free(table);
            sdl_egl_shutdown();
            return 1;
        }
        if (!g_mods[i].soname)
            g_mods[i].soname = strdup(g_mod_names[i]);
        so_relocate(&g_mods[i]);
        g_mods_loaded = i + 1;
        printf("   base=0x%08lx  bias=0x%08lx\n",
               (unsigned long)g_mods[i].map_base,
               (unsigned long)g_mods[i].load_bias);
    }

    printf("\n=== 5) Resolvendo simbolos ===\n");
    int total_unresolved = 0;
    for (int i = 0; i < 3; i++) {
        int unres = so_resolve(&g_mods[i], table, table_bytes, 0);
        so_flush_caches(&g_mods[i]);
        printf("   %s: nao resolvidos = %d\n", g_mod_names[i], unres);
        total_unresolved += unres;
    }
    if (total_unresolved > 0) {
        fprintf(stderr, "M2 ABORTADO: %d simbolos nao resolvidos\n", total_unresolved);
        free(table);
        sdl_egl_shutdown();
        return 1;
    }

    /* 8) Executa construtores .init_array */
    printf("\n=== 6) Executando .init_array ===\n");
    for (int i = 0; i < 3; i++) {
        printf("   init_array %s (%d funcoes)...\n",
               g_mod_names[i], g_mods[i].num_init_array);
        g_cur_mod      = i;
        g_cur_init_idx = -1;
        g_cur_init_fn  = 0;
        so_initialize_tracked(&g_mods[i], &g_cur_init_idx, &g_cur_init_fn);
        printf("   init_array %s OK\n", g_mod_names[i]);
    }
    g_cur_mod = -1;

    /* 9) Chama JNI_OnLoad nos 3 modulos */
    printf("\n=== 7) Chamando JNI_OnLoad ===\n");
    void *vm = fake_jni_get_vm();
    for (int i = 0; i < 3; i++) {
        uintptr_t fn_addr = so_symbol(&g_mods[i], "JNI_OnLoad");
        if (!fn_addr) {
            printf("   %s: JNI_OnLoad ausente\n", g_mod_names[i]);
            continue;
        }
        jni_onload_fn fn = (jni_onload_fn)fn_addr;
        printf("   chamando %s JNI_OnLoad (%p)...\n",
               g_mod_names[i], (void *)fn_addr);
        int ver = fn(vm, NULL);
        printf("   %s JNI_OnLoad retornou 0x%08x\n",
               g_mod_names[i], (unsigned)ver);
    }

    void *env = fake_jni_get_env();
    void *player_obj = fake_jni_make_object("com/unity3d/player/UnityPlayer");

    /* 10) Chama initJni (inicializacao principal do UnityPlayer) */
    printf("\n=== 8) Chamando initJni ===\n");
    void *initjni_ptr = fake_jni_find_native("com/unity3d/player/UnityPlayer", "initJni");
    if (!initjni_ptr)
        initjni_ptr = fake_jni_find_native(NULL, "initJni");

    if (initjni_ptr) {
        printf("   initJni encontrado em %p\n", initjni_ptr);
        void *ctx_obj = fake_jni_make_object("android/content/Context");
        jni_initjni_fn fn_initjni = (jni_initjni_fn)initjni_ptr;
        fn_initjni(env, player_obj, ctx_obj);
        printf("   initJni OK\n");
    } else {
        printf("   AVISO: initJni nao encontrado via RegisterNatives\n");
    }

    /* 11) Chama nativeResume e nativeFocusChanged */
    printf("\n=== 9) Chamando nativeResume + nativeFocusChanged ===\n");
    void *resume_ptr = fake_jni_find_native(NULL, "nativeResume");
    if (resume_ptr) {
        printf("   nativeResume encontrado em %p\n", resume_ptr);
        ((jni_resume_fn)resume_ptr)(env, player_obj);
        printf("   nativeResume OK\n");
    }
    void *focus_ptr = fake_jni_find_native(NULL, "nativeFocusChanged");
    if (focus_ptr) {
        printf("   nativeFocusChanged encontrado em %p\n", focus_ptr);
        ((jni_focus_fn)focus_ptr)(env, player_obj, 1);
        printf("   nativeFocusChanged OK\n");
    }

    /* 12) Chama nativeRecreateGfxState para inicializar superficie de render */
    printf("\n=== 10) Chamando nativeRecreateGfxState ===\n");
    void *gfx_ptr = fake_jni_find_native("com/unity3d/player/UnityPlayer", "nativeRecreateGfxState");
    if (!gfx_ptr)
        gfx_ptr = fake_jni_find_native(NULL, "nativeRecreateGfxState");

    if (gfx_ptr) {
        printf("   nativeRecreateGfxState encontrado em %p\n", gfx_ptr);
        void *surface_obj = fake_jni_make_object("android/view/Surface");
        jni_gfx_fn fn_gfx = (jni_gfx_fn)gfx_ptr;
        fn_gfx(env, player_obj, 0, surface_obj);
        printf("   nativeRecreateGfxState OK\n");
    } else {
        printf("   AVISO: nativeRecreateGfxState nao encontrado\n");
    }

    /* 13) Localiza nativeRender para o loop principal */
    void *render_ptr = fake_jni_find_native("com/unity3d/player/UnityPlayer", "nativeRender");
    if (!render_ptr)
        render_ptr = fake_jni_find_native(NULL, "nativeRender");
    if (render_ptr)
        printf("   nativeRender encontrado em %p\n", render_ptr);
    else
        printf("   AVISO: nativeRender nao encontrado -- loop sem render\n");

    printf("\nM2 INIT OK -- entrando no loop de render\n");
    fflush(stdout);

    /* 14) Loop principal */
    jni_render_fn fn_render = render_ptr ? (jni_render_fn)render_ptr : NULL;
    long frame = 0;
    while (1) {
        if (sdl_egl_poll_events())
            break;  /* Janela fechada ou SDL_QUIT */

        if (fn_render) {
            fn_render(env, player_obj);
        }

        frame++;
        if (frame % 300 == 0)
            printf("   frame %ld\n", frame);
    }

    printf("\nSaindo do loop apos %ld frames\n", frame);
    free(table);
    sdl_egl_shutdown();
    _exit(0);
    return 0;
}
