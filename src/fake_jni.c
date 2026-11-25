/* fake_jni.c -- JavaVM + JNIEnv implementation for M1 and M2 UnityPlayer boot. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ── Class / Member / Ref handling ─────────────────────────────────────── */

static int jni_GetVersion(void *env) { (void)env; return JNI_VERSION_1_6; }

static void *jni_FindClass(void *env, const char *name) {
    (void)env;
    void *c = alloc_jobj(JTAG_CLASS, name ? name : "java/lang/Object", NULL);
    printf("   [JNI] FindClass(\"%s\") -> %p\n", name ? name : "(null)", c);
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

static unsigned char jni_ExceptionCheck(void *env) { (void)env; return 0; }
static void *jni_ExceptionOccurred(void *env) { (void)env; return NULL; }
static void  jni_ExceptionDescribe(void *env) { (void)env; }
static void  jni_ExceptionClear(void *env) { (void)env; }

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
    printf("   [JNI] GetMethodID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetStaticMethodID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    printf("   [JNI] GetStaticMethodID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    printf("   [JNI] GetFieldID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

static void *jni_GetStaticFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    void *m = alloc_member(clazz, name, sig);
    printf("   [JNI] GetStaticFieldID(%s, \"%s\", \"%s\")\n",
           obj_class_name(clazz), name ? name : "?", sig ? sig : "?");
    return m;
}

/* ── Method & Field Dispatch ───────────────────────────────────────────── */

static void *dispatch_obj_method(void *env, void *obj, fake_member_t *m) {
    if (!m || m->tag != JTAG_MEMID) return NULL;
    const char *n = m->name;
    const char *c = m->cls;
    fake_jobj_t *jo = (obj && *(uint32_t *)obj == JTAG_OBJ) ? (fake_jobj_t *)obj : NULL;

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
    if (strcmp(n, "getAssets") == 0)
        return fake_jni_make_object("android/content/res/AssetManager");
    if (strcmp(n, "getResources") == 0)
        return fake_jni_make_object("android/content/res/Resources");
    if (strcmp(n, "getDisplayMetrics") == 0)
        return fake_jni_make_object("android/util/DisplayMetrics");
    if (strcmp(n, "getConfiguration") == 0)
        return fake_jni_make_object("android/content/res/Configuration");
    if (strcmp(n, "getFilesDir") == 0 || strcmp(n, "getCacheDir") == 0 ||
        strcmp(n, "getCodeCacheDir") == 0 || strcmp(n, "getNoBackupFilesDir") == 0 ||
        strcmp(n, "getExternalFilesDir") == 0 || strcmp(n, "getExternalCacheDir") == 0)
        return alloc_jobj(JTAG_OBJ, "java/io/File", g_save_dir);
    if (strcmp(n, "getExternalFilesDirs") == 0 || strcmp(n, "getExternalCacheDirs") == 0) {
        void *arr = jni_NewObjectArray(env, 1, NULL, alloc_jobj(JTAG_OBJ, "java/io/File", g_save_dir));
        return arr;
    }
    if (strcmp(n, "getAbsolutePath") == 0 || strcmp(n, "getPath") == 0 || strcmp(n, "getCanonicalPath") == 0) {
        return jni_NewStringUTF(env, (jo && jo->extra[0]) ? jo->extra : g_save_dir);
    }
    if (strcmp(n, "getSystemService") == 0)
        return fake_jni_make_object("android/view/WindowManager");
    if (strcmp(n, "getDefaultDisplay") == 0)
        return fake_jni_make_object("android/view/Display");
    if (strcmp(n, "getWindowManager") == 0)
        return fake_jni_make_object("android/view/WindowManager");
    if (strcmp(n, "getClassLoader") == 0)
        return fake_jni_make_object("java/lang/ClassLoader");
    if (strcmp(n, "getLanguage") == 0)
        return jni_NewStringUTF(env, "en");
    if (strcmp(n, "getCountry") == 0)
        return jni_NewStringUTF(env, "US");
    if (strcmp(n, "toString") == 0)
        return jni_NewStringUTF(env, c);

    printf("   [JNI] CallObjectMethod nao tratado: %s.%s %s\n", c, n, m->sig);
    return NULL;
}

static void *jni_NewObjectV(void *env, void *cls, void *mid, va_list ap) {
    (void)env; (void)mid; (void)ap;
    return fake_jni_make_object(obj_class_name(cls));
}
static void *jni_NewObjectA(void *env, void *cls, void *mid, const void *args) {
    (void)env; (void)mid; (void)args;
    return fake_jni_make_object(obj_class_name(cls));
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
    (void)ap;
    return dispatch_obj_method(env, obj, (fake_member_t *)mid);
}
static void *jni_CallObjectMethodA(void *env, void *obj, void *mid, const void *args) {
    (void)args;
    return dispatch_obj_method(env, obj, (fake_member_t *)mid);
}
static void *jni_CallObjectMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    void *r = jni_CallObjectMethodV(env, obj, mid, ap);
    va_end(ap); return r;
}

static int jni_CallIntMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env; (void)obj; (void)ap;
    fake_member_t *m = (fake_member_t *)mid;
    if (!m || m->tag != JTAG_MEMID) return 0;
    if (strcmp(m->name, "getWidth") == 0)  return g_disp_w;
    if (strcmp(m->name, "getHeight") == 0) return g_disp_h;
    if (strcmp(m->name, "getRotation") == 0) return 0;
    printf("   [JNI] CallIntMethod: %s.%s %s -> 0\n", m->cls, m->name, m->sig);
    return 0;
}
static int jni_CallIntMethodA(void *env, void *obj, void *mid, const void *args) {
    va_list dummy; (void)args; memset(&dummy, 0, sizeof(dummy));
    return jni_CallIntMethodV(env, obj, mid, dummy);
}
static int jni_CallIntMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    int r = jni_CallIntMethodV(env, obj, mid, ap);
    va_end(ap); return r;
}

