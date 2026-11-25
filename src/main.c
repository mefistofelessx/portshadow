/* main.c -- Entry point for sf2loader (--m0 diagnostic, --m1 init/JNI test). */

#include <stdio.h>
#include <string.h>

int m0_run(const char *dir);
int m1_run(const char *dir);

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--m0") == 0)
        return m0_run(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "--m1") == 0)
        return m1_run(argv[2]);

    fprintf(stderr,
            "uso:\n"
            "  %s --m0 <pasta com .so>   (diagnostico de carga e simbolos)\n"
            "  %s --m1 <pasta com .so>   (resolve shims, init_array e JNI_OnLoad)\n",
            argv[0], argv[0]);
    return 2;
}
