CXX ?= clang++
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
# SDL2_CFLAGS folded in globally: pattern-rule specificity in this Makefile
# doesn't reliably beat the generic $(BUILD)/%.o rule, and several non-SDL
# files (engine.h) transitively need SDL2's headers anyway.
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Iinclude -Ithird_party -Wno-unused-parameter -pthread $(SDL2_CFLAGS)
AUDIO_CXXFLAGS := $(CXXFLAGS)

BUILD := build

DSP_SRCS := $(wildcard src/dsp/*.cpp)
DSP_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(DSP_SRCS))

GFX_SRCS := $(wildcard src/gfx/*.cpp)
GFX_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(GFX_SRCS))

APP_SRCS := $(wildcard src/app/*.cpp)
APP_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(APP_SRCS))

AUDIO_SRCS := $(wildcard src/audio/*.cpp)
AUDIO_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(AUDIO_SRCS))

UNAME_S := $(shell uname -s)

.PHONY: all test run clean

ifeq ($(UNAME_S),Darwin)
all: test app
else
all: test $(BUILD)/xmad
endif

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/main.o: src/main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SDL2_CFLAGS) -c $< -o $@

$(BUILD)/audio/%.o: src/audio/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(AUDIO_CXXFLAGS) $(SDL2_CFLAGS) -c $< -o $@

# --- fft smoke test ----------------------------------------------------------
$(BUILD)/fft_smoke_test: tests/fft_smoke_test.cpp $(DSP_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

# --- bitmap font render test --------------------------------------------------
$(BUILD)/font_render_test: tests/font_render_test.cpp $(GFX_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

# --- decoder test --------------------------------------------------------------
$(BUILD)/decoder_test: tests/decoder_test.cpp $(AUDIO_OBJS) $(DSP_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(AUDIO_CXXFLAGS) $(SDL2_CFLAGS) $^ -o $@ $(SDL2_LIBS)

# --- engine smoke test (real-time playback through the real audio device) -----
$(BUILD)/engine_smoke_test: tests/engine_smoke_test.cpp $(AUDIO_OBJS) $(DSP_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(AUDIO_CXXFLAGS) $^ -o $@ $(SDL2_LIBS)

# --- equalizer test --------------------------------------------------------------
$(BUILD)/eq_test: tests/eq_test.cpp $(AUDIO_OBJS) $(DSP_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(AUDIO_CXXFLAGS) $^ -o $@ $(SDL2_LIBS)

# --- window snap (magnetic docking) test - no SDL dependency ------------------
$(BUILD)/window_snap_test: tests/window_snap_test.cpp $(BUILD)/app/window_snap.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude $^ -o $@

# --- file dialog (Eject button) test - pure parsing only, no SDL --------------
$(BUILD)/file_dialog_test: tests/file_dialog_test.cpp $(BUILD)/app/file_dialog.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude $^ -o $@

# --- session persistence (save-settings-on-exit) test - pure parsing only, no SDL --
$(BUILD)/session_test: tests/session_test.cpp $(BUILD)/app/session.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude $^ -o $@

# --- XSound stereo widener test - pure math, no SDL -----------------------
$(BUILD)/stereo_widen_test: tests/stereo_widen_test.cpp $(BUILD)/audio/stereo_widen.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude $^ -o $@

# --- channel-level LED gradient test - pure color math, no SDL -------------
$(BUILD)/level_meter_test: tests/level_meter_test.cpp $(BUILD)/gfx/level_meter.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude $^ -o $@

# --- ID3 title parsing test - pure parsing only, no SDL ---------------------
# (links dr_impl.o for dr_flac's implementation, which tags.o's FLAC path
# calls into - only the pure ID3 functions are actually exercised by this
# test binary, but the symbols still need to resolve.)
$(BUILD)/tags_test: tests/tags_test.cpp $(BUILD)/audio/tags.o $(BUILD)/audio/dr_impl.o
	@mkdir -p $(BUILD)
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Iinclude -Ithird_party $^ -o $@

# --- the app -------------------------------------------------------------------
$(BUILD)/xmad: $(BUILD)/main.o $(GFX_OBJS) $(APP_OBJS) $(DSP_OBJS) $(AUDIO_OBJS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SDL2_LIBS)

# --- macOS .app bundle, so a Dock/Finder icon (the real extracted SKULL.ico
# resource) actually applies - a bare Unix executable gets the generic
# terminal icon, since Dock icons come from CFBundleIconFile in an app
# bundle's Info.plist, not from anything SDL can set at runtime. ------------
ifeq ($(UNAME_S),Darwin)
APP_VERSION := $(shell grep -o 'kVersion = "[^"]*"' include/app/version.h | sed 's/.*"\(.*\)"/\1/')
APP_BUNDLE := $(BUILD)/xmad.app

.PHONY: app
app: $(APP_BUNDLE)

$(APP_BUNDLE): $(BUILD)/xmad assets/icon/Info.plist assets/icon/AppIcon.icns $(wildcard assets/skin/*)
	@mkdir -p "$(APP_BUNDLE)/Contents/MacOS" "$(APP_BUNDLE)/Contents/Resources"
	cp $(BUILD)/xmad "$(APP_BUNDLE)/Contents/MacOS/xmad"
	cp assets/icon/AppIcon.icns "$(APP_BUNDLE)/Contents/Resources/AppIcon.icns"
	sed 's/@VERSION@/$(APP_VERSION)/g' assets/icon/Info.plist > "$(APP_BUNDLE)/Contents/Info.plist"
	rm -rf "$(APP_BUNDLE)/Contents/Resources/skin"
	cp -R assets/skin "$(APP_BUNDLE)/Contents/Resources/skin"
	touch "$(APP_BUNDLE)"
endif

test: $(BUILD)/fft_smoke_test $(BUILD)/font_render_test $(BUILD)/decoder_test $(BUILD)/engine_smoke_test $(BUILD)/eq_test $(BUILD)/window_snap_test $(BUILD)/file_dialog_test $(BUILD)/session_test $(BUILD)/stereo_widen_test $(BUILD)/tags_test $(BUILD)/level_meter_test
	./$(BUILD)/fft_smoke_test
	./$(BUILD)/font_render_test "../xmplayer/Source/Img_new/DISPLAY.bmp" $(BUILD)/font_render.raw
	./$(BUILD)/decoder_test
	./$(BUILD)/engine_smoke_test tests/fixtures/tone.mp3
	./$(BUILD)/eq_test
	./$(BUILD)/window_snap_test
	./$(BUILD)/file_dialog_test
	./$(BUILD)/session_test
	./$(BUILD)/stereo_widen_test
	./$(BUILD)/tags_test
	./$(BUILD)/level_meter_test

ifeq ($(UNAME_S),Darwin)
# Launched through the .app bundle via `open` (not the raw binary) so
# the real skull-and-crossbones Dock icon actually shows - confirmed
# this session that `open`-launching the bundle is what makes macOS
# read CFBundleIconFile; running the raw binary never does, bundle or not.
run: app
	open "$(APP_BUNDLE)" --args assets/skin tests/fixtures/track_a.mp3 tests/fixtures/track_b.mp3 tests/fixtures/track_c.flac
else
run: $(BUILD)/xmad
	./$(BUILD)/xmad assets/skin tests/fixtures/track_a.mp3 tests/fixtures/track_b.mp3 tests/fixtures/track_c.flac
endif

clean:
	rm -rf $(BUILD)
