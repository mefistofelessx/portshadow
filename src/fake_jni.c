/* fake_jni.c -- JavaVM + JNIEnv implementation for M1 and M2 UnityPlayer boot. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fake_jni.h"

#ifdef __arm__
#define SOFTFP __attribute__((pcs("aapcs")))
#else
#define SOFTFP
#endif

enum {
    JTAG_CLASS  = 0x4A434C53,
    JTAG_OBJ    = 0x4A4F424A,
    JTAG_STRING = 0x4A535452,
    JTAG_ARRAY  = 0x4A415252,
    JTAG_MEMID  = 0x4A4D454D,
};

typedef struct {
    uint32_t tag;
    char     cls[128];
    char     extra[256];
} fake_jobj_t;

typedef struct {
    uint32_t tag;
    char     cls[128];
    char     name[96];
    char     sig[128];
} fake_member_t;

typedef struct {
    uint32_t tag;
    char    *str;
    uint16_t *utf16;
    int      len;
} fake_jstring_t;

typedef struct {
    uint32_t tag;
    int      elem_size;
    int      len;
    void    *data;
} fake_jarray_t;

typedef struct {
    char cls[128];
    char name[96];
    char sig[128];
    void *fnPtr;
} reg_native_t;

#define MAX_REG_NATIVES 128
static reg_native_t g_reg_natives[MAX_REG_NATIVES];
static int g_num_reg_natives = 0;


/* ── Log com limite (evita logs gigantes) ──────────────────────────────── */
#define JLOG_MAX_LINES 4000
static volatile int g_jlog_lines = 0;
static void jlog(const char *fmt, ...) {
    int n = __sync_add_and_fetch(&g_jlog_lines, 1);
    if (n > JLOG_MAX_LINES) {
        if (n == JLOG_MAX_LINES + 1)
            printf("   [JNI] ... log JNI suprimido (limite de %d linhas) ...\n", JLOG_MAX_LINES);
        return;
    }
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* Mensagens "nao tratado" aparecem so as 2 primeiras vezes por chave */
static int seen_key(const char *key) {
    static char keys[512][160];
    static int  cnt[512];
    static int  nkeys = 0;
    for (int i = 0; i < nkeys; i++)
        if (strcmp(keys[i], key) == 0) return ++cnt[i];
    if (nkeys < 512) {
        strncpy(keys[nkeys], key, 159);
        cnt[nkeys] = 1;
        return cnt[nkeys++];
    }
    return 1;
}

static char g_libs_dir[512] = "/roms/ports/sf2/libs";
static char g_apk_path[512] = "/roms/ports/sf2/game.apk";
static char g_save_dir[512] = "/roms/ports/sf2/save";
static int  g_disp_w = 720;
static int  g_disp_h = 720;

static void *g_jni_env_table[236];
static void *g_jni_env_ptr = g_jni_env_table;
static void *g_java_vm_table[8];
static void *g_java_vm_ptr = g_java_vm_table;

void fake_jni_set_config(const char *apk_path, const char *save_dir, int w, int h) {
    if (apk_path) { strncpy(g_apk_path, apk_path, sizeof(g_apk_path) - 1); g_apk_path[sizeof(g_apk_path) - 1] = 0; }
    if (save_dir) { strncpy(g_save_dir, save_dir, sizeof(g_save_dir) - 1); g_save_dir[sizeof(g_save_dir) - 1] = 0; }
    if (w > 0) g_disp_w = w;
    if (h > 0) g_disp_h = h;
}

void fake_jni_set_libs_dir(const char *libs_dir) {
    if (libs_dir) { strncpy(g_libs_dir, libs_dir, sizeof(g_libs_dir) - 1); g_libs_dir[sizeof(g_libs_dir) - 1] = 0; }
}

static fake_jobj_t *alloc_jobj(uint32_t tag, const char *cls, const char *extra) {
    fake_jobj_t *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->tag = tag;
    if (cls)   strncpy(o->cls, cls, sizeof(o->cls) - 1);
    if (extra) strncpy(o->extra, extra, sizeof(o->extra) - 1);
    return o;
}

void *fake_jni_make_object(const char *class_name) {
    return alloc_jobj(JTAG_OBJ, class_name ? class_name : "java/lang/Object", NULL);
}

void *fake_jni_find_native(const char *class_name, const char *method_name) {
    for (int i = 0; i < g_num_reg_natives; i++) {
        if ((!class_name || strcmp(g_reg_natives[i].cls, class_name) == 0) &&
            strcmp(g_reg_natives[i].name, method_name) == 0)
            return g_reg_natives[i].fnPtr;
    }
    return NULL;
}

static const char *obj_class_name(void *obj) {
    if (!obj) return "(null)";
    uint32_t tag = *(uint32_t *)obj;
    if (tag == JTAG_CLASS || tag == JTAG_OBJ)
        return ((fake_jobj_t *)obj)->cls;
    if (tag == JTAG_STRING) return "java/lang/String";
    if (tag == JTAG_ARRAY)  return "[Ljava/lang/Object;";
    return "?";
}

/* ── Strings & Arrays ──────────────────────────────────────────────────── */

static void *jni_NewStringUTF(void *env, const char *utf) {
    (void)env;
    fake_jstring_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->tag = JTAG_STRING;
    s->str = strdup(utf ? utf : "");
    s->len = (int)strlen(s->str);
    return s;
}

static int jni_GetStringUTFLength(void *env, void *jstr) {
    (void)env;
    fake_jstring_t *s = (fake_jstring_t *)jstr;
    if (!s || s->tag != JTAG_STRING || !s->str) return 0;
    return (int)strlen(s->str);
}

static const char *jni_GetStringUTFChars(void *env, void *jstr, unsigned char *isCopy) {
    (void)env;
    if (isCopy) *isCopy = 0;
    fake_jstring_t *s = (fake_jstring_t *)jstr;
    return (s && s->tag == JTAG_STRING && s->str) ? s->str : "";
}

static void jni_ReleaseStringUTFChars(void *env, void *jstr, const char *chars) {
    (void)env; (void)jstr; (void)chars;
}

static void *jni_NewString(void *env, const uint16_t *chars, int len) {
    (void)env;
    fake_jstring_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->tag = JTAG_STRING;
    s->len = len;
    s->str = malloc((size_t)len + 1);
    s->utf16 = malloc((size_t)(len + 1) * 2);
    for (int i = 0; i < len; i++) {
        uint16_t c = chars ? chars[i] : 0;
        if (s->utf16) s->utf16[i] = c;
        if (s->str)   s->str[i] = (c < 128) ? (char)c : '?';
    }
    if (s->str)   s->str[len] = 0;
    if (s->utf16) s->utf16[len] = 0;
    return s;
}

static int jni_GetStringLength(void *env, void *jstr) {
    return jni_GetStringUTFLength(env, jstr);
}

static const uint16_t *jni_GetStringChars(void *env, void *jstr, unsigned char *isCopy) {
    (void)env;
    if (isCopy) *isCopy = 0;
    fake_jstring_t *s = (fake_jstring_t *)jstr;
    if (!s || s->tag != JTAG_STRING) return (const uint16_t *)L"";
    if (!s->utf16 && s->str) {
        int n = (int)strlen(s->str);
        s->utf16 = malloc((size_t)(n + 1) * 2);
        for (int i = 0; i <= n; i++) s->utf16[i] = (uint8_t)s->str[i];
    }
    return s->utf16 ? s->utf16 : (const uint16_t *)"\0";
}

static void jni_ReleaseStringChars(void *env, void *jstr, const uint16_t *chars) {
    (void)env; (void)jstr; (void)chars;
}

static void *alloc_jarray(int elem_size, int len) {
    fake_jarray_t *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->tag = JTAG_ARRAY;
    a->elem_size = elem_size;
    a->len = len > 0 ? len : 0;
    a->data = calloc((size_t)(a->len > 0 ? a->len : 1), (size_t)elem_size);
    return a;
}

static int jni_GetArrayLength(void *env, void *arr) {
    (void)env;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    return (a && a->tag == JTAG_ARRAY) ? a->len : 0;
}

static void *jni_NewObjectArray(void *env, int len, void *cls, void *init) {
    (void)env; (void)cls;
    fake_jarray_t *a = alloc_jarray((int)sizeof(void *), len);
    if (a && init) {
        void **p = (void **)a->data;
        for (int i = 0; i < len; i++) p[i] = init;
    }
    return a;
}
static void *jni_GetObjectArrayElement(void *env, void *arr, int idx) {
    (void)env;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    if (!a || a->tag != JTAG_ARRAY || idx < 0 || idx >= a->len) return NULL;
    return ((void **)a->data)[idx];
}
static void jni_SetObjectArrayElement(void *env, void *arr, int idx, void *val) {
    (void)env;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    if (!a || a->tag != JTAG_ARRAY || idx < 0 || idx >= a->len) return;
    ((void **)a->data)[idx] = val;
}
static void *jni_NewByteArray(void *env, int len) { (void)env; return alloc_jarray(1, len); }
static void *jni_NewIntArray(void *env, int len)  { (void)env; return alloc_jarray(4, len); }
static void *jni_GetArrayElements(void *env, void *arr, unsigned char *isCopy) {
    (void)env;
    if (isCopy) *isCopy = 0;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    return (a && a->tag == JTAG_ARRAY) ? a->data : NULL;
}
static void jni_ReleaseArrayElements(void *env, void *arr, void *elems, int mode) {
    (void)env; (void)arr; (void)elems; (void)mode;
}
static void jni_GetArrayRegion(void *env, void *arr, int start, int len, void *buf) {
    (void)env;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    if (!a || a->tag != JTAG_ARRAY || !buf || start < 0 || start + len > a->len) return;
    memcpy(buf, (uint8_t *)a->data + start * a->elem_size, (size_t)len * (size_t)a->elem_size);
}
static void jni_SetArrayRegion(void *env, void *arr, int start, int len, const void *buf) {
    (void)env;
    fake_jarray_t *a = (fake_jarray_t *)arr;
    if (!a || a->tag != JTAG_ARRAY || !buf || start < 0 || start + len > a->len) return;
    memcpy((uint8_t *)a->data + start * a->elem_size, buf, (size_t)len * (size_t)a->elem_size);
}

/* ── Argumentos de chamadas JNI (variadic / va_list / jvalue[]) ───────── */

typedef struct { void *a0; void *a1; } jargs_t;

static jargs_t args_from_va(va_list ap) {
    jargs_t r;
    r.a0 = va_arg(ap, void *);
    r.a1 = va_arg(ap, void *);
    return r;
}
static jargs_t args_from_array(const void *args) {
    jargs_t r = { NULL, NULL };
    if (args) {
        r.a0 = *(void *const *)args;
        r.a1 = *(void *const *)((const uint8_t *)args + 8);   /* jvalue = 8 bytes */
    }
    return r;
}

static const char *jstr_cstr(void *o) {
    fake_jstring_t *s = (fake_jstring_t *)o;
    if ((uintptr_t)o < 0x10000) return NULL;          /* int, nao ponteiro */
    return (s && s->tag == JTAG_STRING && s->str) ? s->str : NULL;
}

/* Primeiro argumento como string, SO se a assinatura diz que ele e objeto. */
static const char *arg_str(const char *sig, void *a0) {
    return (sig && sig[0] == '(' && (sig[1] == 'L' || sig[1] == '[')) ? jstr_cstr(a0) : NULL;
}

/* ── Excecao pendente (ExceptionCheck/Occurred/Clear/Throw) ───────────── */

static void *g_pending_exc = NULL;

/* ── Leitor minimo de zip (APK) + inflate via libz carregada em runtime ─ */

typedef struct {
    void *next_in; unsigned avail_in; unsigned long total_in;
    void *next_out; unsigned avail_out; unsigned long total_out;
    char *msg; void *state;
    void *zalloc; void *zfree; void *opaque;
    int data_type; unsigned long adler; unsigned long reserved;
} zs_t;

typedef int (*z_inflateInit2_fn)(zs_t *, int, const char *, int);
typedef int (*z_inflate_fn)(zs_t *, int);
typedef int (*z_inflateEnd_fn)(zs_t *);

static uint8_t *g_zip_cd = NULL;       /* central directory em memoria */
static uint32_t g_zip_cd_size = 0;
static char     g_zip_cd_apk[512];

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

static int zip_load_cd(FILE *f, const char *apk) {
    if (g_zip_cd && strcmp(g_zip_cd_apk, apk) == 0) return 0;
    free(g_zip_cd); g_zip_cd = NULL;

    if (fseeko(f, 0, SEEK_END) != 0) return -1;
    off_t fsz = ftello(f);
    size_t tail = fsz < 70000 ? (size_t)fsz : 70000;
    uint8_t *buf = malloc(tail);
    if (!buf) return -1;
    fseeko(f, fsz - (off_t)tail, SEEK_SET);
    if (fread(buf, 1, tail, f) != tail) { free(buf); return -1; }

    long eocd = -1;
    for (long i = (long)tail - 22; i >= 0; i--)
        if (rd32(buf + i) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) { free(buf); return -1; }

    uint32_t cd_size = rd32(buf + eocd + 12);
    uint32_t cd_off  = rd32(buf + eocd + 16);
    free(buf);

    g_zip_cd = malloc(cd_size ? cd_size : 1);
    if (!g_zip_cd) return -1;
    fseeko(f, (off_t)cd_off, SEEK_SET);
    if (fread(g_zip_cd, 1, cd_size, f) != cd_size) { free(g_zip_cd); g_zip_cd = NULL; return -1; }
    g_zip_cd_size = cd_size;
    strncpy(g_zip_cd_apk, apk, sizeof(g_zip_cd_apk) - 1);
    return 0;
}

/* Le "entry" do APK. *out recebe buffer (len+1 bytes, terminado em NUL). */
static int apk_read_entry(const char *apk, const char *entry, uint8_t **out, size_t *outlen) {
    FILE *f = fopen(apk, "rb");
    if (!f) return -1;
    int rc = -1;
    if (zip_load_cd(f, apk) != 0) goto done;

    size_t elen = strlen(entry);
    uint32_t pos = 0;
    while (pos + 46 <= g_zip_cd_size) {
        const uint8_t *e = g_zip_cd + pos;
        if (rd32(e) != 0x02014b50u) break;
        uint32_t method = rd16(e + 10);
        uint32_t csize  = rd32(e + 20);
        uint32_t usize  = rd32(e + 24);
        uint32_t nlen   = rd16(e + 28), xlen = rd16(e + 30), clen = rd16(e + 32);
        uint32_t lho    = rd32(e + 42);
        if (nlen == elen && memcmp(e + 46, entry, elen) == 0) {
            uint8_t lh[30];
            fseeko(f, (off_t)lho, SEEK_SET);
            if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50u) goto done;
            off_t data_off = (off_t)lho + 30 + rd16(lh + 26) + rd16(lh + 28);
            fseeko(f, data_off, SEEK_SET);

            uint8_t *comp = malloc(csize ? csize : 1);
            uint8_t *dst  = malloc((size_t)usize + 1);
            if (!comp || !dst) { free(comp); free(dst); goto done; }
            if (fread(comp, 1, csize, f) != csize) { free(comp); free(dst); goto done; }

            if (method == 0) {
                memcpy(dst, comp, usize);
            } else if (method == 8) {
                void *z = dlopen("libz.so.1", RTLD_NOW);
                z_inflateInit2_fn init = z ? (z_inflateInit2_fn)dlsym(z, "inflateInit2_") : NULL;
                z_inflate_fn      inf  = z ? (z_inflate_fn)dlsym(z, "inflate") : NULL;
                z_inflateEnd_fn   end  = z ? (z_inflateEnd_fn)dlsym(z, "inflateEnd") : NULL;
                if (!init || !inf || !end) { free(comp); free(dst); goto done; }
                zs_t zs; memset(&zs, 0, sizeof(zs));
                zs.next_in = comp;  zs.avail_in = csize;
                zs.next_out = dst;  zs.avail_out = usize;
                if (init(&zs, -15, "1.2.11", (int)sizeof(zs)) != 0) { free(comp); free(dst); goto done; }
                int zr = inf(&zs, 4 /* Z_FINISH */);
                end(&zs);
                if (zr != 1 /* Z_STREAM_END */) { free(comp); free(dst); goto done; }
            } else { free(comp); free(dst); goto done; }

            free(comp);
            dst[usize] = 0;
            *out = dst; *outlen = usize;
            rc = 0;
            goto done;
        }
        pos += 46 + nlen + xlen + clen;
    }
done:
    fclose(f);
    return rc;
}

