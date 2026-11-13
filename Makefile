# DOAX3 Recomp build (Windows: MSYS2 UCRT64 clang + Vulkan, as LAS Recomp's Windows build).
#   make gen   recompile extracted/exefs (your own game files) into generated/
#   make -j    build/doax3recomp.exe and the game code library build/doaxgame.dll
# From an MSYS2 UCRT64 shell, or Git Bash with: export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
comma := ,
B        ?= build
GEN      ?= generated
CC       := clang
CXX      := clang++
IMGUI    := third_party/imgui
PY       := .venv/Scripts/python.exe

OSFLAGS  := -D_FILE_OFFSET_BITS=64 -D_POSIX_THREAD_SAFE_FUNCTIONS -D_CRT_RAND_S -DWIN32_LEAN_AND_MEAN -DNOMINMAX
GENARCH  ?= -march=x86-64-v3  # AVX2 + FMA for the lifted FP/SIMD code (simd.h)
GENOPT   ?= -O1               # the game code's optimization (make B=build_o2 GENOPT=-O2: a second build to compare)
GENFLAGS := $(GENOPT) -fno-strict-aliasing -Wno-return-type -Wno-unused-label -fvisibility=hidden -Iruntime \
            -D_FILE_OFFSET_BITS=64 $(GENARCH)
RTFLAGS  := -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -Iruntime -Iruntime/nvn $(OSFLAGS)
CXXFLAGS := -O2 -g -std=c++17 -I$(IMGUI) -I$(IMGUI)/backends -Iruntime -Iruntime/nvn $(OSFLAGS)
LDFLAGS  := -fuse-ld=lld -Wl,--stack,16777216 -lSDL3 -lavformat -lavcodec -lavutil -lswscale \
            -lvulkan-1 -lws2_32 -lwinmm -lole32 -lshell32 -lcomdlg32 -luuid -lshaderc_shared \
            -Wl,--error-limit=0 -Wl,--export-all-symbols -Wl,--out-implib,$(B)/doax3recomp.dll.a

EXE      := $(B)/doax3recomp.exe
GAMELIB  := $(B)/doaxgame.dll
LAUNCHER := $(B)/DOAX3Launcher.exe
MODULES  := game sdk glslc

