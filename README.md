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

## M2 — janela SDL2 + loop Unity (correcoes desta revisao)
- **Loop infinito de "main thread is trapped"**: o handler de SIGSEGV do Unity retorna e a instrucao falha de novo. O `sigaction` do shim agora envolve os handlers de sinais fatais; na segunda falha identica o loader encerra (`[LOADER] Falha fatal repetida`).
- **`UnityPlayer.kill()`** agora encerra o processo (codigo 3) em vez de retornar.
- **Diagnostico**: `AlertDialog$Builder.setTitle/setMessage` imprimem o texto do erro fatal do Unity como `[JNI][DIALOGO]`; `FatalError` do JNI tambem; funcoes JNIEnv nao implementadas sao logadas pelo indice; falhas de `stat`/`fopen` aparecem como `[FS]`.
- **Stubs JNI novos**: `getClass`, `ClassLoader.findLibrary`, `getObbDir(s)`, `SharedPreferences` (+Editor), `Looper.getMainLooper`, `JNIBridge.newInterfaceProxy` (proxy nao-nulo), `Environment.getExternalStorageState` ("mounted"), `String.equals`, `File.exists/mkdirs`, builders de `Intent`/`AlertDialog`, excecoes pendentes (`Throw`, `ThrowNew`, `ExceptionCheck`).
- **`AssetManager.open`** le de verdade `assets/<nome>` de dentro do APK (zip stored/deflate, inflate via `libz.so.1`) e `Scanner.useDelimiter().next()` devolve o conteudo. Se o asset nao existe, lanca `IOException` pendente.
- **Launcher M2**: log limitado a 5 MB (`head -c`), `timeout` de 180 s, `XDG_RUNTIME_DIR`, rotacao de logs e anexo do tombstone em `sf2_m2_log.txt`.
- **Looper/Handler/Choreographer emulados** (`fake_jni.c`): o Unity espera um `doFrame()` de VSync (via proxies `bitter.jnibridge` -> `JNIBridge.invoke`) e travava no 3o frame em `pthread_cond_wait`. Uma thread faz o papel do Looper (`Handler.post/sendMessage/obtainMessage`) e do Choreographer (~60 Hz).
- IDs de metodo/campo estaveis (cache), parsing de argumentos JNI guiado pela assinatura (jlong em ARM32) e excecao pendente por thread.
- Diagnostico do loop de render: heartbeat `[HB]` a cada 2 s e, se travar, dump de registradores + varredura da pilha (ate 3 vezes).
