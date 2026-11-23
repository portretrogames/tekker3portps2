# Makefile — Tekken 3 PS2 (ps2sdk). Uso no WSL/Linux:
#   export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
#   export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
#   make -j4        -> tekken3.elf
#   make pacote     -> build/pacote/APPS/TEKKEN3 (ELF sem depuração, para o pendrive)
#   make test       -> testes do runtime no PC (gcc normal)
#
# O código de ../gerado NÃO é editado: o caminho rápido de memória entra
# por "-include runtime/fast_memory.h" só nesses arquivos.
# Sem PSX_ENABLE_BLOCK_CYCLES (contagem de ciclos) — de propósito.

EE_BIN = tekken3.elf
REF_INC = ../referencia/Tekken3Recompiled/psxrecomp/runtime/include
EE_INCS = -I../gerado -I$(REF_INC) -Iruntime -I../kit/include -I$(PS2DEV)/gsKit/include
EE_CFLAGS = -O2 -G0 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1
EE_DBGINFOFLAGS =
EE_WARNFLAGS =

GAME_SHARDS := $(addprefix ../gerado/SLUS_004.02_full_,$(addsuffix .c,$(shell seq -w 0 44)))
GAME_SOURCES := $(GAME_SHARDS) ../gerado/SLUS_004.02_dispatch.c
GAME_OBJS := $(patsubst ../gerado/%.c,build/%.o,$(GAME_SOURCES))
# Bibliotecas compartilhadas (../kit): controle, memory card, armazenamento, som
KIT_DIR := ../kit
KIT_BUILD := build
include $(KIT_DIR)/kit.mk
# Rotinas do jogo traduzidas para C nativo (tools/nativo.py), lidas do .bin
BIN_JOGO ?= Tekken 3 (USA) (Track 1).bin
RUNTIME_OBJS := build/nativo.o build/nativo_ganchos.o build/main.o build/ram.o build/memory.o build/io.o build/gpu.o build/gs_render.o build/video_gs.o build/abertura.o build/cartao_ps1.o build/cartao_ps2.o build/mdec.o build/cdrom.o build/pad.o $(KIT_OBJS) build/interp.o build/codigo.o build/bios_hle.o build/gte.o build/stubs.o build/som.o build/spu.o
EE_OBJS := $(GAME_OBJS) $(RUNTIME_OBJS)
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_LIBS += -lgskit -ldmakit $(KIT_LIBS) -ldebug -lm

# Pasta pronta para o pendrive (OPL: mass:/APPS/TEKKEN3/), com o ELF sem as
# informações de depuração. O .bin do jogo NÃO é incluído: o usuário copia o dele.
pacote: all
	@rm -rf build/pacote && mkdir -p build/pacote/APPS/TEKKEN3
	$(EE_STRIP) -R .pdr -R .comment -R .mdebug.eabi64 -o build/pacote/APPS/TEKKEN3/tekken3.elf $(EE_BIN)
	printf 'title=Tekken 3 (port nativo)\nboot=tekken3.elf\n' > build/pacote/APPS/TEKKEN3/title.cfg
	cp pacote/LEIA-ME.txt build/pacote/APPS/TEKKEN3/LEIA-ME.txt
	cp build/pacote/APPS/TEKKEN3/tekken3.elf ../tekken3.elf
	python3 tools/montar_criador.py build/pacote/Criar_Tekken3_PS2.html
	@echo "Pacote em build/pacote (APPS/TEKKEN3 + Criar_Tekken3_PS2.html); copia do ELF em ../tekken3.elf"

.DEFAULT_GOAL := all
all: $(EE_BIN) build/mult1_ok

