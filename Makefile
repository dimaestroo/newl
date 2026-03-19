.POSIX:
.SUFFIXES:

include config.mk

NEWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)

NEWLDEVCFLAGS = -g \
	-Wpedantic -Wall -Wextra \
	-Wdeclaration-after-statement \
	-Wno-unused-parameter \
	-Wshadow \
	-Wunused-macros \
	-Werror=implicit \
	-Werror=return-type \
	-Werror=incompatible-pointer-types \
	-Wfloat-conversion

NEWLRELEASEOPTFLAGS = \
	-O3 \
	-march=native \
	-ffast-math \
	-flto=auto \
	-fno-fat-lto-objects \
	-ffunction-sections \
	-fdata-sections \
	-fno-omit-frame-pointer \
	-fno-plt

NEWLDEBUGOPTFLAGS = \
	-ggdb3 \
	-O0 \
	-fno-omit-frame-pointer \
	-fno-inline \
	-fno-optimize-sibling-calls \
	-fno-ipa-cp \
	-fno-ipa-sra \
	-fno-plt

PKGS = wayland-server xkbcommon libinput $(XLIBS)
NEWLBASECFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(NEWLCPPFLAGS) $(NEWLDEVCFLAGS)
NEWLRELEASECFLAGS = $(NEWLBASECFLAGS) $(NEWLRELEASEOPTFLAGS)
NEWLDEBUGCFLAGS = $(NEWLBASECFLAGS) $(NEWLDEBUGOPTFLAGS)

NEWLRELEASELDFLAGS = $(LDFLAGS) \
	-flto \
	-Wl,--gc-sections \
	-Wl,--as-needed \
	-Wl,-O3 \
	-Wl,--sort-common \
	-Wl,--relax

NEWLDEBUGLDFLAGS = $(LDFLAGS)

LDLIBS = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

RELEASE_OBJS = newl.o dwl-ipc-unstable-v2-protocol.o
DEBUG_OBJS = newl-debug.o dwl-ipc-unstable-v2-protocol-debug.o

all: newl
debug: newl-debug

newl: $(RELEASE_OBJS)
	$(CC) $(RELEASE_OBJS) $(NEWLRELEASECFLAGS) $(NEWLRELEASELDFLAGS) $(LDLIBS) -o $@

newl-debug: $(DEBUG_OBJS)
	$(CC) $(DEBUG_OBJS) $(NEWLDEBUGCFLAGS) $(NEWLDEBUGLDFLAGS) $(LDLIBS) -o $@

newl.o: newl.c config.h config.mk cursor-shape-v1-protocol.h \
	ext-image-copy-capture-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	dwl-ipc-unstable-v2-protocol.h
	$(CC) $(CPPFLAGS) $(NEWLRELEASECFLAGS) -o $@ -c $<

newl-debug.o: newl.c config.h config.mk cursor-shape-v1-protocol.h \
	ext-image-copy-capture-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	dwl-ipc-unstable-v2-protocol.h
	$(CC) $(CPPFLAGS) $(NEWLDEBUGCFLAGS) -o $@ -c $<

dwl-ipc-unstable-v2-protocol.o: dwl-ipc-unstable-v2-protocol.c dwl-ipc-unstable-v2-protocol.h
	$(CC) $(CPPFLAGS) $(NEWLRELEASECFLAGS) -o $@ -c $<

dwl-ipc-unstable-v2-protocol-debug.o: dwl-ipc-unstable-v2-protocol.c dwl-ipc-unstable-v2-protocol.h
	$(CC) $(CPPFLAGS) $(NEWLDEBUGCFLAGS) -o $@ -c $<

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@

ext-image-copy-capture-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@

pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@

wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@

wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

dwl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/dwl-ipc-unstable-v2.xml $@

dwl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-ipc-unstable-v2.xml $@

clean:
	rm -f newl newl-debug *.o *-protocol.h *-protocol.c *.gcda *.gcno

dist: clean
	mkdir -p newl-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md config.h \
		config.mk protocols newl.1 newl.c newl.desktop \
		newl-$(VERSION)
	tar -caf newl-$(VERSION).tar.gz newl-$(VERSION)
	rm -rf newl-$(VERSION)

install: newl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/newl
	cp -f newl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/newl
	mkdir -p $(DESTDIR)$(WAYLANDSESSIONSDIR)
	rm -f $(DESTDIR)$(DATADIR)/wayland-sessions/newl.desktop \
		$(DESTDIR)$(WAYLANDSESSIONSDIR)/newl.desktop
	sed \
		-e 's|^Exec=.*|Exec=/usr/bin/env $(SESSIONENV) $(PREFIX)/bin/newl|' \
		-e 's|^TryExec=.*|TryExec=$(PREFIX)/bin/newl|' \
		newl.desktop > $(DESTDIR)$(WAYLANDSESSIONSDIR)/newl.desktop
	chmod 644 $(DESTDIR)$(WAYLANDSESSIONSDIR)/newl.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/newl \
		$(DESTDIR)$(DATADIR)/wayland-sessions/newl.desktop \
		$(DESTDIR)$(WAYLANDSESSIONSDIR)/newl.desktop
