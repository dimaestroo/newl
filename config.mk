_VERSION = 0.0.1
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share
WAYLANDSESSIONSDIR = /usr/share/wayland-sessions

# Force logind for display-manager sessions. Leave empty to use libseat autodetect.
SESSIONENV = LIBSEAT_BACKEND=logind

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.20`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.20`

# Flag to build XWayland support
XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm

CC = cc
