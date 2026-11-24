# sf2-loader -- Shadow Fight 2 (Unity 2020.3 IL2CPP, armeabi-v7a) para R36S/R36 Ultra X (dArkOS, armhf)
CROSS ?= arm-linux-gnueabihf-
CC    := $(CROSS)gcc

CFLAGS  := -march=armv7-a -mfpu=neon -mfloat-abi=hard -O1 -g -fno-omit-frame-pointer \
           -Wall -Wno-unused-function -Isrc
LDFLAGS := -ldl -lpthread -lm -rdynamic

SRCS := src/m0_main.c src/so_util.c

sf2loader: $(SRCS) src/so_util.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# teste de sintaxe/execucao no PC (32 bits x86) sem executar o codigo ARM do jogo
host-test: $(SRCS) src/so_util.h
	gcc -m32 -O1 -g -Wall -Wno-unused-function -Isrc -o sf2loader-host $(SRCS) -ldl -lpthread -lm

clean:
	rm -f sf2loader sf2loader-host
.PHONY: clean host-test