/* Pool de assets abertos via AssetManager.open() */
#define MAX_ASSETS 64
static struct { uint8_t *data; size_t len; } g_assets[MAX_ASSETS];
static int g_num_assets = 0;

static int asset_open(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "assets/%s", name);
    uint8_t *data = NULL; size_t len = 0;
    if (apk_read_entry(g_apk_path, path, &data, &len) != 0) {
        jlog("   [JNI] AssetManager.open(\"%s\") -> NAO ENCONTRADO no APK (IOException)\n", name);
        return -1;
    }
    jlog("   [JNI] AssetManager.open(\"%s\") -> %lu bytes\n", name, (unsigned long)len);
    if (g_num_assets >= MAX_ASSETS) g_num_assets = 0;
    int idx = g_num_assets++;
    free(g_assets[idx].data);
    g_assets[idx].data = data;
    g_assets[idx].len = len;
    return idx;
}

static int obj_asset_idx(void *obj) {
    if (!obj || *(uint32_t *)obj != JTAG_OBJ) return -1;
    const char *x = ((fake_jobj_t *)obj)->extra;
    if (strncmp(x, "asset:", 6) != 0) return -1;
    int i = atoi(x + 6);
    return (i >= 0 && i < MAX_ASSETS && g_assets[i].data) ? i : -1;
}

/* ── Class / Member / Ref handling ─────────────────────────────────────── */

static int jni_GetVersion(void *env) { (void)env; return JNI_VERSION_1_6; }

static void *jni_FindClass(void *env, const char *name) {
    (void)env;
    void *c = alloc_jobj(JTAG_CLASS, name ? name : "java/lang/Object", NULL);
    jlog("   [JNI] FindClass(\"%s\") -> %p\n", name ? name : "(null)", c);
    return c;
}

static void *jni_GetSuperclass(void *env, void *cls) {
    (void)env; (void)cls;
    return alloc_jobj(JTAG_CLASS, "java/lang/Object", NULL);
}

static unsigned char jni_IsAssignableFrom(void *env, void *c1, void *c2) {
    (void)env; (void)c1; (void)c2; return 1;
}

static void *jni_GetObjectClass(void *env, void *obj) {
    (void)env;
    return alloc_jobj(JTAG_CLASS, obj_class_name(obj), NULL);
}

static unsigned char jni_IsInstanceOf(void *env, void *obj, void *cls) {
    (void)env;
    if (!obj) return 0;
    return strcmp(obj_class_name(obj), obj_class_name(cls)) == 0 ? 1 : 0;
}

static unsigned char jni_IsSameObject(void *env, void *a, void *b) {
    (void)env; return a == b ? 1 : 0;
}

static void *jni_NewGlobalRef(void *env, void *obj) { (void)env; return obj; }
static void  jni_DeleteGlobalRef(void *env, void *obj) { (void)env; (void)obj; }
static void  jni_DeleteLocalRef(void *env, void *obj) { (void)env; (void)obj; }
static void *jni_NewLocalRef(void *env, void *obj) { (void)env; return obj; }
static void *jni_NewWeakGlobalRef(void *env, void *obj) { (void)env; return obj; }
static void  jni_DeleteWeakGlobalRef(void *env, void *obj) { (void)env; (void)obj; }
static int   jni_EnsureLocalCapacity(void *env, int cap) { (void)env; (void)cap; return 0; }
static int   jni_PushLocalFrame(void *env, int cap) { (void)env; (void)cap; return 0; }
static void *jni_PopLocalFrame(void *env, void *res) { (void)env; return res; }

