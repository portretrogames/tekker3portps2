# Makefile — Tekken 3 PS2 (ps2sdk). Uso no WSL/Linux:
#   export PS2DEV=~/ps2dev/ps2dev PS2SDK=$PS2DEV/ps2sdk
#   export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin
#   make -j4        -> tekken3.elf
#   make test       -> testes do runtime no PC (gcc normal)
#
# O código de ../gerado NÃO é editado: o caminho rápido de memória entra
# por "-include runtime/fast_memory.h" só nesses arquivos.
# Sem PSX_ENABLE_BLOCK_CYCLES (contagem de ciclos) — de propósito.

EE_BIN = tekken3.elf
REF_INC = ../referencia/Tekken3Recompiled/psxrecomp/runtime/include
EE_INCS = -I../gerado -I$(REF_INC) -Iruntime -I../kit/include -I$(PS2DEV)/gsKit/include
EE_CFLAGS = -O2 -G0 -DPSX_NO_DEBUG_TOOLS=1 -DPSX_NATIVE_BUILD=1
# Otimização só do código gerado do jogo (GEN_OPT=-Os gera código menor:
# teste de cache de instruções). A variante aparece no painel.
GEN_OPT ?= -O2
JOGO_SUFIXO_FLAG := $(if $(filter -O2,$(GEN_OPT)),,-DJOGO_SUFIXO='" $(subst -,,$(GEN_OPT))"')
# build/gen_opt muda quando GEN_OPT muda: força recompilar o que depende dele
$(shell mkdir -p build; echo '$(GEN_OPT)' | cmp -s - build/gen_opt || echo '$(GEN_OPT)' > build/gen_opt)
build/main.o build/video_gs.o: build/gen_opt
EE_DBGINFOFLAGS =
EE_WARNFLAGS =

GAME_SHARDS := $(addprefix ../gerado/SLUS_004.02_full_,$(addsuffix .c,$(shell seq -w 0 44)))
GAME_SOURCES := $(GAME_SHARDS) ../gerado/SLUS_004.02_dispatch.c
GAME_OBJS := $(patsubst ../gerado/%.c,build/%.o,$(GAME_SOURCES))
# Bibliotecas compartilhadas (../kit): controle, armazenamento, diagnóstico
KIT_DIR := ../kit
KIT_BUILD := build
include $(KIT_DIR)/kit.mk
# Rotinas do jogo traduzidas para C nativo (tools/nativo.py), lidas do .bin
BIN_JOGO ?= Tekken 3 (USA) (Track 1).bin
NAT_VERIFICAR ?= 0
RUNTIME_OBJS := build/nativo.o build/nativo_ganchos.o build/main.o build/ram.o build/memory.o build/io.o build/gpu.o build/gs_render.o build/video_gs.o build/abertura.o build/cartao_ps1.o build/cartao_ps2.o build/mdec.o build/cdrom.o build/pad.o $(KIT_OBJS) build/interp.o build/codigo.o build/bios_hle.o build/gte.o build/stubs.o build/som.o build/spu.o build/trace_fmv.o build/pmc.o
EE_OBJS := $(GAME_OBJS) $(RUNTIME_OBJS)
EE_LDFLAGS += -L$(PS2DEV)/gsKit/lib
EE_LIBS += -lgskit -ldmakit $(KIT_LIBS) -ldebug -lm


# Pasta pronta para o pendrive (OPL: mass:/APPS/TEKKEN3/). O .bin do jogo
# NÃO é incluído: o usuário copia o dele.
pacote: $(EE_BIN)
	@rm -rf build/pacote && mkdir -p build/pacote/APPS/TEKKEN3
	cp $(EE_BIN) build/pacote/APPS/TEKKEN3/tekken3.elf
	printf 'title=Tekken 3 (port nativo)\nboot=tekken3.elf\n' > build/pacote/APPS/TEKKEN3/title.cfg
	cp docs/PS2_REAL.md build/pacote/APPS/TEKKEN3/LEIA-ME.md
	cp $(EE_BIN) ../tekken3.elf
	@echo "Pacote em build/pacote/APPS/TEKKEN3 (copie o .bin do jogo para la); copia em ../tekken3.elf"