# V60: o RTPT (runtime/gte.c, prod3_par/mul32_par) usa o multiplicador 1 do EE
# (HI1/LO1) sem que o GCC saiba. Se outra parte do ELF passar a usar o
# multiplicador 1 (outro compilador, biblioteca nova), o build para aqui.
EE_OBJDUMP ?= mips64r5900el-ps2-elf-objdump
# instruções que escrevem em HI1/LO1 (as que só leem, como PMFHL, não atrapalham)
MULT1_RE := [[:space:]](mult1|multu1|madd1|maddu1|div1|divu1|mthi1|mtlo1|mfhi1|mflo1|pmthl\.[a-z]+|pmultu?[wh]|pmaddu?[wh]|pmsubu?[wh]|phm(ad|sb)h|pdiv(u?w|bw))[[:space:]]
build/mult1_ok: $(EE_BIN) build/gte.o
	@rm -f $@; \
	 $(EE_OBJDUMP) -d $(EE_BIN) > $@.elf.txt && $(EE_OBJDUMP) -d build/gte.o > $@.gte.txt || \
	   { echo "erro: $(EE_OBJDUMP) falhou (conferência do multiplicador 1)"; rm -f $@.*.txt; exit 1; }; \
	 n_elf=$$(grep -cE '$(MULT1_RE)' $@.elf.txt); n_gte=$$(grep -cE '$(MULT1_RE)' $@.gte.txt); \
	 rm -f $@.elf.txt $@.gte.txt; \
	 if [ "$$n_gte" -eq 0 ]; then \
	   echo "erro: nenhuma instrução do multiplicador 1 em gte.o (a conferência não achou o que procura)"; exit 1; fi; \
	 if [ "$$n_elf" != "$$n_gte" ]; then \
	   echo "erro: o multiplicador 1 do EE aparece fora de gte.o ($$n_elf no ELF, $$n_gte em gte.o): ver gte.c, prod3_par"; \
	   exit 1; fi; \
	 echo "mult1: só em gte.o ($$n_gte instruções)" > $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

# Cópia do código gerado com os ajustes de tools/transformar_gerado.py
# (gerado/ não é editado).
build/r/%.c: ../gerado/%.c tools/transformar_gerado.py tools/nativas.txt
	@mkdir -p $(@D)
	python3 tools/transformar_gerado.py $< $@

build/r/nativo.c: tools/nativo.py tools/nativas.txt tools/nativas_overlay.txt
	@mkdir -p build/r
	python3 tools/nativo.py "$(BIN_JOGO)" $@ tools/nativas.txt tools/nativas_overlay.txt

build/nativo.o: build/r/nativo.c runtime/fast_memory.h runtime/janela.h runtime/gte_regs.h runtime/ram.h runtime/nativo_overlay.h
	$(EE_C_COMPILE) -c $< -o $@

build/r/SLUS_004.02_decls.h: ../gerado/SLUS_004.02_decls.h tools/transformar_gerado.py
	@mkdir -p build/r
	python3 tools/transformar_gerado.py $< $@

build/%.o: build/r/%.c build/r/SLUS_004.02_decls.h runtime/fast_memory.h runtime/janela.h runtime/ram.h
	@mkdir -p $(@D)
	$(EE_C_COMPILE) -O2 -MMD -MP -DPS2_GENERATED_FAST_MEMORY=1 -include runtime/fast_memory.h -c $< -o $@

# -Werror=undef: um #if com nome não definido vale 0 sem aviso. Na limpeza da V60
# isso desligou os efeitos e as vozes (PS2_SPU2_LIGADA apagado, #if ficou): agora para o build.
build/%.o: runtime/%.c runtime/ram.h runtime/memory.h runtime/io.h runtime/mdec.h runtime/bios_hle.h runtime/cdrom.h runtime/pad.h runtime/interp.h Makefile
	@mkdir -p $(@D)
	$(EE_C_COMPILE) -Werror=undef -MMD -MP -c $< -o $@

.PRECIOUS: build/r/%.c

# Dependências geradas pelo compilador (-MMD): qualquer header alterado
# recompila quem o usa (achado F8 da revisão técnica).
-include $(EE_OBJS:.o=.d)

# Símbolos do runtime que os testes não compilam junto (g_ps2_tmr_servindo,
# g_cod_ver...): definições fracas, ver tests/stubs_pc.c.
TEST_STUBS := tests/stubs_pc.c