static unsigned char jni_ExceptionCheck(void *env) { (void)env; return g_pending_exc ? 1 : 0; }
static void *jni_ExceptionOccurred(void *env) { (void)env; return g_pending_exc; }
static void  jni_ExceptionDescribe(void *env) { (void)env; if (g_pending_exc) jlog("   [JNI] ExceptionDescribe: %s\n", obj_class_name(g_pending_exc)); }
static void  jni_ExceptionClear(void *env) { (void)env; g_pending_exc = NULL; }
static int   jni_Throw(void *env, void *exc) { (void)env; g_pending_exc = exc; return 0; }
static int   jni_ThrowNew(void *env, void *cls, const char *msg) {
    (void)env;
    jlog("   [JNI] ThrowNew(%s, \"%s\")\n", obj_class_name(cls), msg ? msg : "");
    g_pending_exc = alloc_jobj(JTAG_OBJ, obj_class_name(cls), msg);
    return 0;
}
static void  jni_FatalError(void *env, const char *msg) {
    (void)env;
    printf("\n[JNI] *** FatalError(\"%s\") chamado pelo Unity -- encerrando ***\n", msg ? msg : "(null)");
    fflush(stdout); fflush(stderr);
    _exit(4);
}

static void *alloc_member(void *clazz, const char *name, const char *sig) {
    fake_member_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->tag = JTAG_MEMID;
    strncpy(m->cls, obj_class_name(clazz), sizeof(m->cls) - 1);
    if (name) strncpy(m->name, name, sizeof(m->name) - 1);
    if (sig)  strncpy(m->sig,  sig,  sizeof(m->sig) - 1);
    return m;
}

