CC= clang++
CFLAGS= -c -Wall -std=c++14 -Wno-unused-const-variable -Wno-missing-braces -O3
#CFLAGS+= -DDEBUG_CPU
INCLUDE= \
	-I. \
	-Iinclude \
	-I/System/Library/Frameworks/OpenGL.framework/Headers \
	-I/usr/local/include/SDL2 \
	-I/opt/local/include \
	-I/usr/include/GL \
	-I/usr/include/SDL2 

LDFLAGS= -L/System/Library/Frameworks/OpenGL.framework/Versions/A/Libraries -L/opt/local/lib
LIBS= -lSDL2 -lGL -lsamplerate \
	`imlib2-config --cflags` `imlib2-config --libs`


SOURCES= \
	src/apu.cpp     \
	src/cpu.cpp     \
	src/ppu.cpp     \
	src/rom.cpp     \
	src/nes.cpp     \
	src/cpu-map.cpp     \
	src/cpu-asm.cpp     \
	src/controller_std.cpp \
	src/audio_sdl.cpp \
	src/audio_sox_pipe.cpp \
	src/video_sdl.cpp \
	src/video_tty.cpp \
	src/video_autosnapshot.cpp \
	src/input_sdl.cpp \
	src/input_script.cpp \
	src/input_script_recorder.cpp \
	src/mappers/sxrom.cpp \
	src/mappers/uxrom.cpp \
	src/mappers/cnrom.cpp \
	src/mappers/mmc3.cpp \
	src/image.cpp \
	main.cpp

OBJECTS= $(SOURCES:.cpp=.o)

all: nes

nes: $(OBJECTS)
	$(CC) $(LDFLAGS) $(LIBS) $(OBJECTS) -o nes

.cpp.o:
	$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

clean:
	rm -f $(OBJECTS) nes