.DEFAULT_GOAL := all
DEBUG_GPU ?= 0
DEBUG_VRAM ?= 0
GPU_FLAGS := $(if $(filter 1,$(DEBUG_GPU)),-DPS2_DEBUG_GPU,)
VRAM_FLAGS := $(if $(filter 1,$(DEBUG_VRAM)),-DPS2_DEBUG_VRAM,)
# AUTO_START=1: aperta START sozinho (teste sem controle no PCSX2 headless)
AUTO_START ?= 0
VRAM_FLAGS += $(if $(filter 1,$(AUTO_START)),-DPS2_AUTO_START,)
# VER_VRAM=1: mostra a VRAM inteira do PS1 (1024x512) na tela (diagnóstico)
VER_VRAM ?= 0
VRAM_FLAGS += $(if $(filter 1,$(VER_VRAM)),-DPS2_VER_VRAM,)
# LOG_BLOCOS: grava o log a cada N relatórios de ~2 s (15 = ~30 s); 0 = sem log
LOG_BLOCOS ?= 15
LOG_FLAGS := -DPS2_LOG_BLOCOS=$(LOG_BLOCOS)
build/gpu.o: EE_CFLAGS += $(GPU_FLAGS)
# FORCAR_IOP=1: testa no PCSX2 o caminho do PS2 real (reinicia o IOP, módulos USB)
build/main.o: EE_CFLAGS += $(if $(filter 1,$(FORCAR_IOP)),-DPS2_FORCAR_IOP,) $(JOGO_SUFIXO_FLAG) $(LOG_FLAGS)
build/video_gs.o: EE_CFLAGS += $(VRAM_FLAGS) $(JOGO_SUFIXO_FLAG) $(LOG_FLAGS)
# V45: o perfil do GTE por comando só entra no ELF com log
build/gte.o: EE_CFLAGS += $(LOG_FLAGS)
# TRACE_FMV=1: rastreio do vídeo da abertura (MDEC, DMA0/1, CD) no log
# SOM_SEQ: 1 = roda o sequenciador de som do jogo (80088BA8, V53: efeitos e vozes
# na SPU2); 0 = pula (V36, sem efeitos)
SOM_SEQ ?= 1
build/nativo_ganchos.o: EE_CFLAGS += -DPS2_SOM_PULAR=$(if $(filter 1,$(SOM_SEQ)),0,1)
# PMC=1: contadores de desempenho do R5900 por região no log (runtime/pmc.h; só no console)
PMC ?= 0
PMC_OBJS := build/pmc.o build/io.o build/bios_hle.o build/nativo_ganchos.o build/video_gs.o build/main.o
$(PMC_OBJS): EE_CFLAGS += $(if $(filter 1,$(PMC)),-DPS2_PMC,)
# PMC_VARRE=1 ou 2 (com PMC=1): varre a posição da pilha no cache de dados (runtime/pmc.h)
PMC_VARRE ?= 0
$(PMC_OBJS): EE_CFLAGS += $(if $(filter 1,$(PMC)),$(if $(filter 1 2,$(PMC_VARRE)),-DPS2_PMC_VARRE=$(PMC_VARRE),),)
$(PMC_OBJS): build/debug_flags
# LIMPO=1: versão de jogar sem medição (V58): sem painel, sem relatório de 2 s,
# sem cronômetros no caminho quente (despacho, polígonos, nativas, GPU) e sem a
# linha do tempo. Com log (LOG_BLOCOS>0) sobra só a linha "quadros:" a cada ~2 s.
LIMPO ?= 0
LIMPO_OBJS := build/SLUS_004.02_dispatch.o build/stubs.o build/nativo_ganchos.o build/gs_render.o build/gpu.o build/video_gs.o
$(LIMPO_OBJS): EE_CFLAGS += $(if $(filter 1,$(LIMPO)),-DPS2_LIMPO,)
$(LIMPO_OBJS): build/debug_flags
# (só estes objetos olham PS2_LIMPO: quem passar a olhar entra na lista)
ifeq ($(LIMPO)$(PMC),11)
$(error LIMPO=1 e PMC=1 nao combinam: a versao limpa nao chama o relatorio do PMC)
endif
# RECUP_APRES=1: passo de recuperação da lógica também na apresentação do round
# (8002AE58 devolve 0; ver ps2_luta_sem_recup em runtime/nativo_ganchos.c)
RECUP_APRES ?= 1
build/nativo_ganchos.o: EE_CFLAGS += -DPS2_RECUP_APRES=$(if $(filter 1,$(RECUP_APRES)),1,0)
# TRAB_QUADRO=1 (com log): trabalho de cada quadro em décimos de ms na linha "trab:" (V60)
TRAB_QUADRO ?= 0
build/nativo_ganchos.o: EE_CFLAGS += $(if $(filter 1,$(TRAB_QUADRO)),-DPS2_TRAB_QUADRO,)
ifeq ($(LIMPO)$(TRAB_QUADRO),11)
$(error LIMPO=1 e TRAB_QUADRO=1 nao combinam: a versao limpa nao grava a linha do tempo)
endif
# ABERTURA=0: sem as telas de início (V59). Desligada com AUTO_START=1
# (medidas automáticas contam o tempo a partir do boot).
ABERTURA ?= $(if $(filter 1,$(AUTO_START)),0,1)
build/main.o: EE_CFLAGS += -DPS2_ABERTURA=$(if $(filter 1,$(ABERTURA)),1,0)
# LOG_MARCA: acrescentada ao nome do log (ex.: _diag -> tekken3_log_V53_diag.txt)
LOG_MARCA ?=
build/main.o: EE_CFLAGS += $(if $(LOG_MARCA),-DJOGO_LOG_MARCA='"$(LOG_MARCA)"',)
# LOG_REDE=1: rodando pela rede (ps2link), log com buffer em host:tekken3_log_Vxx.txt
LOG_REDE ?= 0
build/main.o: EE_CFLAGS += -DPS2_LOG_REDE=$(if $(filter 1,$(LOG_REDE)),1,0)
# SPU2: 0 = não manda os comandos da SPU do PS1 para a SPU2 (efeitos mudos, teste A/B)
SPU2 ?= 1
build/spu.o: EE_CFLAGS += -DPS2_SPU2_LIGADA=$(SPU2)
# SPU_TRACE=1: grava KON/KOFF e ENDX/ENVX por VBlank em host:spu_trace.bin (90 s)
SPU_TRACE ?= 0
build/spu.o: EE_CFLAGS += $(if $(filter 1,$(SPU_TRACE)),-DPS2_SPU_TRACE,)
# CDDA_TESTE=1: toca 10 s da faixa 2 (CD-DA) a ~20 s de jogo (V59: o jogo nunca pediu Play nos testes)
CDDA_TESTE ?= 0
build/som.o: EE_CFLAGS += $(if $(filter 1,$(CDDA_TESTE)),-DPS2_CDDA_TESTE,)
# SEM_MUSICA=1: cala a música (XA) e deixa só os efeitos/vozes (medir pela placa de captura)
SEM_MUSICA ?= 0
build/som.o: EE_CFLAGS += $(if $(filter 1,$(SEM_MUSICA)),-DPS2_SEM_MUSICA,)
# Marcos dos primeiros VBlanks gravados na hora no log (só com log)
build/bios_hle.o: EE_CFLAGS += -DPS2_LOG_ETAPAS=$(if $(filter 0,$(LOG_BLOCOS)),0,1)
TRACE_FMV ?= 0
TRACE_FLAGS := $(if $(filter 1,$(TRACE_FMV)),-DPS2_TRACE_FMV,)
build/io.o build/mdec.o build/bios_hle.o build/cdrom.o build/interp.o build/trace_fmv.o: EE_CFLAGS += $(TRACE_FLAGS)
build/gpu.o build/gs_render.o build/video_gs.o build/main.o build/gte.o build/spu.o build/som.o build/nativo_ganchos.o: build/debug_flags
build/io.o build/mdec.o build/bios_hle.o build/cdrom.o build/interp.o build/trace_fmv.o: build/debug_flags