static void *jni_GetMethodID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    jlog("   [JNI] GetMethodID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetStaticMethodID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    jlog("   [JNI] GetStaticMethodID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    jlog("   [JNI] GetFieldID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetStaticFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    jlog("   [JNI] GetStaticFieldID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

/* ── Method & Field Dispatch ───────────────────────────────────────────── */


/* ── Helpers de File ──────────────────────────────────────────────────── */
static void *make_file(const char *path) {
    return alloc_jobj(JTAG_OBJ, "java/io/File", path);
}
static void make_subdir(char *out, size_t n, const char *sub) {
    snprintf(out, n, "%s/%s", g_save_dir, sub);
    mkdir(g_save_dir, 0755);
    mkdir(out, 0755);
}
static void path_parent(const char *path, char *out, size_t n) {
    strncpy(out, path, n - 1); out[n - 1] = 0;
    char *sl = strrchr(out, '/');
    if (!sl) { strcpy(out, "/"); return; }
    if (sl == out) out[1] = 0; else *sl = 0;
}
static const char *path_name(const char *path) {
    const char *sl = strrchr(path, '/');
    return sl ? sl + 1 : path;
}

static int starts_with(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static void note_unhandled(const char *kind, const fake_member_t *m) {
    char key[200];
    snprintf(key, sizeof(key), "%s|%s.%s", kind, m->cls, m->name);
    int n = seen_key(key);
    if (n <= 2)
        jlog("   [JNI] %s nao tratado: %s.%s %s%s\n", kind, m->cls, m->name, m->sig,
             n == 2 ? "  (nao repete mais)" : "");
}

static void *dispatch_obj_method(void *env, void *obj, fake_member_t *m, jargs_t a) {
    if (!m || m->tag != JTAG_MEMID) return NULL;
    const char *n = m->name;
    const char *c = m->cls;
    fake_jobj_t *jo = (obj && *(uint32_t *)obj == JTAG_OBJ) ? (fake_jobj_t *)obj : NULL;

    /* ---- Diagnostico: dialogo de erro do Unity ---- */
    if (strcmp(n, "setTitle") == 0 || strcmp(n, "setMessage") == 0) {
        const char *t = jstr_cstr(a.a0);
        printf("   [JNI][DIALOGO] %s.%s(\"%s\")\n", c, n, t ? t : "(nao-string)");
        fflush(stdout);
        return obj;
    }

    /* ---- Context / PackageManager ---- */
    if (strcmp(n, "getPackageName") == 0)
        return jni_NewStringUTF(env, "com.nekki.shadowfight");
    if (strcmp(n, "getPackageCodePath") == 0 || strcmp(n, "getPackageResourcePath") == 0)
        return jni_NewStringUTF(env, g_apk_path);
    if (strcmp(n, "getApplicationContext") == 0)
        return obj ? obj : fake_jni_make_object("android/content/Context");
    if (strcmp(n, "getApplicationInfo") == 0)
        return fake_jni_make_object("android/content/pm/ApplicationInfo");
    if (strcmp(n, "getPackageManager") == 0)
        return fake_jni_make_object("android/content/pm/PackageManager");
    if (strcmp(n, "getPackageInfo") == 0)
        return fake_jni_make_object("android/content/pm/PackageInfo");
    if (strcmp(n, "queryIntentActivities") == 0)
        return fake_jni_make_object("java/util/ArrayList");
    if (strcmp(n, "getAssets") == 0)
        return fake_jni_make_object("android/content/res/AssetManager");
    if (strcmp(n, "getResources") == 0)
        return fake_jni_make_object("android/content/res/Resources");
    if (strcmp(n, "getDisplayMetrics") == 0)
        return fake_jni_make_object("android/util/DisplayMetrics");
    if (strcmp(n, "getConfiguration") == 0)
        return fake_jni_make_object("android/content/res/Configuration");

    /* ---- Arquivos / diretorios ----
     * files/cache/obb ficam em subpastas de save/, entao File.getParent()
     * de "files" devolve a propria pasta save (como /data/data/<pkg>). */
    if (strcmp(n, "getFilesDir") == 0 || strcmp(n, "getNoBackupFilesDir") == 0 ||
        strcmp(n, "getExternalFilesDir") == 0) {
        char d[600]; make_subdir(d, sizeof d, "files"); return make_file(d);
    }
    if (strcmp(n, "getCacheDir") == 0 || strcmp(n, "getCodeCacheDir") == 0 ||
        strcmp(n, "getExternalCacheDir") == 0) {
        char d[600]; make_subdir(d, sizeof d, "cache"); return make_file(d);
    }
    if (strcmp(n, "getObbDir") == 0) {
        char d[600]; make_subdir(d, sizeof d, "obb"); return make_file(d);
    }
    if (strcmp(n, "getExternalFilesDirs") == 0 || strcmp(n, "getExternalCacheDirs") == 0 ||
        strcmp(n, "getObbDirs") == 0) {
        char d[600];
        make_subdir(d, sizeof d, strstr(n, "Cache") ? "cache" : strstr(n, "Obb") ? "obb" : "files");
        return jni_NewObjectArray(env, 1, NULL, make_file(d));
    }
    if (strcmp(n, "getAbsolutePath") == 0 || strcmp(n, "getPath") == 0 ||
        strcmp(n, "getCanonicalPath") == 0 || strcmp(n, "getAbsoluteFile") == 0 ||
        strcmp(n, "getCanonicalFile") == 0) {
        const char *pth = (jo && jo->extra[0]) ? jo->extra : g_save_dir;
        if (strstr(n, "File")) return make_file(pth);
        return jni_NewStringUTF(env, pth);
    }
    if (strcmp(n, "getParent") == 0 || strcmp(n, "getParentFile") == 0) {
        char par[600];
        path_parent((jo && jo->extra[0]) ? jo->extra : g_save_dir, par, sizeof par);
        return strcmp(n, "getParent") == 0 ? jni_NewStringUTF(env, par) : make_file(par);
    }
    if (strcmp(n, "getName") == 0 && strcmp(c, "java/io/File") == 0)
        return jni_NewStringUTF(env, path_name((jo && jo->extra[0]) ? jo->extra : g_save_dir));

    /* ---- java.lang.String ---- */
    {
        const char *str = jstr_cstr(obj);
        if (str) {
            const char *arg = arg_str(m->sig, a.a0);
            if (strcmp(n, "toString") == 0 || strcmp(n, "trim") == 0 || strcmp(n, "intern") == 0)
                return obj;
            if (strcmp(n, "toLowerCase") == 0 || strcmp(n, "toUpperCase") == 0) {
                char *t = strdup(str);
                for (char *q = t; *q; q++)
                    *q = (n[2] == 'L') ? (char)((*q >= 'A' && *q <= 'Z') ? *q + 32 : *q)
                                       : (char)((*q >= 'a' && *q <= 'z') ? *q - 32 : *q);
                void *r = jni_NewStringUTF(env, t); free(t); return r;
            }
            if (strcmp(n, "substring") == 0) {
                int len = (int)strlen(str), b = (int)(intptr_t)a.a0;
                int e = (m->sig[2] == 'I' && m->sig[3] == 'I') ? (int)(intptr_t)a.a1 : len;
                if (b < 0) b = 0; if (e > len) e = len; if (e < b) e = b;
                char *t = strndup(str + b, (size_t)(e - b));
                void *r = jni_NewStringUTF(env, t); free(t); return r;
            }
            if (strcmp(n, "concat") == 0 && arg) {
                size_t L = strlen(str) + strlen(arg) + 1;
                char *t = malloc(L); snprintf(t, L, "%s%s", str, arg);
                void *r = jni_NewStringUTF(env, t); free(t); return r;
            }
        }
    }

    /* ---- Classes / ClassLoader ---- */
    if (strcmp(n, "getClass") == 0)
        return alloc_jobj(JTAG_CLASS, obj_class_name(obj), NULL);
    if (strcmp(n, "getClassLoader") == 0)
        return fake_jni_make_object("java/lang/ClassLoader");
    if (strcmp(n, "findLibrary") == 0) {
        const char *lib = jstr_cstr(a.a0);
        char path[1100];
        snprintf(path, sizeof(path), "%s/lib%s.so", g_libs_dir, lib ? lib : "");
        int ok = (access(path, F_OK) == 0);
        jlog("   [JNI] ClassLoader.findLibrary(\"%s\") -> %s\n", lib ? lib : "(null)", ok ? path : "NULL");
        return ok ? jni_NewStringUTF(env, path) : NULL;
    }

    /* ---- Servicos / Display ---- */
    if (strcmp(n, "getSystemService") == 0)
        return fake_jni_make_object("android/view/WindowManager");
    if (strcmp(n, "getDefaultDisplay") == 0)
        return fake_jni_make_object("android/view/Display");
    if (strcmp(n, "getWindowManager") == 0)
        return fake_jni_make_object("android/view/WindowManager");
    if (strcmp(n, "getLanguage") == 0)
        return jni_NewStringUTF(env, "en");
    if (strcmp(n, "getCountry") == 0)
        return jni_NewStringUTF(env, "US");
    if (strcmp(n, "toString") == 0)
        return jni_NewStringUTF(env, c);

    /* ---- SharedPreferences (em memoria, sem persistencia) ---- */
    if (strcmp(n, "getSharedPreferences") == 0)
        return fake_jni_make_object("android/content/SharedPreferences");
    if (strcmp(n, "edit") == 0)
        return fake_jni_make_object("android/content/SharedPreferences$Editor");
    if (strcmp(n, "getString") == 0)
        return a.a1;                                  /* valor default */
    if (strcmp(n, "getAll") == 0)
        return fake_jni_make_object("java/util/HashMap");
    if (strcmp(n, "entrySet") == 0 || strcmp(n, "keySet") == 0)
        return fake_jni_make_object("java/util/HashSet");
    if (strcmp(n, "iterator") == 0)
        return fake_jni_make_object("java/util/Iterator");
    if (ends_with(c, "$Editor") &&
        (starts_with(n, "put") || strcmp(n, "remove") == 0 || strcmp(n, "clear") == 0))
        return obj;                                   /* builder */

    /* ---- Looper / Handler ---- */
    if (strcmp(n, "getMainLooper") == 0 || strcmp(n, "myLooper") == 0 || strcmp(n, "getLooper") == 0)
        return fake_jni_make_object("android/os/Looper");

    /* ---- AssetManager.open / Scanner (usado pelo initJni) ---- */
    if (strcmp(n, "open") == 0 && strcmp(c, "android/content/res/AssetManager") == 0) {
        const char *name = jstr_cstr(a.a0);
        int idx = name ? asset_open(name) : -1;
        if (idx < 0) {
            g_pending_exc = alloc_jobj(JTAG_OBJ, "java/io/IOException", name);
            return NULL;
        }
        char tag[32]; snprintf(tag, sizeof(tag), "asset:%d", idx);
        return alloc_jobj(JTAG_OBJ, "java/io/InputStream", tag);
    }
    if (strcmp(n, "useDelimiter") == 0)
        return obj;
    if (strcmp(n, "next") == 0 && strcmp(c, "java/util/Scanner") == 0) {
        int i = obj_asset_idx(obj);
        if (i < 0 || g_assets[i].len == 0) {
            g_pending_exc = alloc_jobj(JTAG_OBJ, "java/util/NoSuchElementException", NULL);
            return NULL;
        }
        return jni_NewStringUTF(env, (const char *)g_assets[i].data);
    }

    /* ---- Builders (Intent, AlertDialog$Builder, ...): devolvem this ---- */
    if (strcmp(c, "android/content/Intent") == 0 &&
        (starts_with(n, "add") || starts_with(n, "set") || starts_with(n, "put")))
        return obj;
    if (strstr(c, "Builder") &&
        (starts_with(n, "set") || starts_with(n, "add") || strcmp(n, "create") == 0 || strcmp(n, "show") == 0))
        return obj ? obj : fake_jni_make_object(c);

    note_unhandled("CallObjectMethod", m);
    /* Metodo que devolve String: devolve "" para o Unity nao fazer strlen(NULL) */
    if (ends_with(m->sig, ")Ljava/lang/String;"))
        return jni_NewStringUTF(env, "");
    return NULL;
}

static void *jni_NewObjectArgs(void *env, void *cls, jargs_t a) {
    (void)env;
    const char *cn = obj_class_name(cls);
    fake_jobj_t *o = (fake_jobj_t *)fake_jni_make_object(cn);
    /* new Scanner(InputStream, String): herda o asset do stream */
    if (o && strcmp(cn, "java/util/Scanner") == 0 && a.a0 && *(uint32_t *)a.a0 == JTAG_OBJ)
        memcpy(o->extra, ((fake_jobj_t *)a.a0)->extra, sizeof(o->extra));
    /* new File(String) / new File(File, String) */
    if (o && strcmp(cn, "java/io/File") == 0) {
        const char *p = jstr_cstr(a.a0);
        if (p) strncpy(o->extra, p, sizeof(o->extra) - 1);
    }
    return o;
}
static void *jni_NewObjectV(void *env, void *cls, void *mid, va_list ap) {
    (void)mid;
    return jni_NewObjectArgs(env, cls, args_from_va(ap));
}
static void *jni_NewObjectA(void *env, void *cls, void *mid, const void *args) {
    (void)mid;
    return jni_NewObjectArgs(env, cls, args_from_array(args));
}
static void *jni_NewObject(void *env, void *cls, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    void *r = jni_NewObjectV(env, cls, mid, ap);
    va_end(ap); return r;
}
static void *jni_AllocObject(void *env, void *cls) {
    (void)env;
    return fake_jni_make_object(obj_class_name(cls));
}

static void *jni_CallObjectMethodV(void *env, void *obj, void *mid, va_list ap) {
    return dispatch_obj_method(env, obj, (fake_member_t *)mid, args_from_va(ap));
}
static void *jni_CallObjectMethodA(void *env, void *obj, void *mid, const void *args) {
    return dispatch_obj_method(env, obj, (fake_member_t *)mid, args_from_array(args));
}
static void *jni_CallObjectMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    void *r = jni_CallObjectMethodV(env, obj, mid, ap);
    va_end(ap); return r;
}

/* ---- int ---- */
static int dispatch_int_method(void *obj, fake_member_t *m, jargs_t a) {
    (void)obj;
    if (!m || m->tag != JTAG_MEMID) return 0;
    if (strcmp(m->name, "getWidth") == 0)  return g_disp_w;
    if (strcmp(m->name, "getHeight") == 0) return g_disp_h;
    if (strcmp(m->name, "getRotation") == 0) return 0;
    {   /* java.lang.String */
        const char *str = jstr_cstr(obj);
        if (str) {
            const char *n = m->name, *arg = arg_str(m->sig, a.a0);
            if (strcmp(n, "length") == 0) return (int)strlen(str);
            if (strcmp(n, "hashCode") == 0) {
                int h = 0;
                for (const char *q = str; *q; q++) h = 31 * h + (unsigned char)*q;
                return h;
            }
            if (strcmp(n, "indexOf") == 0 && arg) { const char *f = strstr(str, arg); return f ? (int)(f - str) : -1; }
            if (strcmp(n, "indexOf") == 0) { const char *f = strchr(str, (int)(intptr_t)a.a0); return f ? (int)(f - str) : -1; }
            if (strcmp(n, "lastIndexOf") == 0 && arg) {
                const char *f = NULL, *q = str;
                while ((q = strstr(q, arg)) != NULL) { f = q; q++; }
                return f ? (int)(f - str) : -1;
            }
            if (strcmp(n, "compareTo") == 0 && arg) return strcmp(str, arg);
        }
    }
    if (strcmp(m->name, "getInt") == 0)      return (int)(intptr_t)a.a1;   /* SharedPreferences default */
    if (strcmp(m->name, "available") == 0) {
        int i = obj_asset_idx(obj);
        return i >= 0 ? (int)g_assets[i].len : 0;
    }
    note_unhandled("CallIntMethod", m);
    return 0;
}
static int jni_CallIntMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env;
    return dispatch_int_method(obj, (fake_member_t *)mid, args_from_va(ap));
}
static int jni_CallIntMethodA(void *env, void *obj, void *mid, const void *args) {
    (void)env;
    return dispatch_int_method(obj, (fake_member_t *)mid, args_from_array(args));
}
static int jni_CallIntMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    int r = jni_CallIntMethodV(env, obj, mid, ap);
    va_end(ap); return r;
}

