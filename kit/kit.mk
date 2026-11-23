# kit.mk — inclua no Makefile do projeto (ps2sdk) ANTES de montar EE_OBJS:
#   KIT_DIR := ../kit
#   KIT_BUILD := build
#   include $(KIT_DIR)/kit.mk
#   EE_OBJS += $(KIT_OBJS)
#   EE_INCS += $(KIT_INCS)
#   EE_LIBS += $(KIT_LIBS)
# Requer PS2SDK. Os módulos do IOP vão embutidos no ELF (bin2c).
# Use KIT_DIR e KIT_BUILD relativos e sem espaço (a pasta do projeto pode ter
# espaços: o make separa caminhos absolutos em palavras).

KIT_DIR   ?= kit
KIT_BUILD ?= build
KIT_INCS  := -I$(KIT_DIR)/include
KIT_LIBS  := -lpad -lmc -lfileXio -lpatches -laudsrv
KIT_IRX_NOMES := iomanX fileXio usbd bdm bdmfs_fatfs usbmass_bd freesd
KIT_IRX_OBJS  := $(addprefix $(KIT_BUILD)/kit_irx_,$(addsuffix .o,$(KIT_IRX_NOMES))) \
                 $(KIT_BUILD)/kit_irx_audsrv.o
KIT_OBJS  := $(KIT_BUILD)/kit_controle.o $(KIT_BUILD)/kit_cartao.o $(KIT_BUILD)/kit_armazenamento.o \
             $(KIT_BUILD)/kit_diagnostico.o $(KIT_BUILD)/kit_som.o $(KIT_IRX_OBJS)

$(KIT_BUILD)/kit_%.o: $(KIT_DIR)/src/%.c $(wildcard $(KIT_DIR)/include/kit/*.h)
	@mkdir -p $(@D)
	$(EE_CC) -D_EE -G0 -O2 -Wall $(KIT_INCS) -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -c $< -o $@

$(KIT_BUILD)/kit_irx_%.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p $(@D)
	$(PS2SDK)/bin/bin2c $< $@ $*_irx

# audsrv: compilado de kit/iop/audsrv (PS2SDK com o volume do PCM corrigido).
KIT_AUDSRV := $(KIT_DIR)/iop/audsrv
$(KIT_BUILD)/kit_iop/audsrv.irx: $(wildcard $(KIT_AUDSRV)/src/* $(KIT_AUDSRV)/include/*) $(KIT_AUDSRV)/Makefile
	@mkdir -p $(KIT_BUILD)/kit_iop/obj
	$(MAKE) -f $(KIT_AUDSRV)/Makefile IOP_BIN=$@ IOP_OBJS_DIR=$(KIT_BUILD)/kit_iop/obj/ \
	    IOP_SRC_DIR=$(KIT_AUDSRV)/src/ IOP_INC_DIR=$(KIT_AUDSRV)/include/ \
	    IOP_INCS="-I$(KIT_AUDSRV)/src -I$(PS2SDK)/iop/include -I$(PS2SDK)/common/include -I. -I$(KIT_AUDSRV)/include/"
	@# 0.3.1: a parte com conteúdo (FileSiz do LOAD) tem que ser múltiplo de 4: o IOP do
	@# console zera a .bss a partir dali com escritas de 4 bytes e travava ao carregar
	@# (um u16 com valor inicial no fim da .data; o PCSX2 aceita).
	@python3 -c "import struct,sys;d=open(sys.argv[1],'rb').read();po=struct.unpack_from('<I',d,28)[0];es,n=struct.unpack_from('<HH',d,42);ph=[struct.unpack_from('<8I',d,po+i*es) for i in range(n)];bad=[hex(p[4]) for p in ph if p[0]==1 and p[4]%4];sys.exit('ERRO: IRX com FileSiz nao multiplo de 4 ('+','.join(bad)+'): o IOP do console trava ao carregar' if bad else 0)" $@
$(KIT_BUILD)/kit_irx_audsrv.c: $(KIT_BUILD)/kit_iop/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

$(KIT_BUILD)/kit_irx_%.o: $(KIT_BUILD)/kit_irx_%.c
	$(EE_CC) -D_EE -G0 -O2 -c $< -o $@

.PRECIOUS: $(KIT_BUILD)/kit_irx_%.c
