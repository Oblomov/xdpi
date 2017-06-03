CPPFLAGS=-g -Wall -Wextra
CFLAGS=-std=c99
LDLIBS=-lm -lX11 -lXrandr -lXinerama -lxcb -lxcb-randr -lxcb-xinerama

RM ?= rm -rf

xdpi: xdpi.c

clean:
	$(RM) xdpi
