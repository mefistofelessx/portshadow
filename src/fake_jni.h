#ifndef FAKE_JNI_H
#define FAKE_JNI_H

#include <stdint.h>

#define JNI_VERSION_1_6 0x00010006
#define JNI_OK          0
#define JNI_ERR         (-1)
#define JNI_EDETACHED   (-2)
#define JNI_EVERSION    (-3)

typedef struct {
    const char *name;
    const char *signature;
    void       *fnPtr;
} JNINativeMethod;

void *fake_jni_get_vm(void);
void *fake_jni_get_env(void);

/* Configure Android paths and display size returned to UnityPlayer via JNI. */
void fake_jni_set_config(const char *apk_path, const char *save_dir, int w, int h);

/* Directory containing libmain/libunity/libil2cpp (used by ClassLoader.findLibrary). */
void fake_jni_set_libs_dir(const char *libs_dir);

/* Create a dummy jobject with a known Java class name (e.g. Context, Surface). */
void *fake_jni_make_object(const char *class_name);

/* Look up a function pointer registered via JNI RegisterNatives. */
void *fake_jni_find_native(const char *class_name, const char *method_name);

#endif /* FAKE_JNI_H */