static unsigned char jni_CallBooleanMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env; (void)obj; (void)ap;
    fake_member_t *m = (fake_member_t *)mid;
    if (m && m->tag == JTAG_MEMID)
        printf("   [JNI] CallBooleanMethod: %s.%s %s -> 0\n", m->cls, m->name, m->sig);
    return 0;
}
static unsigned char jni_CallBooleanMethodA(void *env, void *obj, void *mid, const void *args) {
    (void)env; (void)obj; (void)mid; (void)args; return 0;
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

static void jni_CallVoidMethodV(void *env, void *obj, void *mid, va_list ap) {
    (void)env; (void)obj; (void)ap;
    fake_member_t *m = (fake_member_t *)mid;
    if (m && m->tag == JTAG_MEMID)
        printf("   [JNI] CallVoidMethod: %s.%s %s\n", m->cls, m->name, m->sig);
}
static void jni_CallVoidMethodA(void *env, void *obj, void *mid, const void *a) {
    (void)env; (void)obj; (void)mid; (void)a;
}
static void jni_CallVoidMethod(void *env, void *obj, void *mid, ...) {
    va_list ap; va_start(ap, mid);
    jni_CallVoidMethodV(env, obj, mid, ap);
    va_end(ap);
}

static void *jni_CallStaticObjectMethodV(void *env, void *cls, void *mid, va_list ap) {
    (void)ap;
    fake_member_t *m = (fake_member_t *)mid;
    if (!m || m->tag != JTAG_MEMID) return NULL;
    if (strcmp(m->name, "getDefault") == 0)
        return fake_jni_make_object("java/util/Locale");
    if (strcmp(m->name, "getExternalStorageDirectory") == 0 || strcmp(m->name, "getDownloadCacheDirectory") == 0)
        return alloc_jobj(JTAG_OBJ, "java/io/File", g_save_dir);
    printf("   [JNI] CallStaticObjectMethod: %s.%s %s -> NULL\n",
           obj_class_name(cls), m->name, m->sig);
    return NULL;
}
static void *jni_CallStaticObjectMethodA(void *env, void *cls, void *mid, const void *a) {
    va_list dummy; (void)a; memset(&dummy, 0, sizeof(dummy));
    return jni_CallStaticObjectMethodV(env, cls, mid, dummy);
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
    printf("   [JNI] GetObjectField: %s.%s %s -> NULL\n", m->cls, m->name, m->sig);
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
    return NULL;
}

static int jni_GetStaticIntField(void *env, void *cls, void *fid) {
    (void)env; (void)cls;
    fake_member_t *m = (fake_member_t *)fid;
    if (m && m->tag == JTAG_MEMID && strcmp(m->name, "SDK_INT") == 0) return 29;
    return 0;
}

static int jni_RegisterNatives(void *env, void *clazz, const JNINativeMethod *methods, int nMethods) {
    (void)env;
    const char *cname = obj_class_name(clazz);
    printf("   [JNI] RegisterNatives(%s, %d metodos):\n", cname, nMethods);
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
        g_jni_env_table[i] = (void *)jni_unimpl_stub;

    g_jni_env_table[4]   = (void *)jni_GetVersion;
    g_jni_env_table[6]   = (void *)jni_FindClass;
    g_jni_env_table[10]  = (void *)jni_GetSuperclass;
    g_jni_env_table[11]  = (void *)jni_IsAssignableFrom;
    g_jni_env_table[15]  = (void *)jni_ExceptionOccurred;
    g_jni_env_table[16]  = (void *)jni_ExceptionDescribe;
    g_jni_env_table[17]  = (void *)jni_ExceptionClear;
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
