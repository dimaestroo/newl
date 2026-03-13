_VERSION = 0.0.1
VERSION  = `git describe --tags --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG = pkg-config

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share
# Ly scans /usr/share/wayland-sessions by default, even when binaries live in /usr/local.
WAYLANDSESSIONSDIR = /usr/share/wayland-sessions
# Force logind for display-manager sessions. Leave empty to use libseat autodetect.
SESSIONENV = LIBSEAT_BACKEND=logind

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.19`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.19`

# Allow using an alternative wlroots installations
# This has to have all the includes required by wlroots, e.g:
# Assuming wlroots git repo is "${PWD}/wlroots" and you only ran "meson setup build && ninja -C build"
#WLR_INCS = -I/usr/include/pixman-1 -I/usr/include/elogind -I/usr/include/libdrm \
#	-I$(PWD)/wlroots/include
# Set -rpath to avoid using the wrong library.
#WLR_LIBS = -Wl,-rpath,$(PWD)/wlroots/build -L$(PWD)/wlroots/build -lwlroots-0.19

# Assuming you ran "meson setup --prefix ${PWD}/0.19 build && ninja -C build install"
#WLR_INCS = -I/usr/include/pixman-1 -I/usr/include/elogind -I/usr/include/libdrm \
#	-I$(PWD)/wlroots/0.19/include/wlroots-0.19
#WLR_LIBS = -Wl,-rpath,$(PWD)/wlroots/0.19/lib64 -L$(PWD)/wlroots/0.19/lib64 -lwlroots-0.19
# Uncomment to build XWayland support
XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm

CC = cc