/* ---- boolean ---- */
static unsigned char dispatch_bool_method(void *obj, fake_member_t *m, jargs_t a) {
    if (!m || m->tag != JTAG_MEMID) return 0;
    const char *n = m->name;
    fake_jobj_t *jo = (obj && *(uint32_t *)obj == JTAG_OBJ) ? (fake_jobj_t *)obj : NULL;

    if (strcmp(n, "equals") == 0) {
        const char *x = jstr_cstr(obj), *y = jstr_cstr(a.a0);
        if (x && y) return strcmp(x, y) == 0;
        return obj == a.a0;
    }
    {   /* java.lang.String */
        const char *str = jstr_cstr(obj), *arg = arg_str(m->sig, a.a0);
        if (str) {
            if (strcmp(n, "isEmpty") == 0)    return str[0] == 0;
            if (strcmp(n, "startsWith") == 0 && arg) return strncmp(str, arg, strlen(arg)) == 0;
            if (strcmp(n, "endsWith") == 0 && arg) {
                size_t a1 = strlen(str), b1 = strlen(arg);
                return a1 >= b1 && strcmp(str + a1 - b1, arg) == 0;
            }
            if (strcmp(n, "contains") == 0 && arg) return strstr(str, arg) != NULL;
            if (strcmp(n, "equalsIgnoreCase") == 0 && arg) return strcasecmp(str, arg) == 0;
        }
    }
    if (strcmp(n, "isEmpty") == 0)  return 1;      /* List vazia: sem LEANBACK/TV */
    if (strcmp(n, "hasNext") == 0) {
        if (strcmp(m->cls, "java/util/Scanner") == 0) {
            int i = obj_asset_idx(obj);
            return i >= 0 && g_assets[i].len > 0;
        }
        return 0;
    }
    if (jo && strcmp(m->cls, "java/io/File") == 0 && jo->extra[0]) {
        struct stat st;
        if (strcmp(n, "exists") == 0)      return stat(jo->extra, &st) == 0;
        if (strcmp(n, "isDirectory") == 0) return stat(jo->extra, &st) == 0 && S_ISDIR(st.st_mode);
        if (strcmp(n, "isFile") == 0)      return stat(jo->extra, &st) == 0 && S_ISREG(st.st_mode);
        if (strcmp(n, "canRead") == 0 || strcmp(n, "canWrite") == 0) return access(jo->extra, R_OK | W_OK) == 0;
        if (strcmp(n, "mkdirs") == 0 || strcmp(n, "mkdir") == 0)     return mkdir(jo->extra, 0755) == 0;
    }
    if (strcmp(n, "contains") == 0) return 0;
    if (strcmp(n, "getBoolean") == 0) return (unsigned char)((intptr_t)a.a1 & 0xff);
    note_unhandled("CallBooleanMethod", m);
    return 0;
}
static unsigned char jni_CallBooleanMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env;
    return dispatch_bool_method(obj, (fake_member_t *)mid, args_from_va(ap));
}
static unsigned char jni_CallBooleanMethodA(void *env, void *obj, void *mid, const void *args) {
    (void)env;
    return dispatch_bool_method(obj, (fake_member_t *)mid, args_from_array(args));
}
static unsigned char jni_CallBooleanMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    unsigned char r = jni_CallBooleanMethodV(env, obj, mid, ap);
    va_end(ap); return r;
}

static int64_t jni_CallLongMethodV(void *env, void *obj, void *mid, va_list ap) { (void)env;(void)obj;(void)mid;(void)ap; return 0; }
static int64_t jni_CallLongMethodA(void *env, void *obj, void *mid, const void *a) { (void)env;(void)obj;(void)mid;(void)a; return 0; }
static int64_t jni_CallLongMethod(void *env, void *obj, void *mid, ...) { (void)env;(void)obj;(void)mid; return 0; }
static SOFTFP float jni_CallFloatMethodV(void *env, void *obj, void *mid, va_list ap) { (void)env;(void)obj;(void)mid;(void)ap; return 0.0f; }
static SOFTFP float jni_CallFloatMethodA(void *env, void *obj, void *mid, const void *a) { (void)env;(void)obj;(void)mid;(void)a; return 0.0f; }
static SOFTFP float jni_CallFloatMethod(void *env, void *obj, void *mid, ...) { (void)env;(void)obj;(void)mid; return 0.0f; }
static SOFTFP double jni_CallDoubleMethodV(void *env, void *obj, void *mid, va_list ap) { (void)env;(void)obj;(void)mid;(void)ap; return 0.0; }
static SOFTFP double jni_CallDoubleMethodA(void *env, void *obj, void *mid, const void *a) { (void)env;(void)obj;(void)mid;(void)a; return 0.0; }
static SOFTFP double jni_CallDoubleMethod(void *env, void *obj, void *mid, ...) { (void)env;(void)obj;(void)mid; return 0.0; }

/* ---- void ---- */
static void dispatch_void_method(void *obj, fake_member_t *m, jargs_t a) {
    (void)obj; (void)a;
    if (!m || m->tag != JTAG_MEMID) return;
    if (strcmp(m->name, "kill") == 0) {
        /* UnityPlayer.kill(): nunca retorna no Android. Encerra o processo. */
        printf("\n[JNI] *** UnityPlayer.kill() chamado -- o Unity abortou por erro fatal (veja [JNI][DIALOGO] acima) ***\n");
        fflush(stdout); fflush(stderr);
        _exit(3);
    }
    char key[200];
    snprintf(key, sizeof(key), "void|%s.%s", m->cls, m->name);
    int cnt = seen_key(key);
    if (cnt <= 2)
        jlog("   [JNI] CallVoidMethod: %s.%s %s\n", m->cls, m->name, m->sig);
}
static void jni_CallVoidMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env;
    dispatch_void_method(obj, (fake_member_t *)mid, args_from_va(ap));
}
static void jni_CallVoidMethodA(void *env, void *obj, void *mid, const void *args) {
    (void)env;
    dispatch_void_method(obj, (fake_member_t *)mid, args_from_array(args));
}
static void jni_CallVoidMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    jni_CallVoidMethodV(env, obj, mid, ap);
    va_end(ap);
}

/* ---- static ---- */
static void *dispatch_static_obj(void *env, void *cls, fake_member_t *m, jargs_t a) {
    (void)a;
    if (!m || m->tag != JTAG_MEMID) return NULL;
    if (strcmp(m->name, "getDefault") == 0)
        return fake_jni_make_object("java/util/Locale");
    if (strcmp(m->name, "getExternalStorageDirectory") == 0 || strcmp(m->name, "getDownloadCacheDirectory") == 0)
        return make_file(g_save_dir);
    if (strcmp(m->name, "getExternalStorageState") == 0)
        return jni_NewStringUTF(env, "mounted");
    if (strcmp(m->name, "getMainLooper") == 0 || strcmp(m->name, "myLooper") == 0)
        return fake_jni_make_object("android/os/Looper");
    if (strcmp(m->name, "newInterfaceProxy") == 0)     /* JNIBridge: proxy nao-nulo */
        return fake_jni_make_object("java/lang/reflect/Proxy");
    if (strcmp(m->name, "forName") == 0)
        return alloc_jobj(JTAG_CLASS, jstr_cstr(a.a0) ? jstr_cstr(a.a0) : "java/lang/Object", NULL);
    char key[200];
    snprintf(key, sizeof(key), "static|%s.%s", obj_class_name(cls), m->name);
    int cnt = seen_key(key);
    if (cnt <= 2)
        jlog("   [JNI] CallStaticObjectMethod: %s.%s %s -> NULL\n", obj_class_name(cls), m->name, m->sig);
    return NULL;
}
static void *jni_CallStaticObjectMethodV(void *env, void *cls, void *mid, va_list ap) {
    return dispatch_static_obj(env, cls, (fake_member_t *)mid, args_from_va(ap));
}
static void *jni_CallStaticObjectMethodA(void *env, void *cls, void *mid, const void *a) {
    return dispatch_static_obj(env, cls, (fake_member_t *)mid, args_from_array(a));
}
static void *jni_CallStaticObjectMethod(void *env, void *cls, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    void *r = jni_CallStaticObjectMethodV(env, cls, mid, ap);
    va_end(ap); return r;
}

static unsigned char jni_CallStaticBooleanMethodV(void *env, void *cls, void *mid, va_list ap) { (void)env;(void)cls;(void)mid;(void)ap; return 0; }
static unsigned char jni_CallStaticBooleanMethodA(void *env, void *cls, void *mid, const void *a) { (void)env;(void)cls;(void)mid;(void)a; return 0; }
static unsigned char jni_CallStaticBooleanMethod(void *env, void *cls, void *mid, ...) { (void)env;(void)cls;(void)mid; return 0; }
static int jni_CallStaticIntMethodV(void *env, void *cls, void *mid, va_list ap) { (void)env;(void)cls;(void)mid;(void)ap; return 0; }
static int jni_CallStaticIntMethodA(void *env, void *cls, void *mid, const void *a) { (void)env;(void)cls;(void)mid;(void)a; return 0; }
static int jni_CallStaticIntMethod(void *env, void *cls, void *mid, ...) { (void)env;(void)cls;(void)mid; return 0; }
static void jni_CallStaticVoidMethodV(void *env, void *cls, void *mid, va_list ap) { (void)env;(void)cls;(void)mid;(void)ap; }
static void jni_CallStaticVoidMethodA(void *env, void *cls, void *mid, const void *a) { (void)env;(void)cls;(void)mid;(void)a; }
static void jni_CallStaticVoidMethod(void *env, void *cls, void *mid, ...) { (void)env;(void)cls;(void)mid; }

/* Field getters */
static void *jni_GetObjectField(void *env, void *obj, void *fid) {
    (void)obj;
    fake_member_t *m = (fake_member_t *)fid;
    if (!m || m->tag != JTAG_MEMID) return NULL;
    if (strcmp(m->name, "sourceDir") == 0 || strcmp(m->name, "publicSourceDir") == 0)
        return jni_NewStringUTF(env, g_apk_path);
    if (strcmp(m->name, "dataDir") == 0 || strcmp(m->name, "nativeLibraryDir") == 0)
        return jni_NewStringUTF(env, g_save_dir);
    if (strcmp(m->name, "packageName") == 0)
        return jni_NewStringUTF(env, "com.nekki.shadowfight");
    if (strcmp(m->name, "versionName") == 0)
        return jni_NewStringUTF(env, "2.24.0");
    if (strcmp(m->name, "splitSourceDirs") == 0 || strcmp(m->name, "splitPublicSourceDirs") == 0)
        return NULL;
    jlog("   [JNI] GetObjectField: %s.%s %s -> NULL\n", m->cls, m->name, m->sig);
    return NULL;
}

static int jni_GetIntField(void *env, void *obj, void *fid) {
    (void)env; (void)obj;
    fake_member_t *m = (fake_member_t *)fid;
    if (!m || m->tag != JTAG_MEMID) return 0;
    if (strcmp(m->name, "widthPixels") == 0)  return g_disp_w;
    if (strcmp(m->name, "heightPixels") == 0) return g_disp_h;
    if (strcmp(m->name, "densityDpi") == 0)   return 320;
    if (strcmp(m->name, "versionCode") == 0)  return 22400;
    if (strcmp(m->name, "targetSdkVersion") == 0) return 29;
    return 0;
}