GEN_SRC  := $(strip $(foreach m,$(MODULES),$(wildcard $(GEN)/$(m)/*.c)))
GEN_OBJ  := $(patsubst $(GEN)/%.c,$(B)/obj/%.o,$(GEN_SRC)) $(B)/obj/tools/gamelib_entry.o
RT_SRC   := $(wildcard runtime/*.c) $(wildcard runtime/hle/*.c) $(wildcard runtime/services/*.c) \
            $(wildcard runtime/nvn/*.c) $(wildcard runtime/nvn/maxwell/*.c) runtime/platform/windows.c
RT_HDRS  := $(wildcard runtime/*.h runtime/platform/*.h runtime/nvn/*.h runtime/nvn/maxwell/*.h \
                       runtime/nvn/vulkan/*.h runtime/ui/*.h)
BACKEND_SRC := $(wildcard runtime/nvn/vulkan/*.c) $(wildcard runtime/nvn/vulkan/*.cpp)
UI_SRC   := $(wildcard runtime/ui/*.mm)
RT_OBJ   := $(patsubst runtime/%.c,$(B)/rt/%.o,$(RT_SRC))
BACKEND_OBJ := $(patsubst runtime/%.cpp,$(B)/rt/%.o,$(patsubst runtime/%.c,$(B)/rt/%.o,$(BACKEND_SRC)))
UI_OBJ   := $(patsubst runtime/%.mm,$(B)/rt/%.o,$(UI_SRC))
IMGUI_OBJ := $(B)/imgui/imgui.o $(B)/imgui/imgui_draw.o $(B)/imgui/imgui_tables.o $(B)/imgui/imgui_widgets.o \
             $(B)/imgui/imgui_impl_sdl3.o $(B)/imgui/imgui_impl_vulkan.o

# what the game code library is built from: a library with another id is never loaded (runtime/gamelib.h)
GAMELIB_SRCS := tools/gen.py tools/lifter.py tools/analyze.py tools/nso.py tools/passes.py tools/simd_c.py tools/patches.py \
                tools/gamelib_entry.c runtime/hle/hle_symbols.txt runtime/cpu.h runtime/simd.h runtime/simd_fp.h \
                runtime/gamelib.h
GAMELIB_ID := $(shell (cat $(GAMELIB_SRCS); echo '$(GENFLAGS)') | sha256sum | cut -c1-16)
GAMELIB_BUILD := $(shell od -An -tx1 -j64 -N8 extracted/exefs/main 2>/dev/null | tr -d ' \n' | tr a-f A-F)
GAMELIB_FULL_ID := $(GAMELIB_ID)-$(or $(GAMELIB_BUILD),unknown)

DLLS     := $(B)/dlls.stamp

all: $(EXE) $(LAUNCHER) $(DLLS) $(if $(GEN_SRC),$(GAMELIB))

# ---- the game code: generated C from the game's own NSOs (never committed)
gen:
	cd tools && ../$(PY) gen.py game ../extracted/exefs/main 0x7000000000 ../$(GEN)/game --simd c
	cd tools && ../$(PY) gen.py sdk ../extracted/exefs/sdk 0x7004000000 ../$(GEN)/sdk --hle ../runtime/hle/hle_symbols.txt --simd c
	cd tools && ../$(PY) gen.py glslc ../extracted/exefs/subsdk0 0x7005000000 ../$(GEN)/glslc --simd c

$(B)/obj/%.o: $(GEN)/%.c runtime/cpu.h runtime/simd.h runtime/simd_fp.h
	@mkdir -p $(dir $@)
	@$(CC) $(GENFLAGS) -c $< -o $@

$(B)/obj/tools/gamelib_entry.o: tools/gamelib_entry.c $(GAMELIB_SRCS) $(wildcard extracted/exefs/main)  # (its id names the main program)
	@mkdir -p $(dir $@)
	$(CC) $(GENFLAGS) -DGAMELIB_ID='"$(GAMELIB_FULL_ID)"' -c $< -o $@

$(GAMELIB): $(GEN_OBJ) | $(EXE)
	$(CC) -shared -fuse-ld=lld $(B)/doax3recomp.dll.a -o $@.tmp $^ $(B)/doax3recomp.dll.a
	mv -f $@.tmp $@
	echo $(GAMELIB_FULL_ID) > $@.id

# ---- the runtime
$(B)/rt/gamelib.o: RTFLAGS += -DGAMELIB_ID='"$(GAMELIB_ID)"'
$(B)/rt/gamelib.o: $(GAMELIB_SRCS)

$(B)/rt/%.o: runtime/%.c $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(RTFLAGS) -c $< -o $@

$(B)/rt/%.o: runtime/%.cpp $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/rt/ui/%.o: runtime/ui/%.mm $(RT_HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

$(B)/inc/smaa_hlsl.inc: third_party/smaa/SMAA.hlsl
	@mkdir -p $(dir $@)
	LC_ALL=C tr -d '\r' < $< | LC_ALL=C tr '\200-\377' '?' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' > $@
$(B)/rt/nvn/smaa_data.o: $(B)/inc/smaa_hlsl.inc
$(B)/rt/nvn/smaa_data.o: RTFLAGS += -I$(B)/inc -Ithird_party/smaa/Textures

$(B)/inc/shader_variants.inc: runtime/nvn/shader_variants.txt
	@mkdir -p $(dir $@)
	(LC_ALL=C tr -d '\r' < $< | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/'; echo '""') > $@
$(B)/rt/nvn/shader_prep.o: $(B)/inc/shader_variants.inc
TRANSLATOR_ID := $(shell cat runtime/nvn/maxwell/translate.c runtime/nvn/maxwell/shader.h | sha256sum | cut -c1-12)
$(B)/rt/nvn/shader_prep.o: RTFLAGS += -I$(B)/inc -DSHADER_TRANSLATOR_ID='"$(TRANSLATOR_ID)"'

$(B)/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(B)/imgui/imgui_impl_%.o: $(IMGUI)/backends/imgui_impl_%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- the exes' icon: this project's own (assets/icon.ico). SDL gives the game's window the exe's first icon.
# make GAME_ICON=1 uses your copy's home menu icon instead, from your game files (tools/make_icon.py): for your own PC
# only, as it's the game's artwork.
ICON     := $(B)/icon/icon.ico
ifeq ($(GAME_ICON),1)
$(B)/icon/icon.ico: extracted/control/icon_AmericanEnglish.dat tools/make_icon.py
	@mkdir -p $(dir $@)
	$(PY) tools/make_icon.py $< $@
else
$(B)/icon/icon.ico: assets/icon.ico
	@mkdir -p $(dir $@)
	cp $< $@
endif

$(B)/icon/icon.o: $(B)/icon/icon.ico
	echo '1 ICON "icon.ico"' > $(B)/icon/icon.rc
	cd $(B)/icon && windres icon.rc -O coff -o icon.o

$(EXE): $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ) $(IMGUI_OBJ) $(if $(ICON),$(B)/icon/icon.o)
	@mkdir -p $(dir $@)link
	$(CXX) -o $(dir $@)link/$(notdir $@) $^ $(LDFLAGS)
	mv -f $(dir $@)link/$(notdir $@) $@

# ---- the MSYS2 libraries next to the game, so it starts without MSYS2 on PATH and survives MSYS2 updates. When
# MSYS2 no longer has a library it was linked with (exit 2), the game is linked again against today's.
$(DLLS): $(EXE) tools/copy_dlls.sh
	sh tools/copy_dlls.sh $(EXE) || { [ $$? -eq 2 ] && rm -f $(EXE) && $(MAKE) $(EXE) && sh tools/copy_dlls.sh $(EXE); }
	touch $@

# ---- the launcher (display mode and resolution, then the game): launcher/
$(B)/launcher/res.o: launcher/launcher.rc launcher/launcher.manifest $(ICON)
	@mkdir -p $(dir $@)
	cd launcher && windres $(if $(ICON),-DHAVE_ICON -I../$(B)/icon) launcher.rc -O coff -o ../$@

$(B)/launcher/launcher.o: launcher/launcher.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -Wall -municode -c $< -o $@

$(LAUNCHER): $(B)/launcher/launcher.o $(B)/launcher/res.o
	$(CC) -fuse-ld=lld -municode -mwindows -o $@ $^ -lcomctl32 -lshell32

runtime-objs: $(RT_OBJ) $(BACKEND_OBJ) $(UI_OBJ)

clean:
	rm -rf $(B)

.PHONY: all gen runtime-objs clean
