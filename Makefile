# ============================================================================
#  Gens for Mac - Makefile  (macOS, Intel x86_64)
#
#  Builds a Sega Mega Drive / Genesis emulator: a mature 64-bit-clean C core
#  (Genesis Plus GX lineage) driven by the custom Gens-style SDL2 frontend in
#  src/gens_mac.c.
#
#  Requirements:
#    - Apple clang (Xcode command line tools)
#    - SDL2 (Homebrew:  brew install sdl2)
#
#  Usage:
#    make            # build ./gens_mac
#    make app        # build Gens.app bundle
#    make clean
# ============================================================================

NAME      = gens_mac

CC        = clang

# ---- SDL2 selection --------------------------------------------------------
# Prefer the vendored STATIC SDL2 (built by ./build_sdl2.sh into
# vendor/install) so the resulting binary / Gens.app is fully self-contained.
# Fall back to the system sdl2-config when the static build is absent.
SDL_STATIC_PREFIX = vendor/install
ifneq ($(wildcard $(SDL_STATIC_PREFIX)/lib/libSDL2.a),)
  SDL_CFLAGS = -I$(SDL_STATIC_PREFIX)/include/SDL2 -D_THREAD_SAFE
  SDL_LIBS   = $(SDL_STATIC_PREFIX)/lib/libSDL2.a \
               -liconv \
               -framework CoreVideo -framework CoreAudio -framework AudioToolbox \
               -framework ForceFeedback -framework IOKit -framework Carbon \
               -framework CoreHaptics -framework GameController -framework Metal \
               -framework QuartzCore -framework CoreServices
else
  SDL_CFLAGS = $(shell sdl2-config --cflags)
  SDL_LIBS   = $(shell sdl2-config --libs)
endif

CFLAGS    = $(SDL_CFLAGS) -O2 -fomit-frame-pointer -std=gnu11 \
            -Wno-strict-aliasing -Wno-everything -arch x86_64
OBJCFLAGS = $(SDL_CFLAGS) -O2 -fomit-frame-pointer -fobjc-arc -w -arch x86_64
DEFINES   = -DLSB_FIRST -DUSE_16BPP_RENDERING -DUSE_LIBTREMOR -DUSE_LIBCHDR \
            -DMAXROMSIZE=33554432 -DHAVE_YM3438_CORE -DHAVE_OPLL_CORE \
            -DENABLE_SUB_68K_ADDRESS_ERROR_EXCEPTIONS -DZ7_ST \
            -DZSTD_DISABLE_ASM -DHAVE_ALLOCA_H

SRCDIR    = core
FRONTDIR  = src
CHDLIBDIR = $(SRCDIR)/cd_hw/libchdr

INCLUDES  = -I$(SRCDIR) -I$(SRCDIR)/z80 -I$(SRCDIR)/m68k -I$(SRCDIR)/sound \
            -I$(SRCDIR)/sound/minimp3 -I$(SRCDIR)/sound/tremor \
            -I$(SRCDIR)/input_hw -I$(SRCDIR)/cart_hw -I$(SRCDIR)/cart_hw/svp \
            -I$(SRCDIR)/cd_hw -I$(SRCDIR)/ntsc -I$(FRONTDIR) \
            -I$(CHDLIBDIR)/include -I$(CHDLIBDIR)/deps/lzma-24.05/include

LIBS      = $(SDL_LIBS) -lz -lm -framework Cocoa

OBJDIR    = build