static SOFTFP float jni_GetFloatField(void *env, void *obj, void *fid) {
    (void)env; (void)obj;
    fake_member_t *m = (fake_member_t *)fid;
    if (!m || m->tag != JTAG_MEMID) return 0.0f;
    if (strcmp(m->name, "density") == 0 || strcmp(m->name, "scaledDensity") == 0) return 2.0f;
    if (strcmp(m->name, "xdpi") == 0 || strcmp(m->name, "ydpi") == 0) return 268.0f;
    return 0.0f;
}

static unsigned char jni_GetBooleanField(void *env, void *obj, void *fid) { (void)env;(void)obj;(void)fid; return 0; }
static int64_t jni_GetLongField(void *env, void *obj, void *fid) { (void)env;(void)obj;(void)fid; return 0; }

static void *jni_GetStaticObjectField(void *env, void *cls, void *fid) {
    (void)cls;
    fake_member_t *m = (fake_member_t *)fid;
    if (!m || m->tag != JTAG_MEMID) return NULL;
    if (strcmp(m->name, "MODEL") == 0)        return jni_NewStringUTF(env, "R36UltraX");
    if (strcmp(m->name, "MANUFACTURER") == 0) return jni_NewStringUTF(env, "GameConsole");
    if (strcmp(m->name, "DEVICE") == 0)       return jni_NewStringUTF(env, "r36ux");
    if (strcmp(m->name, "BRAND") == 0)        return jni_NewStringUTF(env, "R36");
    if (strcmp(m->name, "HARDWARE") == 0)     return jni_NewStringUTF(env, "rk3326");
    if (strcmp(m->name, "CPU_ABI") == 0)      return jni_NewStringUTF(env, "armeabi-v7a");
    if (strcmp(m->name, "CPU_ABI2") == 0)     return jni_NewStringUTF(env, "armeabi");
    if (strcmp(m->name, "SUPPORTED_ABIS") == 0 || strcmp(m->name, "SUPPORTED_32_BIT_ABIS") == 0) {
        void *arr = jni_NewObjectArray(env, 2, NULL, NULL);
        jni_SetObjectArrayElement(env, arr, 0, jni_NewStringUTF(env, "armeabi-v7a"));
        jni_SetObjectArrayElement(env, arr, 1, jni_NewStringUTF(env, "armeabi"));
        return arr;
    }
    if (strcmp(m->name, "SUPPORTED_64_BIT_ABIS") == 0)
        return jni_NewObjectArray(env, 0, NULL, NULL);
    if (strcmp(m->name, "MEDIA_MOUNTED") == 0)   return jni_NewStringUTF(env, "mounted");
    if (strcmp(m->name, "ACTION_MAIN") == 0)     return jni_NewStringUTF(env, "android.intent.action.MAIN");
    if (strcmp(m->name, "LOCATION_SERVICE") == 0) return jni_NewStringUTF(env, "location");
    /* Qualquer outra constante String: devolve o proprio nome (nao-nula) */
    if (strcmp(m->sig, "Ljava/lang/String;") == 0)
        return jni_NewStringUTF(env, m->name);
    return NULL;
}

static int jni_GetStaticIntField(void *env, void *cls, void *fid) {
    (void)env; (void)cls;
    fake_member_t *m = (fake_member_t *)fid;
    if (!m || m->tag != JTAG_MEMID) return 0;
    if (strcmp(m->name, "SDK_INT") == 0)           return 29;
    if (strcmp(m->name, "MODE_PRIVATE") == 0)      return 0;
    if (strcmp(m->name, "GET_META_DATA") == 0)     return 128;
    if (strcmp(m->name, "GET_INTENT_FILTERS") == 0) return 8;
    if (strcmp(m->name, "BUTTON_POSITIVE") == 0)   return -1;
    if (strcmp(m->name, "BUTTON_NEGATIVE") == 0)   return -2;
    if (strcmp(m->name, "BUTTON_NEUTRAL") == 0)    return -3;
    return 0;
}

static int jni_RegisterNatives(void *env, void *clazz, const JNINativeMethod *methods, int nMethods) {
    (void)env;
    const char *cname = obj_class_name(clazz);
    jlog("   [JNI] RegisterNatives(%s, %d metodos):\n", cname, nMethods);
    for (int i = 0; i < nMethods; i++) {
        printf("         - %s %s -> %p\n",
               methods[i].name ? methods[i].name : "?",
               methods[i].signature ? methods[i].signature : "?",
               methods[i].fnPtr);
        if (g_num_reg_natives < MAX_REG_NATIVES && methods[i].name) {
            reg_native_t *rn = &g_reg_natives[g_num_reg_natives++];
            strncpy(rn->cls,  cname, sizeof(rn->cls) - 1);
            strncpy(rn->name, methods[i].name, sizeof(rn->name) - 1);
            if (methods[i].signature)
                strncpy(rn->sig, methods[i].signature, sizeof(rn->sig) - 1);
            rn->fnPtr = methods[i].fnPtr;
        }
    }
    return JNI_OK;
}