build/debug_flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' 'DEBUG_GPU=$(DEBUG_GPU) DEBUG_VRAM=$(DEBUG_VRAM) AUTO_START=$(AUTO_START) VER_VRAM=$(VER_VRAM) FORCAR_IOP=$(FORCAR_IOP) LOG_BLOCOS=$(LOG_BLOCOS) TRACE_FMV=$(TRACE_FMV) SPU2=$(SPU2) SOM_SEQ=$(SOM_SEQ) LOG_MARCA=$(LOG_MARCA) NAT_VERIFICAR=$(NAT_VERIFICAR) SEM_MUSICA=$(SEM_MUSICA) SPU_TRACE=$(SPU_TRACE) LOG_REDE=$(LOG_REDE) RECUP_APRES=$(RECUP_APRES) PMC=$(PMC) PMC_VARRE=$(PMC_VARRE) NAT_OPT=$(NAT_OPT) LIMPO=$(LIMPO) ABERTURA=$(ABERTURA) CDDA_TESTE=$(CDDA_TESTE) TRAB_QUADRO=$(TRAB_QUADRO)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

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

# Cópia do código gerado com "restrict" no ponteiro da CPU (gerado/ não é
# editado): sem isso o compilador supõe que cada escrita na RAM do PS1 pode
# alterar cpu->gpr[] e relê os registros depois de cada store.
# IRQ_FRENTE=1: checagem de interrupção também nos desvios para a frente
# (até a V58; ver tools/transformar_gerado.py). build/irq_frente muda quando
# a opção muda e refaz o código transformado.
IRQ_FRENTE ?= 0
ifneq ($(strip $(shell cat build/irq_frente 2>/dev/null)),$(IRQ_FRENTE))
.PHONY: build/irq_frente
endif
build/irq_frente:
	@mkdir -p $(@D)
	@printf '%s\n' '$(IRQ_FRENTE)' > $@
