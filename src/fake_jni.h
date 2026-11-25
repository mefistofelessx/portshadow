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

#endif /* FAKE_JNI_H */
