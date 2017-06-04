# X11 DPI information retrieval

This is a small C program that retrieves all information about DPI (dots
per inch) of the available displays in X11. Information from both the
core protocol and the XRANDR extension is presented. Xinerama
information (which lacks physical dimensions, though) is also presented.

Licensed under the Mozilla Public License, version 2. See LICENSE.txt.

## Running

Simply:

    ./xdpi

The program currently accepts no options.

## Compiling

Simply run:

    make

You will need a compiler supporting C99, and development files for Xlib,
the XRANDR and Xinerama extensions, xcb, xcb-xrandr and xcb-xinerama.

## Why both Xlib and xcb?

Mostly, because I wanted to have a look at xcb and how different it was
from Xlib. My usage is probably imperfect, but it does show how much
more complex fully taking advantage of the asynchronous nature of the
X11 protocol (which is what xcb is all about) is.