# ---- object list ----------------------------------------------------------
OBJECTS = \
  $(OBJDIR)/z80.o \
  $(OBJDIR)/m68kcpu.o $(OBJDIR)/s68kcpu.o \
  $(OBJDIR)/genesis.o $(OBJDIR)/vdp_ctrl.o $(OBJDIR)/vdp_render.o \
  $(OBJDIR)/system.o $(OBJDIR)/io_ctrl.o $(OBJDIR)/mem68k.o \
  $(OBJDIR)/memz80.o $(OBJDIR)/membnk.o $(OBJDIR)/state.o $(OBJDIR)/loadrom.o \
  $(OBJDIR)/input.o $(OBJDIR)/gamepad.o $(OBJDIR)/lightgun.o $(OBJDIR)/mouse.o \
  $(OBJDIR)/activator.o $(OBJDIR)/xe_1ap.o $(OBJDIR)/teamplayer.o \
  $(OBJDIR)/paddle.o $(OBJDIR)/smash.o $(OBJDIR)/sportspad.o \
  $(OBJDIR)/terebi_oekaki.o $(OBJDIR)/graphic_board.o \
  $(OBJDIR)/sound.o $(OBJDIR)/psg.o $(OBJDIR)/ym2413.o $(OBJDIR)/opll.o \
  $(OBJDIR)/ym3438.o $(OBJDIR)/ym2612.o $(OBJDIR)/blip_buf.o $(OBJDIR)/eq.o \
  $(OBJDIR)/sram.o $(OBJDIR)/svp.o $(OBJDIR)/ssp16.o $(OBJDIR)/ggenie.o \
  $(OBJDIR)/areplay.o $(OBJDIR)/eeprom_93c.o $(OBJDIR)/eeprom_i2c.o \
  $(OBJDIR)/eeprom_spi.o $(OBJDIR)/flash_cfi.o $(OBJDIR)/yx5200.o \
  $(OBJDIR)/md_cart.o $(OBJDIR)/sms_cart.o $(OBJDIR)/megasd.o \
  $(OBJDIR)/scd.o $(OBJDIR)/cdd.o $(OBJDIR)/cdc.o $(OBJDIR)/gfx.o \
  $(OBJDIR)/pcm.o $(OBJDIR)/cd_cart.o \
  $(OBJDIR)/sms_ntsc.o $(OBJDIR)/md_ntsc.o \
  $(OBJDIR)/bitwise.o $(OBJDIR)/block.o $(OBJDIR)/codebook.o $(OBJDIR)/floor0.o \
  $(OBJDIR)/floor1.o $(OBJDIR)/framing.o $(OBJDIR)/info.o $(OBJDIR)/mapping0.o \
  $(OBJDIR)/mdct.o $(OBJDIR)/registry.o $(OBJDIR)/res012.o $(OBJDIR)/sharedbook.o \
  $(OBJDIR)/synthesis.o $(OBJDIR)/vorbisfile.o $(OBJDIR)/window.o \
  $(OBJDIR)/libchdr_bitstream.o $(OBJDIR)/libchdr_cdrom.o $(OBJDIR)/libchdr_chd.o \
  $(OBJDIR)/libchdr_flac.o $(OBJDIR)/libchdr_huffman.o $(OBJDIR)/LzFind.o \
  $(OBJDIR)/LzmaDec.o $(OBJDIR)/LzmaEnc.o $(OBJDIR)/CpuArch.o \
  $(OBJDIR)/zstd_decompress.o $(OBJDIR)/huf_decompress.o \
  $(OBJDIR)/zstd_decompress_block.o $(OBJDIR)/zstd_ddict.o \
  $(OBJDIR)/entropy_common.o $(OBJDIR)/error_private.o $(OBJDIR)/fse_decompress.o \
  $(OBJDIR)/xxhash.o $(OBJDIR)/zstd_common.o \
  $(OBJDIR)/gens_mac.o $(OBJDIR)/config.o $(OBJDIR)/error.o \
  $(OBJDIR)/unzip.o $(OBJDIR)/fileio.o \
  $(OBJDIR)/settings.o $(OBJDIR)/video_fx.o $(OBJDIR)/ui.o $(OBJDIR)/font.o $(OBJDIR)/cocoa_menu.o

all: $(NAME)

$(NAME): $(OBJDIR) $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) $(LIBS) -o $@
	@echo "==> built ./$(NAME)"

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ---- pattern rules (mirror upstream layout) -------------------------------
$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/sound/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/input_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/cart_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/cart_hw/svp/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/cd_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/z80/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/m68k/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/ntsc/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(SRCDIR)/sound/tremor/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(CHDLIBDIR)/src/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) -I$(CHDLIBDIR)/include -I$(CHDLIBDIR)/deps/zstd-1.5.6/lib -I$(CHDLIBDIR)/deps/zstd-1.5.6/lib/common -I$(CHDLIBDIR)/deps/zlib-1.3.1 $< -o $@
$(OBJDIR)/%.o : $(CHDLIBDIR)/deps/lzma-24.05/src/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(CHDLIBDIR)/deps/zstd-1.5.6/lib/common/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(CHDLIBDIR)/deps/zstd-1.5.6/lib/decompress/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(FRONTDIR)/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
$(OBJDIR)/%.o : $(FRONTDIR)/%.m
	$(CC) -c $(OBJCFLAGS) -Icore -Isrc $(DEFINES) $< -o $@

# ---- macOS .app bundle ----------------------------------------------------
app: $(NAME)
	@sh ./make_app.sh

clean:
	rm -rf $(OBJDIR) $(NAME)

distclean: clean
	rm -rf Gens.app

.PHONY: all app clean distclean