build/r/%.c: ../gerado/%.c tools/transformar_gerado.py tools/nativas.txt build/irq_frente
	@mkdir -p $(@D)
	PS2_IRQ_FRENTE=$(IRQ_FRENTE) python3 tools/transformar_gerado.py $< $@

build/r/nativo.c: tools/nativo.py tools/nativas.txt tools/nativas_overlay.txt
	@mkdir -p build/r
	python3 tools/nativo.py "$(BIN_JOGO)" $@ tools/nativas.txt tools/nativas_overlay.txt

# NAT_OPT: otimização só das rotinas nativas (teste de tamanho x cache: -Os)
NAT_OPT ?=
build/nativo.o: build/r/nativo.c runtime/fast_memory.h runtime/janela.h runtime/gte_regs.h runtime/ram.h runtime/nativo_overlay.h build/debug_flags
	$(EE_C_COMPILE) $(NAT_OPT) -c $< -o $@

build/nativo_ganchos.o build/nativo.o: EE_CFLAGS += $(if $(filter 1,$(NAT_VERIFICAR)),-DPS2_NAT_VERIFICAR,)

build/r/SLUS_004.02_decls.h: ../gerado/SLUS_004.02_decls.h tools/transformar_gerado.py
	@mkdir -p build/r
	python3 tools/transformar_gerado.py $< $@

build/%.o: build/r/%.c build/r/SLUS_004.02_decls.h runtime/fast_memory.h runtime/janela.h runtime/ram.h build/gen_opt
	@mkdir -p $(@D)
	$(EE_C_COMPILE) $(GEN_OPT) -MMD -MP -DPS2_GENERATED_FAST_MEMORY=1 -include runtime/fast_memory.h -c $< -o $@

build/%.o: runtime/%.c runtime/ram.h runtime/memory.h runtime/io.h runtime/mdec.h runtime/bios_hle.h runtime/cdrom.h runtime/pad.h runtime/interp.h
	@mkdir -p $(@D)
	$(EE_C_COMPILE) -MMD -MP -c $< -o $@

.PRECIOUS: build/r/%.c

# Dependências geradas pelo compilador (-MMD): qualquer header alterado
# recompila quem o usa (achado F8 da revisão técnica).
-include $(EE_OBJS:.o=.d)

# Símbolos do runtime que os testes não compilam junto (ps2_diag,
# g_ps2_tmr_servindo, g_cod_ver): definições fracas, ver tests/stubs_pc.c.
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
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
	    -DPS2_DEDUP_CONTEUDO=1 -I$(REF_INC) -Iruntime tests/dedup_test.c $(TEST_STUBS) -o build/tests/dedup_conteudo_test
	build/tests/dedup_conteudo_test
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
	@echo "Testes OK"

clean:
	rm -rf build $(EE_BIN)

FORCE:

.PHONY: all test clean FORCE
