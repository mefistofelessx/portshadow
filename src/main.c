/* main.c -- Entry point for sf2loader (--m0, --m1, --m2). */

#include <stdio.h>
#include <string.h>

int m0_run(const char *dir);
int m1_run(const char *dir);
int m2_run(const char *libs_dir, const char *apk_path, const char *save_dir);

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--m0") == 0)
        return m0_run(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "--m1") == 0)
        return m1_run(argv[2]);
    if (argc >= 5 && strcmp(argv[1], "--m2") == 0)
        return m2_run(argv[2], argv[3], argv[4]);

    fprintf(stderr,
            "uso:\n"
            "  %s --m0 <pasta com .so>                      (diagnostico de carga e simbolos)\n"
            "  %s --m1 <pasta com .so>                      (resolve shims, init_array e JNI_OnLoad)\n"
            "  %s --m2 <pasta com .so> <apk> <pasta save>   (SDL2 + EGL + loop Unity)\n",
            argv[0], argv[0], argv[0]);
    return 2;
}
