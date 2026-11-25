/* fake_jni.c -- Minimal JavaVM + JNIEnv table for M1 JNI_OnLoad probing. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_jni.h"

typedef struct {
    char name[128];
} fake_jclass_t;

typedef struct {
    char *str;
} fake_jstring_t;

static void *g_jni_env_table[236];
static void *g_jni_env_ptr = g_jni_env_table;

static void *g_java_vm_table[8];
static void *g_java_vm_ptr = g_java_vm_table;

/* ── JNIEnv functions ──────────────────────────────────────────────────── */

static int jni_GetVersion(void *env) {
    (void)env;
    return JNI_VERSION_1_6;
}

static void *jni_FindClass(void *env, const char *name) {
    (void)env;
    fake_jclass_t *c = calloc(1, sizeof(*c));
    if (c && name) {
        strncpy(c->name, name, sizeof(c->name) - 1);
    }
    printf("   [JNI] FindClass(\"%s\") -> %p\n", name ? name : "(null)", (void *)c);
    return c;
}

static void *jni_GetObjectClass(void *env, void *obj) {
    (void)env; (void)obj;
    return jni_FindClass(env, "java/lang/Object");
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

static void *jni_GetMethodID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    fake_jclass_t *c = (fake_jclass_t *)clazz;
    printf("   [JNI] GetMethodID(%s, \"%s\", \"%s\")\n",
           c ? c->name : "?", name ? name : "?", sig ? sig : "?");
    return (void *)(uintptr_t)0x1001;
}

static void *jni_GetStaticMethodID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env;
    fake_jclass_t *c = (fake_jclass_t *)clazz;
    printf("   [JNI] GetStaticMethodID(%s, \"%s\", \"%s\")\n",
           c ? c->name : "?", name ? name : "?", sig ? sig : "?");
    return (void *)(uintptr_t)0x1002;
}

static void *jni_GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env; (void)clazz; (void)name; (void)sig;
    return (void *)(uintptr_t)0x1003;
}

static void *jni_GetStaticFieldID(void *env, void *clazz, const char *name, const char *sig) {
    (void)env; (void)clazz; (void)name; (void)sig;
    return (void *)(uintptr_t)0x1004;
}

static void *jni_NewStringUTF(void *env, const char *utf) {
    (void)env;
    fake_jstring_t *s = malloc(sizeof(*s));
    if (s) s->str = strdup(utf ? utf : "");
    return s;
}

static const char *jni_GetStringUTFChars(void *env, void *jstr, unsigned char *isCopy) {
    (void)env;
    if (isCopy) *isCopy = 0;
    fake_jstring_t *s = (fake_jstring_t *)jstr;
    return (s && s->str) ? s->str : "";
}

static void jni_ReleaseStringUTFChars(void *env, void *jstr, const char *chars) {
    (void)env; (void)jstr; (void)chars;
}

static int jni_RegisterNatives(void *env, void *clazz, const JNINativeMethod *methods, int nMethods) {
    (void)env;
    fake_jclass_t *c = (fake_jclass_t *)clazz;
    printf("   [JNI] RegisterNatives(%s, %d metodos):\n", c ? c->name : "?", nMethods);
    for (int i = 0; i < nMethods; i++) {
        printf("         - %s %s -> %p\n",
               methods[i].name ? methods[i].name : "?",
               methods[i].signature ? methods[i].signature : "?",
               methods[i].fnPtr);
    }
    return JNI_OK;
}

static int jni_UnregisterNatives(void *env, void *clazz) {
    (void)env; (void)clazz;
    return JNI_OK;
}

static int jni_GetJavaVM(void *env, void **vm) {
    (void)env;
    if (vm) *vm = fake_jni_get_vm();
    return JNI_OK;
}

static int jni_unimpl_stub(void) {
    return 0;
}

/* ── JavaVM functions ──────────────────────────────────────────────────── */

static int vm_DestroyJavaVM(void *vm) { (void)vm; return JNI_OK; }

static int vm_AttachCurrentThread(void *vm, void **penv, void *args) {
    (void)vm; (void)args;
    if (penv) *penv = fake_jni_get_env();
    return JNI_OK;
}

static int vm_DetachCurrentThread(void *vm) { (void)vm; return JNI_OK; }

static int vm_GetEnv(void *vm, void **penv, int version) {
    (void)vm; (void)version;
    if (penv) *penv = fake_jni_get_env();
    return JNI_OK;
}

static int vm_AttachCurrentThreadAsDaemon(void *vm, void **penv, void *args) {
    (void)vm; (void)args;
    if (penv) *penv = fake_jni_get_env();
    return JNI_OK;
}

static void init_tables(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;

    for (size_t i = 0; i < sizeof(g_jni_env_table) / sizeof(g_jni_env_table[0]); i++)
        g_jni_env_table[i] = (void *)jni_unimpl_stub;

    /* Standard JNIEnv slot indices (JNI 1.6) */
    g_jni_env_table[4]   = (void *)jni_GetVersion;
    g_jni_env_table[6]   = (void *)jni_FindClass;
    g_jni_env_table[15]  = (void *)jni_ExceptionOccurred;
    g_jni_env_table[16]  = (void *)jni_ExceptionDescribe;
    g_jni_env_table[17]  = (void *)jni_ExceptionClear;
    g_jni_env_table[19]  = (void *)jni_PushLocalFrame;
    g_jni_env_table[20]  = (void *)jni_PopLocalFrame;
    g_jni_env_table[21]  = (void *)jni_NewGlobalRef;
    g_jni_env_table[22]  = (void *)jni_DeleteGlobalRef;
    g_jni_env_table[23]  = (void *)jni_DeleteLocalRef;
    g_jni_env_table[25]  = (void *)jni_NewLocalRef;
    g_jni_env_table[26]  = (void *)jni_EnsureLocalCapacity;
    g_jni_env_table[31]  = (void *)jni_GetObjectClass;
    g_jni_env_table[33]  = (void *)jni_GetMethodID;
    g_jni_env_table[94]  = (void *)jni_GetFieldID;
    g_jni_env_table[113] = (void *)jni_GetStaticMethodID;
    g_jni_env_table[144] = (void *)jni_GetStaticFieldID;
    g_jni_env_table[167] = (void *)jni_NewStringUTF;
    g_jni_env_table[169] = (void *)jni_GetStringUTFChars;
    g_jni_env_table[170] = (void *)jni_ReleaseStringUTFChars;
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
