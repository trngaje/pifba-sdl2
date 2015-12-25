# Comilation file for RPI
PROG_NAME = ../fbacapex
OBJS = capex.o
LDFLAGS = 

CC = gcc
CXX = g++
STRIP = strip

# output from sdl-config
SDL_FLAGS=-I/usr/include/SDL2 -I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include
SDL_LIBS=-lSDL2 -lm -ldl -lglib-2.0

CFLAGS = $(SDL_FLAGS) -O2 -funroll-loops -Wextra -Werror
CXXFLAGS = $(SDL_FLAGS) -O2 -funroll-loops -Wall
LIBS = $(SDL_LIBS)  -L/opt/vc/lib -lrt

TARGET = $(PROG_NAME)

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)
#	$(STRIP) $(TARGET)

clean:
	/bin/rm -rf *~ *.o $(TARGET)
