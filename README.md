# X11 DPI information retrieval

This is a small C program that retrieves all information about DPI (dots
per inch) of the available displays in X11. Information from both the
core protocol and the XRANDR extension is presented. Xinerama
information (which lacks physical dimensions, and is thus not directly
useful to determine output DPI) is also presented.

To improve the usefulness of `xdpi` as a debugging tool, we also show
the content of the following known-relevant environment variables,
if set:

* `CLUTTER_SCALE`
* `GDK_SCALE`
* `GDK_DPI_SCALE`
* `QT_AUTO_SCREEN_SCALE_FACTOR`
* `QT_SCALE_FACTOR`
* `QT_SCREEN_SCALE_FACTORS`
* `QT_DEVICE_PIXEL_RATIO`

<!-- TODO link the relevant variables to the pages
     explaining their purpose -->

All of the code is licensed under the Mozilla Public License, version 2.
See `LICENSE.txt` for details.

## Running

Simply:

    ./xdpi

The program currently accepts no options.

## Compiling

Simply run:

    make

You will need a compiler supporting C99, and development files for Xlib,
and the XRANDR and Xinerama extensions.

For xcb support, you will also need the development files for xcb-xrandr,
xcb-xinerama and xcb-xrm.

If you do not have xcb or your xcb version is too old, you can compile
without xcb support by running

    make xcb=0

## Why both Xlib and xcb?

Mostly, because I wanted to have a look at xcb and how different it was
from Xlib. My usage is probably imperfect, but it does show how much
more complex fully taking advantage of the asynchronous nature of the
X11 protocol (which is what xcb is all about) is.

# Qt

A simple program to illustrate how Qt 5.6 and higher handle DPI
information depending on the application settings
`Qt::AA_EnableHighDpiScaling` and `Qt::AA_DisableHighDpiScaling` can be
found in the `qt` directory.

Build it with

    qmake && make

and then run it with

    ./qtdpi

If your `qmake` by defaults builds against Qt4, run `qtmake -qt=5`
before `make`.