static int jni_UnregisterNatives(void *env, void *clazz) { (void)env; (void)clazz; return JNI_OK; }
static int jni_GetJavaVM(void *env, void **vm) { (void)env; if (vm) *vm = fake_jni_get_vm(); return JNI_OK; }
static int jni_unimpl_stub(void) { return 0; }
/* Stubs numerados: logam qual funcao JNIEnv nao implementada o Unity chamou */
#define UNIMPL(N) static int jni_unimpl_##N(void) { char k[32]; snprintf(k, sizeof k, "unimpl%d", N); if (seen_key(k) == 1) jlog("   [JNI] funcao JNIEnv NAO IMPLEMENTADA chamada: indice %d\n", N); return 0; }
UNIMPL(0)
UNIMPL(1)
UNIMPL(2)
UNIMPL(3)
UNIMPL(4)
UNIMPL(5)
UNIMPL(6)
UNIMPL(7)
UNIMPL(8)
UNIMPL(9)
UNIMPL(10)
UNIMPL(11)
UNIMPL(12)
UNIMPL(13)
UNIMPL(14)
UNIMPL(15)
UNIMPL(16)
UNIMPL(17)
UNIMPL(18)
UNIMPL(19)
UNIMPL(20)
UNIMPL(21)
UNIMPL(22)
UNIMPL(23)
UNIMPL(24)
UNIMPL(25)
UNIMPL(26)
UNIMPL(27)
UNIMPL(28)
UNIMPL(29)
UNIMPL(30)
UNIMPL(31)
UNIMPL(32)
UNIMPL(33)
UNIMPL(34)
UNIMPL(35)
UNIMPL(36)
UNIMPL(37)
UNIMPL(38)
UNIMPL(39)
UNIMPL(40)
UNIMPL(41)
UNIMPL(42)
UNIMPL(43)
UNIMPL(44)
UNIMPL(45)
UNIMPL(46)
UNIMPL(47)
UNIMPL(48)
UNIMPL(49)
UNIMPL(50)
UNIMPL(51)
UNIMPL(52)
UNIMPL(53)
UNIMPL(54)
UNIMPL(55)
UNIMPL(56)
UNIMPL(57)
UNIMPL(58)
UNIMPL(59)
UNIMPL(60)
UNIMPL(61)
UNIMPL(62)
UNIMPL(63)
UNIMPL(64)
UNIMPL(65)
UNIMPL(66)
UNIMPL(67)
UNIMPL(68)
UNIMPL(69)
UNIMPL(70)
UNIMPL(71)
UNIMPL(72)
UNIMPL(73)
UNIMPL(74)
UNIMPL(75)
UNIMPL(76)
UNIMPL(77)
UNIMPL(78)
UNIMPL(79)
UNIMPL(80)
UNIMPL(81)
UNIMPL(82)
UNIMPL(83)
UNIMPL(84)
UNIMPL(85)
UNIMPL(86)
UNIMPL(87)
UNIMPL(88)
UNIMPL(89)
UNIMPL(90)
UNIMPL(91)
UNIMPL(92)
UNIMPL(93)
UNIMPL(94)
UNIMPL(95)
UNIMPL(96)
UNIMPL(97)
UNIMPL(98)
UNIMPL(99)
UNIMPL(100)
UNIMPL(101)
UNIMPL(102)
UNIMPL(103)
UNIMPL(104)
UNIMPL(105)
UNIMPL(106)
UNIMPL(107)
UNIMPL(108)
UNIMPL(109)
UNIMPL(110)
UNIMPL(111)
UNIMPL(112)
UNIMPL(113)
UNIMPL(114)
UNIMPL(115)
UNIMPL(116)
UNIMPL(117)
UNIMPL(118)
UNIMPL(119)
UNIMPL(120)
UNIMPL(121)
UNIMPL(122)
UNIMPL(123)
UNIMPL(124)
UNIMPL(125)
UNIMPL(126)
UNIMPL(127)
UNIMPL(128)
UNIMPL(129)
UNIMPL(130)
UNIMPL(131)
UNIMPL(132)
UNIMPL(133)
UNIMPL(134)
UNIMPL(135)
UNIMPL(136)
UNIMPL(137)
UNIMPL(138)
UNIMPL(139)
UNIMPL(140)
UNIMPL(141)
UNIMPL(142)
UNIMPL(143)
UNIMPL(144)
UNIMPL(145)
UNIMPL(146)
UNIMPL(147)
UNIMPL(148)
UNIMPL(149)
UNIMPL(150)
UNIMPL(151)
UNIMPL(152)
UNIMPL(153)
UNIMPL(154)
UNIMPL(155)
UNIMPL(156)
UNIMPL(157)
UNIMPL(158)
UNIMPL(159)
UNIMPL(160)
UNIMPL(161)
UNIMPL(162)
UNIMPL(163)
UNIMPL(164)
UNIMPL(165)
UNIMPL(166)
UNIMPL(167)
UNIMPL(168)
UNIMPL(169)
UNIMPL(170)
UNIMPL(171)
UNIMPL(172)
UNIMPL(173)
UNIMPL(174)
UNIMPL(175)
UNIMPL(176)
UNIMPL(177)
UNIMPL(178)
UNIMPL(179)
UNIMPL(180)
UNIMPL(181)
UNIMPL(182)
UNIMPL(183)
UNIMPL(184)
UNIMPL(185)
UNIMPL(186)
UNIMPL(187)
UNIMPL(188)
UNIMPL(189)
UNIMPL(190)
UNIMPL(191)
UNIMPL(192)
UNIMPL(193)
UNIMPL(194)
UNIMPL(195)
UNIMPL(196)
UNIMPL(197)
UNIMPL(198)
UNIMPL(199)
UNIMPL(200)
UNIMPL(201)
UNIMPL(202)
UNIMPL(203)
UNIMPL(204)
UNIMPL(205)
UNIMPL(206)
UNIMPL(207)
UNIMPL(208)
UNIMPL(209)
UNIMPL(210)
UNIMPL(211)
UNIMPL(212)
UNIMPL(213)
UNIMPL(214)
UNIMPL(215)
UNIMPL(216)
UNIMPL(217)
UNIMPL(218)
UNIMPL(219)
UNIMPL(220)
UNIMPL(221)
UNIMPL(222)
UNIMPL(223)
UNIMPL(224)
UNIMPL(225)
UNIMPL(226)
UNIMPL(227)
UNIMPL(228)
UNIMPL(229)
UNIMPL(230)
UNIMPL(231)
UNIMPL(232)
UNIMPL(233)
UNIMPL(234)
UNIMPL(235)
static void *g_unimpl_table[236] = {
    (void *)jni_unimpl_0,
    (void *)jni_unimpl_1,
    (void *)jni_unimpl_2,
    (void *)jni_unimpl_3,
    (void *)jni_unimpl_4,
    (void *)jni_unimpl_5,
    (void *)jni_unimpl_6,
    (void *)jni_unimpl_7,
    (void *)jni_unimpl_8,
    (void *)jni_unimpl_9,
    (void *)jni_unimpl_10,
    (void *)jni_unimpl_11,
    (void *)jni_unimpl_12,
    (void *)jni_unimpl_13,
    (void *)jni_unimpl_14,
    (void *)jni_unimpl_15,
    (void *)jni_unimpl_16,
    (void *)jni_unimpl_17,
    (void *)jni_unimpl_18,
    (void *)jni_unimpl_19,
    (void *)jni_unimpl_20,
    (void *)jni_unimpl_21,
    (void *)jni_unimpl_22,
    (void *)jni_unimpl_23,
    (void *)jni_unimpl_24,
    (void *)jni_unimpl_25,
    (void *)jni_unimpl_26,
    (void *)jni_unimpl_27,
    (void *)jni_unimpl_28,
    (void *)jni_unimpl_29,
    (void *)jni_unimpl_30,
    (void *)jni_unimpl_31,
    (void *)jni_unimpl_32,
    (void *)jni_unimpl_33,
    (void *)jni_unimpl_34,
    (void *)jni_unimpl_35,
    (void *)jni_unimpl_36,
    (void *)jni_unimpl_37,
    (void *)jni_unimpl_38,
    (void *)jni_unimpl_39,
    (void *)jni_unimpl_40,
    (void *)jni_unimpl_41,
    (void *)jni_unimpl_42,
    (void *)jni_unimpl_43,
    (void *)jni_unimpl_44,
    (void *)jni_unimpl_45,
    (void *)jni_unimpl_46,
    (void *)jni_unimpl_47,
    (void *)jni_unimpl_48,
    (void *)jni_unimpl_49,
    (void *)jni_unimpl_50,
    (void *)jni_unimpl_51,
    (void *)jni_unimpl_52,
    (void *)jni_unimpl_53,
    (void *)jni_unimpl_54,
    (void *)jni_unimpl_55,
    (void *)jni_unimpl_56,
    (void *)jni_unimpl_57,
    (void *)jni_unimpl_58,
    (void *)jni_unimpl_59,
    (void *)jni_unimpl_60,
    (void *)jni_unimpl_61,
    (void *)jni_unimpl_62,
    (void *)jni_unimpl_63,
    (void *)jni_unimpl_64,
    (void *)jni_unimpl_65,
    (void *)jni_unimpl_66,
    (void *)jni_unimpl_67,
    (void *)jni_unimpl_68,
    (void *)jni_unimpl_69,
    (void *)jni_unimpl_70,
    (void *)jni_unimpl_71,
    (void *)jni_unimpl_72,
    (void *)jni_unimpl_73,
    (void *)jni_unimpl_74,
    (void *)jni_unimpl_75,
    (void *)jni_unimpl_76,
    (void *)jni_unimpl_77,
    (void *)jni_unimpl_78,
    (void *)jni_unimpl_79,
    (void *)jni_unimpl_80,
    (void *)jni_unimpl_81,
    (void *)jni_unimpl_82,
    (void *)jni_unimpl_83,
    (void *)jni_unimpl_84,
    (void *)jni_unimpl_85,
    (void *)jni_unimpl_86,
    (void *)jni_unimpl_87,
    (void *)jni_unimpl_88,
    (void *)jni_unimpl_89,
    (void *)jni_unimpl_90,
    (void *)jni_unimpl_91,
    (void *)jni_unimpl_92,
    (void *)jni_unimpl_93,
    (void *)jni_unimpl_94,
    (void *)jni_unimpl_95,
    (void *)jni_unimpl_96,
    (void *)jni_unimpl_97,
    (void *)jni_unimpl_98,
    (void *)jni_unimpl_99,
    (void *)jni_unimpl_100,
    (void *)jni_unimpl_101,
    (void *)jni_unimpl_102,
    (void *)jni_unimpl_103,
    (void *)jni_unimpl_104,
    (void *)jni_unimpl_105,
    (void *)jni_unimpl_106,
    (void *)jni_unimpl_107,
    (void *)jni_unimpl_108,
    (void *)jni_unimpl_109,
    (void *)jni_unimpl_110,
    (void *)jni_unimpl_111,
    (void *)jni_unimpl_112,
    (void *)jni_unimpl_113,
    (void *)jni_unimpl_114,
    (void *)jni_unimpl_115,
    (void *)jni_unimpl_116,
    (void *)jni_unimpl_117,
    (void *)jni_unimpl_118,
    (void *)jni_unimpl_119,
    (void *)jni_unimpl_120,
    (void *)jni_unimpl_121,
    (void *)jni_unimpl_122,
    (void *)jni_unimpl_123,
    (void *)jni_unimpl_124,
    (void *)jni_unimpl_125,
    (void *)jni_unimpl_126,
    (void *)jni_unimpl_127,
    (void *)jni_unimpl_128,
    (void *)jni_unimpl_129,
    (void *)jni_unimpl_130,
    (void *)jni_unimpl_131,
    (void *)jni_unimpl_132,
    (void *)jni_unimpl_133,
    (void *)jni_unimpl_134,
    (void *)jni_unimpl_135,
    (void *)jni_unimpl_136,
    (void *)jni_unimpl_137,
    (void *)jni_unimpl_138,
    (void *)jni_unimpl_139,
    (void *)jni_unimpl_140,
    (void *)jni_unimpl_141,
    (void *)jni_unimpl_142,
    (void *)jni_unimpl_143,
    (void *)jni_unimpl_144,
    (void *)jni_unimpl_145,
    (void *)jni_unimpl_146,
    (void *)jni_unimpl_147,
    (void *)jni_unimpl_148,
    (void *)jni_unimpl_149,
    (void *)jni_unimpl_150,
    (void *)jni_unimpl_151,
    (void *)jni_unimpl_152,
    (void *)jni_unimpl_153,
    (void *)jni_unimpl_154,
    (void *)jni_unimpl_155,
    (void *)jni_unimpl_156,
    (void *)jni_unimpl_157,
    (void *)jni_unimpl_158,
    (void *)jni_unimpl_159,
    (void *)jni_unimpl_160,
    (void *)jni_unimpl_161,
    (void *)jni_unimpl_162,
    (void *)jni_unimpl_163,
    (void *)jni_unimpl_164,
    (void *)jni_unimpl_165,
    (void *)jni_unimpl_166,
    (void *)jni_unimpl_167,
    (void *)jni_unimpl_168,
    (void *)jni_unimpl_169,
    (void *)jni_unimpl_170,
    (void *)jni_unimpl_171,
    (void *)jni_unimpl_172,
    (void *)jni_unimpl_173,
    (void *)jni_unimpl_174,
    (void *)jni_unimpl_175,
    (void *)jni_unimpl_176,
    (void *)jni_unimpl_177,
    (void *)jni_unimpl_178,
    (void *)jni_unimpl_179,
    (void *)jni_unimpl_180,
    (void *)jni_unimpl_181,
    (void *)jni_unimpl_182,
    (void *)jni_unimpl_183,
    (void *)jni_unimpl_184,
    (void *)jni_unimpl_185,
    (void *)jni_unimpl_186,
    (void *)jni_unimpl_187,
    (void *)jni_unimpl_188,
    (void *)jni_unimpl_189,
    (void *)jni_unimpl_190,
    (void *)jni_unimpl_191,
    (void *)jni_unimpl_192,
    (void *)jni_unimpl_193,
    (void *)jni_unimpl_194,
    (void *)jni_unimpl_195,
    (void *)jni_unimpl_196,
    (void *)jni_unimpl_197,
    (void *)jni_unimpl_198,
    (void *)jni_unimpl_199,
    (void *)jni_unimpl_200,
    (void *)jni_unimpl_201,
    (void *)jni_unimpl_202,
    (void *)jni_unimpl_203,
    (void *)jni_unimpl_204,
    (void *)jni_unimpl_205,
    (void *)jni_unimpl_206,
    (void *)jni_unimpl_207,
    (void *)jni_unimpl_208,
    (void *)jni_unimpl_209,
    (void *)jni_unimpl_210,
    (void *)jni_unimpl_211,
    (void *)jni_unimpl_212,
    (void *)jni_unimpl_213,
    (void *)jni_unimpl_214,
    (void *)jni_unimpl_215,
    (void *)jni_unimpl_216,
    (void *)jni_unimpl_217,
    (void *)jni_unimpl_218,
    (void *)jni_unimpl_219,
    (void *)jni_unimpl_220,
    (void *)jni_unimpl_221,
    (void *)jni_unimpl_222,
    (void *)jni_unimpl_223,
    (void *)jni_unimpl_224,
    (void *)jni_unimpl_225,
    (void *)jni_unimpl_226,
    (void *)jni_unimpl_227,
    (void *)jni_unimpl_228,
    (void *)jni_unimpl_229,
    (void *)jni_unimpl_230,
    (void *)jni_unimpl_231,
    (void *)jni_unimpl_232,
    (void *)jni_unimpl_233,
    (void *)jni_unimpl_234,
    (void *)jni_unimpl_235,
};