test:
	@mkdir -p build/tests
	gcc -std=c99 -Wall -Wextra -Werror runtime/ram.c runtime/codigo.c tests/ram_test.c -o build/tests/ram_test
	build/tests/ram_test
	gcc -std=c99 -Wall -Wextra -Werror -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c runtime/mdec.c runtime/cdrom.c runtime/pad.c tests/memory_test.c $(TEST_STUBS) -o build/tests/memory_test
	build/tests/memory_test
	gcc -std=c99 -Wall -Wextra -Werror -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c runtime/mdec.c runtime/cdrom.c runtime/pad.c tests/fast_memory_test.c $(TEST_STUBS) -o build/tests/fast_memory_test
	build/tests/fast_memory_test
	gcc -std=c99 -Wall -Wextra -Werror -DPS2_LOAD_COMPACTO=0 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c runtime/mdec.c runtime/cdrom.c runtime/pad.c tests/fast_memory_test.c $(TEST_STUBS) -o build/tests/fast_memory_test_inline
	build/tests/fast_memory_test_inline
	gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only -DPS2_GENERATED_FAST_MEMORY=1 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 \
	    -I$(REF_INC) -Iruntime -include runtime/fast_memory.h tests/fast_memory_gerado_test.c
	gcc -std=c99 -Wall -Wextra -Werror -I$(REF_INC) runtime/gte.c tests/gte_test.c -o build/tests/gte_test
	build/tests/gte_test
	gcc -std=c99 -Wall -Wextra -Werror runtime/gpu.c tests/gpu_test.c -o build/tests/gpu_test
	build/tests/gpu_test
	gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iruntime runtime/mdec.c runtime/ram.c runtime/codigo.c tests/mdec_test.c -lm -o build/tests/mdec_test
	build/tests/mdec_test
	gcc -std=c99 -Wall -Wextra -Werror -Dclock=relogio_teste -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    tests/revisao_test.c runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c \
	    runtime/mdec.c runtime/cdrom.c runtime/pad.c runtime/bios_hle.c runtime/cartao_ps1.c $(TEST_STUBS) -o build/tests/revisao_test
	build/tests/revisao_test
	gcc -std=c99 -Wall -Wextra -Werror -Iruntime tests/revisao_cd_test.c runtime/ram.c runtime/codigo.c $(TEST_STUBS) -o build/tests/revisao_cd_test
	build/tests/revisao_cd_test
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iruntime tests/cd_bfrd_test.c $(TEST_STUBS) -o build/tests/cd_bfrd_test
	build/tests/cd_bfrd_test
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
	    -I$(REF_INC) -Iruntime tests/dma_ciclo_test.c $(TEST_STUBS) -o build/tests/dma_ciclo_test
	build/tests/dma_ciclo_test > /dev/null
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
	    -I$(REF_INC) -Iruntime tests/dedup_test.c $(TEST_STUBS) -o build/tests/dedup_test
	build/tests/dedup_test
	gcc -std=c99 -Wall -Wextra -Werror -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    tests/revisao_bios_test.c runtime/ram.c runtime/codigo.c runtime/io.c runtime/gpu.c runtime/mdec.c \
	    runtime/cdrom.c runtime/pad.c runtime/cartao_ps1.c $(TEST_STUBS) -o build/tests/revisao_bios_test
	build/tests/revisao_bios_test
	gcc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	    -Dclock=relogio_teste -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1 -I$(REF_INC) -Iruntime \
	    tests/pad_espera_test.c runtime/ram.c runtime/codigo.c runtime/memory.c runtime/io.c runtime/gpu.c runtime/mdec.c \
	    runtime/cdrom.c runtime/pad.c $(TEST_STUBS) -o build/tests/pad_espera_test
	build/tests/pad_espera_test > build/tests/pad_espera_test.txt
	tail -1 build/tests/pad_espera_test.txt
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iruntime \
	    tests/som_test.c runtime/spu.c runtime/ram.c runtime/codigo.c -lm -o build/tests/som_test
	build/tests/som_test
	python3 tools/msx_fonte.py $(PS2SDK)/ee/lib/libdebug.a $(PS2DEV)/ee/bin/mips64r5900el-ps2-elf-objcopy build/tests/msx_fonte.h
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iruntime -Ibuild/tests \
	    tests/abertura_test.c runtime/abertura.c -lm -o build/tests/abertura_test
	build/tests/abertura_test
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iruntime \
	    tests/cartao_test.c runtime/cartao_ps1.c -o build/tests/cartao_test
	build/tests/cartao_test
	gcc -std=c99 -fsanitize=undefined -fno-sanitize-recover=all -I$(REF_INC) runtime/gte.c tests/gte_test.c -o build/tests/gte_ubsan
	build/tests/gte_ubsan > /dev/null
	@# criador do navegador (núcleo em JS); com o disco em ../../ROM confere o jogo de verdade
	@if command -v node >/dev/null; then node tools/criador/testar_nucleo.js "../../ROM"; else echo "(sem node: teste do criador pulado)"; fi
	@echo "Testes OK"

clean:
	rm -rf build $(EE_BIN)

FORCE:

.PHONY: all test clean FORCE
