#BIN := focm
ENGINE_BIN := focm_engine
UI_BIN := focm_ui

#SRC := src/main.cpp \
       src/file_io/object_loader.cpp \
       src/record.cpp \
       src/engine/object.cpp \
       src/depricated/physics.cpp \
       src/engine/ship.cpp \
       src/engine/planet.cpp \
       src/engine/command.cpp \
       src/file_io/save_loader.cpp \
       src/file_io/buttons_loader.cpp \
       src/file_io/config_loader.cpp \
       src/file_io/ui_config_loader.cpp \
       src/file_io/scene_loader.cpp \
       src/ui/object_ui_factory.cpp \
       src/ui/ui_scene_builder.cpp \
       src/ui/object_selectable.cpp

UI_SRC := src/ui.cpp \
        src/file_io/config_loader.cpp \
        src/file_io/object_loader.cpp \
        src/file_io/ui_config_loader.cpp \
        src/file_io/buttons_loader.cpp \
        src/file_io/hash_utils.cpp \
        src/ui/menu.cpp \
        src/engine/object.cpp \
        src/depricated/physics.cpp \
        src/stream_io/tcp_protocol.cpp 

# Engine-only sources (no SDL/UI)
ENGINE_SRC := src/engine.cpp \
        src/file_io/config_loader.cpp \
        src/file_io/object_loader.cpp \
        src/file_io/save_loader.cpp \
        src/file_io/scene_loader.cpp \
        src/file_io/hash_utils.cpp \
        src/stream_io/tcp_protocol.cpp \
        src/stream_io/server.cpp \
        src/engine/object.cpp \
        src/engine/ship.cpp \
        src/engine/planet.cpp \
        src/engine/command.cpp \
        src/depricated/physics.cpp

CXX := g++

# SDL2
CXXFLAGS := -std=gnu++17 -Wall -Wextra -O2 $(shell sdl2-config --cflags)
LDFLAGS := $(shell sdl2-config --libs)

# SDL2_image (PNG)
CXXFLAGS += $(shell pkg-config --cflags SDL2_image 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs SDL2_image 2>/dev/null || echo -lSDL2_image)

# SDL2_ttf (TrueType fonts)
CXXFLAGS += $(shell pkg-config --cflags SDL2_ttf 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs SDL2_ttf 2>/dev/null || echo -lSDL2_ttf)

# json-c
CXXFLAGS += $(shell pkg-config --cflags json-c 2>/dev/null)
LDFLAGS += $(shell pkg-config --libs json-c 2>/dev/null || echo -ljson-c)

CXXFLAGS += -Isrc -Isrc/file_io -Isrc/ui -Isrc/depricated -Isrc/engine
all: $(ENGINE_BIN) $(UI_BIN)
#all: $(BIN) $(ENGINE_BIN) $(UI_BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Build the headless engine binary without SDL libs
ENGINE_CXXFLAGS := -std=gnu++17 -Wall -Wextra -O2 -Isrc -Isrc/file_io -Isrc/engine -Isrc/depricated $(shell pkg-config --cflags json-c 2>/dev/null)
ENGINE_LDFLAGS := $(shell pkg-config --libs json-c 2>/dev/null || echo -ljson-c)

$(ENGINE_BIN): $(ENGINE_SRC)
	$(CXX) $(ENGINE_CXXFLAGS) -o $@ $(ENGINE_SRC) $(ENGINE_LDFLAGS)


$(UI_BIN): $(UI_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(UI_SRC) $(LDFLAGS)

.PHONY: clean run
clean:
	rm -f $(ENGINE_BIN)
	rm -f $(UI_BIN)

run: $(ENGINE_BIN) $(UI_BIN)
	./start.sh
