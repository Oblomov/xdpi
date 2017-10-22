xcb?=1
CPPFLAGS=-g -Wall -Wextra -DWITH_XCB=$(xcb) -Werror
CFLAGS=-std=c99
LDLIBS=-lm -lX11 -lXrandr -lXinerama

LDLIBS_xcb1=-lxcb -lxcb-randr -lxcb-xinerama -lxcb-xrm

LDLIBS += $(LDLIBS_xcb${xcb})

RM ?= rm -rf

xdpi: xdpi.c

clean:
	$(RM) xdpi
