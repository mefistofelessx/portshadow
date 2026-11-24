# sf2-loader — Shadow Fight 2 (Unity 2020.3 IL2CPP, armeabi-v7a) para R36S / R36 Ultra X

Loader de `.so` Android para Linux armhf (dArkOS). Cada marco (M0, M1, ...) é uma etapa testável.

## M0 — diagnóstico (este pacote)
Não inicia o jogo. Testa: funções de tempo da glibc, símbolos de unwind/aeabi, carga e relocação dos
três `.so`, RAM usada e lista de imports. O resultado vai para `ports/sf2_m0_log.txt`.

### Compilar (GitHub, sem instalar nada)
1. Crie um repositório novo (privado serve) em github.com.
2. Envie o conteúdo desta pasta (**Add file → Upload files**; arraste tudo, inclusive a pasta `.github`).
   Se a pasta `.github` não subir, use **Add file → Create new file**, digite
   `.github/workflows/build.yml` como nome e cole o conteúdo do arquivo.
3. Aba **Actions** → "Build sf2loader (armhf)" → rode (ou ele roda sozinho após o upload).
4. Quando ficar verde, abra a execução e baixe o artefato **sf2-m0** (um zip).

### Instalar no aparelho
1. Extraia o `sf2-m0.zip` dentro da pasta `ports` do cartão.
2. Copie `libmain.so`, `libunity.so` e `libil2cpp.so` (de `lib/armeabi-v7a/` do seu APK) para `ports/sf2/libs/`.
3. Reinicie o EmulationStation, abra **ShadowFight2-M0** em Ports e aguarde voltar ao menu.
4. Envie `ports/sf2_m0_log.txt`.

## Origem do código
`src/so_util.[ch]`: carregador de ELF derivado de TheOfficialFloW/gtactw_vita (MIT), via
gw3-portmaster; alterado aqui para registrar PT_ARM_EXIDX e o tamanho mapeado.
O repositório gw3-portmaster não traz arquivo de licença próprio: antes de publicar este projeto,
confirmar a licença com o autor ou reescrever os trechos derivados.

Não inclua o APK, `.so` nem dados do jogo no repositório. O jogador fornece o próprio APK.
