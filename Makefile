# sf2-loader -- Shadow Fight 2 (Unity 2020.3 IL2CPP, armeabi-v7a) para R36S/R36 Ultra X (dArkOS, armhf)
CROSS ?= arm-linux-gnueabihf-
CC    := $(CROSS)gcc

CFLAGS  := -march=armv7-a -mfpu=neon -mfloat-abi=hard -O1 -g -fno-omit-frame-pointer \
           -Wall -Wno-unused-function -Isrc
LDFLAGS := -ldl -lpthread -lm -rdynamic

C_SRCS := src/main.c src/m0_main.c src/m1_main.c src/so_util.c \
          src/bionic_shims.c src/ndk_stubs.c src/fake_jni.c
ASM_SRCS := src/bionic_setjmp.S
HDRS := src/so_util.h src/bionic_shims.h src/softfp_thunks.h src/ndk_stubs.h src/fake_jni.h

sf2loader: $(C_SRCS) $(ASM_SRCS) src/softfp_thunks.c $(HDRS)
	$(CC) $(CFLAGS) -fno-builtin -c src/softfp_thunks.c -o src/softfp_thunks.o
	$(CC) $(CFLAGS) -o $@ $(C_SRCS) $(ASM_SRCS) src/softfp_thunks.o $(LDFLAGS)

clean:
	rm -f sf2loader sf2loader-host src/*.o
.PHONY: clean