/* ── JavaVM functions ──────────────────────────────────────────────────── */

static int vm_DestroyJavaVM(void *vm) { (void)vm; return JNI_OK; }
static int vm_AttachCurrentThread(void *vm, void **penv, void *args) {
    (void)vm; (void)args; if (penv) *penv = fake_jni_get_env(); return JNI_OK;
}
static int vm_DetachCurrentThread(void *vm) { (void)vm; return JNI_OK; }
static int vm_GetEnv(void *vm, void **penv, int version) {
    (void)vm; (void)version; if (penv) *penv = fake_jni_get_env(); return JNI_OK;
}
static int vm_AttachCurrentThreadAsDaemon(void *vm, void **penv, void *args) {
    (void)vm; (void)args; if (penv) *penv = fake_jni_get_env(); return JNI_OK;
}

static void init_tables(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;

    for (size_t i = 0; i < sizeof(g_jni_env_table) / sizeof(g_jni_env_table[0]); i++)
        g_jni_env_table[i] = g_unimpl_table[i];

    g_jni_env_table[4]   = (void *)jni_GetVersion;
    g_jni_env_table[6]   = (void *)jni_FindClass;
    g_jni_env_table[10]  = (void *)jni_GetSuperclass;
    g_jni_env_table[11]  = (void *)jni_IsAssignableFrom;
    g_jni_env_table[13]  = (void *)jni_Throw;
    g_jni_env_table[14]  = (void *)jni_ThrowNew;
    g_jni_env_table[15]  = (void *)jni_ExceptionOccurred;
    g_jni_env_table[16]  = (void *)jni_ExceptionDescribe;
    g_jni_env_table[17]  = (void *)jni_ExceptionClear;
    g_jni_env_table[18]  = (void *)jni_FatalError;
    g_jni_env_table[19]  = (void *)jni_PushLocalFrame;
    g_jni_env_table[20]  = (void *)jni_PopLocalFrame;
    g_jni_env_table[21]  = (void *)jni_NewGlobalRef;
    g_jni_env_table[22]  = (void *)jni_DeleteGlobalRef;
    g_jni_env_table[23]  = (void *)jni_DeleteLocalRef;
    g_jni_env_table[24]  = (void *)jni_IsSameObject;
    g_jni_env_table[25]  = (void *)jni_NewLocalRef;
    g_jni_env_table[26]  = (void *)jni_EnsureLocalCapacity;
    g_jni_env_table[27]  = (void *)jni_AllocObject;
    g_jni_env_table[28]  = (void *)jni_NewObject;
    g_jni_env_table[29]  = (void *)jni_NewObjectV;
    g_jni_env_table[30]  = (void *)jni_NewObjectA;
    g_jni_env_table[31]  = (void *)jni_GetObjectClass;
    g_jni_env_table[32]  = (void *)jni_IsInstanceOf;
    g_jni_env_table[33]  = (void *)jni_GetMethodID;

    g_jni_env_table[34]  = (void *)jni_CallObjectMethod;
    g_jni_env_table[35]  = (void *)jni_CallObjectMethodV;
    g_jni_env_table[36]  = (void *)jni_CallObjectMethodA;
    g_jni_env_table[37]  = (void *)jni_CallBooleanMethod;
    g_jni_env_table[38]  = (void *)jni_CallBooleanMethodV;
    g_jni_env_table[39]  = (void *)jni_CallBooleanMethodA;
    g_jni_env_table[49]  = (void *)jni_CallIntMethod;
    g_jni_env_table[50]  = (void *)jni_CallIntMethodV;
    g_jni_env_table[51]  = (void *)jni_CallIntMethodA;
    g_jni_env_table[52]  = (void *)jni_CallLongMethod;
    g_jni_env_table[53]  = (void *)jni_CallLongMethodV;
    g_jni_env_table[54]  = (void *)jni_CallLongMethodA;
    g_jni_env_table[55]  = (void *)jni_CallFloatMethod;
    g_jni_env_table[56]  = (void *)jni_CallFloatMethodV;
    g_jni_env_table[57]  = (void *)jni_CallFloatMethodA;
    g_jni_env_table[58]  = (void *)jni_CallDoubleMethod;
    g_jni_env_table[59]  = (void *)jni_CallDoubleMethodV;
    g_jni_env_table[60]  = (void *)jni_CallDoubleMethodA;
    g_jni_env_table[61]  = (void *)jni_CallVoidMethod;
    g_jni_env_table[62]  = (void *)jni_CallVoidMethodV;
    g_jni_env_table[63]  = (void *)jni_CallVoidMethodA;

    g_jni_env_table[94]  = (void *)jni_GetFieldID;
    g_jni_env_table[95]  = (void *)jni_GetObjectField;
    g_jni_env_table[96]  = (void *)jni_GetBooleanField;
    g_jni_env_table[100] = (void *)jni_GetIntField;
    g_jni_env_table[101] = (void *)jni_GetLongField;
    g_jni_env_table[102] = (void *)jni_GetFloatField;

    g_jni_env_table[113] = (void *)jni_GetStaticMethodID;
    g_jni_env_table[114] = (void *)jni_CallStaticObjectMethod;
    g_jni_env_table[115] = (void *)jni_CallStaticObjectMethodV;
    g_jni_env_table[116] = (void *)jni_CallStaticObjectMethodA;
    g_jni_env_table[117] = (void *)jni_CallStaticBooleanMethod;
    g_jni_env_table[118] = (void *)jni_CallStaticBooleanMethodV;
    g_jni_env_table[119] = (void *)jni_CallStaticBooleanMethodA;
    g_jni_env_table[129] = (void *)jni_CallStaticIntMethod;
    g_jni_env_table[130] = (void *)jni_CallStaticIntMethodV;
    g_jni_env_table[131] = (void *)jni_CallStaticIntMethodA;
    g_jni_env_table[141] = (void *)jni_CallStaticVoidMethod;
    g_jni_env_table[142] = (void *)jni_CallStaticVoidMethodV;
    g_jni_env_table[143] = (void *)jni_CallStaticVoidMethodA;

    g_jni_env_table[144] = (void *)jni_GetStaticFieldID;
    g_jni_env_table[145] = (void *)jni_GetStaticObjectField;
    g_jni_env_table[150] = (void *)jni_GetStaticIntField;

    g_jni_env_table[163] = (void *)jni_NewString;
    g_jni_env_table[164] = (void *)jni_GetStringLength;
    g_jni_env_table[165] = (void *)jni_GetStringChars;
    g_jni_env_table[166] = (void *)jni_ReleaseStringChars;
    g_jni_env_table[167] = (void *)jni_NewStringUTF;
    g_jni_env_table[168] = (void *)jni_GetStringUTFLength;
    g_jni_env_table[169] = (void *)jni_GetStringUTFChars;
    g_jni_env_table[170] = (void *)jni_ReleaseStringUTFChars;

    g_jni_env_table[171] = (void *)jni_GetArrayLength;
    g_jni_env_table[172] = (void *)jni_NewObjectArray;
    g_jni_env_table[173] = (void *)jni_GetObjectArrayElement;
    g_jni_env_table[174] = (void *)jni_SetObjectArrayElement;
    g_jni_env_table[176] = (void *)jni_NewByteArray;
    g_jni_env_table[179] = (void *)jni_NewIntArray;
    g_jni_env_table[184] = (void *)jni_GetArrayElements;
    g_jni_env_table[187] = (void *)jni_GetArrayElements;
    g_jni_env_table[192] = (void *)jni_ReleaseArrayElements;
    g_jni_env_table[195] = (void *)jni_ReleaseArrayElements;
    g_jni_env_table[200] = (void *)jni_GetArrayRegion;
    g_jni_env_table[203] = (void *)jni_GetArrayRegion;
    g_jni_env_table[208] = (void *)jni_SetArrayRegion;
    g_jni_env_table[211] = (void *)jni_SetArrayRegion;

    g_jni_env_table[215] = (void *)jni_RegisterNatives;
    g_jni_env_table[216] = (void *)jni_UnregisterNatives;
    g_jni_env_table[219] = (void *)jni_GetJavaVM;
    g_jni_env_table[226] = (void *)jni_NewWeakGlobalRef;
    g_jni_env_table[227] = (void *)jni_DeleteWeakGlobalRef;
    g_jni_env_table[228] = (void *)jni_ExceptionCheck;

    g_java_vm_table[3] = (void *)vm_DestroyJavaVM;
    g_java_vm_table[4] = (void *)vm_AttachCurrentThread;
    g_java_vm_table[5] = (void *)vm_DetachCurrentThread;
    g_java_vm_table[6] = (void *)vm_GetEnv;
    g_java_vm_table[7] = (void *)vm_AttachCurrentThreadAsDaemon;
}

void *fake_jni_get_vm(void) {
    init_tables();
    return &g_java_vm_ptr;
}

void *fake_jni_get_env(void) {
    init_tables();
    return &g_jni_env_ptr;
}
