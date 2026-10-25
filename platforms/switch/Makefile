# rondo-nx for Nintendo Switch (libnx). Build from this folder with devkitPro's make:
#   make -j8 GEN=<folder with xlat_*.cpp and xlat_table.inc>
# Output: rondo-nx.nro

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

ROOT    := ../..
GG      := $(ROOT)/third_party/geargrafx
GEN     ?= $(ROOT)/out/generated
BUILD   := build

APP_TITLE   := rondo-nx
APP_AUTHOR  := FurryLilManChild
APP_VERSION := 0.1

CORE := geargrafx_core adpcm audio arcade_card_mapper cdrom_audio cdrom_chd_file_adapter cdrom_chd_image \
        cdrom_cuebin_image cdrom_mmi_image media_file media_file_native media_file_slice mmi_archive \
        ogg_vorbis_decoder cdrom_image cdrom_media cdrom huc6202 huc6260 huc6270 huc6280 rondo_trace \
        huc6280_functors huc6280_opcodes huc6280_psg input laseractive mapper mb128 media memory \
        scsi_controller sf2_mapper trace_logger profiler turbolink vgm_recorder

CXX_SRC := $(addprefix $(GG)/src/,$(addsuffix .cpp,$(CORE))) $(GG)/dependencies/qon/qon.cpp \
           $(ROOT)/runtime/rondo_xlat.cpp $(ROOT)/app/switch_main.cpp $(ROOT)/app/menu.cpp
C_SRC   := $(GG)/dependencies/libchdr/src/libchdr_bitstream.c $(GG)/dependencies/libchdr/src/libchdr_cdrom.c \
           $(GG)/dependencies/libchdr/src/libchdr_chd.c $(GG)/dependencies/libchdr/src/libchdr_flac.c \
           $(GG)/dependencies/libchdr/src/libchdr_huffman.c $(GG)/dependencies/lzma/src/LzFind.c \
           $(GG)/dependencies/lzma/src/LzmaEnc.c $(GG)/dependencies/lzma/src/LzmaDec.c \
           $(GG)/dependencies/lzma/src/CpuArch.c $(GG)/dependencies/minivorbis/minivorbis.c \
           $(GG)/dependencies/miniz/miniz.c $(ROOT)/app/switch_compat.c
GEN_SRC := $(sort $(wildcard $(GEN)/xlat_*.cpp))

OBJS := $(addprefix $(BUILD)/core/,$(notdir $(CXX_SRC:.cpp=.o)) $(notdir $(C_SRC:.c=.o))) \
        $(addprefix $(BUILD)/gen/,$(notdir $(GEN_SRC:.cpp=.o)))

INC  := -I$(GG)/src -I$(GG)/dependencies/libchdr/include -I$(GG)/dependencies/lzma/include \
        -I$(GG)/dependencies/miniz -I$(GG)/dependencies/minivorbis -I$(GG)/dependencies/zstd \
        -I$(GG)/dependencies/json -I$(GG)/dependencies/qon -I$(ROOT)/runtime -I$(GEN) -I$(LIBNX)/include \
        -I$(PORTLIBS)/include

ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFS     := -D__SWITCH__ -DHAVE_LIBNX -DMINIZ_NO_TIME -DGG_DISABLE_DISASSEMBLER -DGG_DISABLE_VGMRECORDER -DZ7_ST -DZSTD_DISABLE_ASM -DNDEBUG -DEMULATOR_BUILD=\"rondo-nx\"
CFLAGS   := -O3 -ffunction-sections -fdata-sections $(ARCH) $(DEFS) $(INC) -w
CXXFLAGS := $(CFLAGS) -std=gnu++11 -fno-rtti -fno-exceptions
LDFLAGS  := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/rondo-nx.map
LIBS     := -L$(LIBNX)/lib -L$(PORTLIBS)/lib -lnx -lm

vpath %.cpp $(sort $(dir $(CXX_SRC)))
vpath %.c $(sort $(dir $(C_SRC)))

.PHONY: all clean
all: rondo-nx.nro

rondo-nx.nro: $(BUILD)/rondo-nx.elf $(BUILD)/rondo-nx.nacp
	elf2nro $(BUILD)/rondo-nx.elf $@ --nacp=$(BUILD)/rondo-nx.nacp --icon=$(LIBNX)/default_icon.jpg
	@echo built $@

$(BUILD)/rondo-nx.nacp: | $(BUILD)
	nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(BUILD)/rondo-nx.elf: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILD)/core/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/core/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -std=gnu99 -c -o $@ $<

$(BUILD)/gen/%.o: $(GEN)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -O1 -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)/core $(BUILD)/gen

clean:
	rm -rf $(BUILD) rondo-nx.nro
