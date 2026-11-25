# sf2-loader — Shadow Fight 2 (Unity 2020.3.18f1 IL2CPP, armeabi-v7a) para R36S / R36 Ultra X

Loader de `.so` Android para Linux armhf (dArkOS). Cada marco (`M0`, `M1`, `M2`, ...) é uma etapa testável.

## M0 — diagnóstico (validado)
Testa funções de tempo da glibc, símbolos de unwind/aeabi, carga e relocação dos três `.so`, RAM usada e lista de imports.

## M1 — shims Bionic, thunks softfp, `.init_array` e `JNI_OnLoad` (atual)
Ainda não abre a janela do jogo. Testa:
1. Resolução de todos os símbolos importados pelos 3 `.so` usando shims Bionic$\leftrightarrow$glibc (`__sF`, `FILE*`, `pthread_*`, `sem_*`, `stat`, `dirent`, `sigaction`, `setjmp`, `dlopen`/`dlsym`, `__gnu_Unwind_Find_exidx`, etc.), thunks `softfp`$\rightarrow$`hardfp` para `libm` e stubs iniciais de NDK/EGL.
2. Execução dos construtores `.init_array` de `libmain.so`, `libunity.so` e `libil2cpp.so`.
3. Chamada a `JNI_OnLoad` de cada módulo usando `JavaVM`/`JNIEnv` mínimo, listando os métodos nativos registrados pela Unity.

### Compilar (GitHub Actions)
1. Envie os arquivos desta pasta para o seu repositório no GitHub (incluindo `.github/workflows/build.yml`).
2. Na aba **Actions** → **Build sf2loader (armhf)**, aguarde finalizar e baixe o artifact **sf2-m1**.

### Instalar no aparelho
1. Extraia o `sf2-m1.zip` dentro da pasta `ports` do cartão SD (mantendo `libmain.so`, `libunity.so` e `libil2cpp.so` em `ports/sf2/libs/`).
2. Reinicie o EmulationStation, abra **ShadowFight2-M1** em Ports e aguarde voltar ao menu.
3. Envie `ports/sf2_m1_log.txt`.
