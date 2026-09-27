# jack-graph -- a JACK and ALSA MIDI connection graph.
#
#   jack-graph        the graph window, its JACK Settings window and its About card
#   tools/uirender    the offline layout audit: no X server, no sound card, no JACK
#
# X11 + Cairo + FreeType and nothing else. NO GTK, NO GDK, NO PANGO, NO GOBJECT, NO GIO, NO GLIB.
#
# THE GFX LAYER AND THE PANELS DELIBERATELY DO NOT SEE X11. src/gfx/, graphpanel.cpp,
# settingspanel.cpp and chrome.cpp need cairo and nothing more, which is what lets tools/uirender
# compose and audit the real layouts headlessly -- in CI, on an aarch64 runner with no display.
# Keep it that way: an #include of Xlib.h there fails to compile under the rules below rather than
# quietly costing the audit. src/platform/ is the only place X11 lives, and src/main.cpp the only
# file outside it that sees an X header.
#
#   make                  build jack-graph and tools/uirender
#   make WERROR=1         the same with -Werror, which is what CI runs
#   make install          install under $(DESTDIR)$(PREFIX): binary, fonts, desktop entry
#   make PREFIX=/usr      build for /usr (the .deb); the prefix is compiled in as a font fallback

CXX        ?= g++
PKG_CONFIG ?= pkg-config
INSTALL    ?= install

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin
SHAREDIR ?= $(PREFIX)/share/jack-graph
APPSDIR  ?= $(PREFIX)/share/applications

# ONE VERSION, IN ONE FILE. The About card, --version, the release tarball's name and the .deb all
# read ./VERSION; bump it when cutting a release.
VERSION := $(shell tr -d '[:space:]' < VERSION)
ifeq ($(VERSION),)
$(error VERSION is empty or missing)
endif

# --- packages ----------------------------------------------------------------------------------
GFX_PKGS  = cairo cairo-ft freetype2
X11_PKGS  = cairo-xlib x11
JACK_PKGS = jack
ALSA_PKGS = alsa

GFX_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(GFX_PKGS))
GFX_LIBS    := $(shell $(PKG_CONFIG) --libs   $(GFX_PKGS))
X11_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(X11_PKGS))
X11_LIBS    := $(shell $(PKG_CONFIG) --libs   $(X11_PKGS))
JACK_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(JACK_PKGS))
JACK_LIBS   := $(shell $(PKG_CONFIG) --libs   $(JACK_PKGS))
ALSA_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(ALSA_PKGS))
ALSA_LIBS   := $(shell $(PKG_CONFIG) --libs   $(ALSA_PKGS))

# --- warnings and hardening --------------------------------------------------------------------
# -Werror is OFF by default: install.sh builds from source on whatever compiler the user has, and
# a warning a newer GCC invents must not stop an install. CI builds with WERROR=1, and gates on the
# build log besides, so a warning never reaches a release.
WARN = -Wall -Wextra -Wformat=2 -Wformat-security
ifeq ($(WERROR),1)
WARN += -Werror
endif
HARDEN   = -O2 -fstack-protector-strong -fstack-clash-protection -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -fPIE
LDHARDEN = -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

# -fcf-protection is x86 only; aarch64 has -mbranch-protection instead.
ARCH := $(shell $(CXX) -dumpmachine)
ifneq ($(findstring x86_64,$(ARCH)),)
HARDEN += -fcf-protection=full
endif
ifneq ($(findstring aarch64,$(ARCH)),)
HARDEN += -mbranch-protection=standard
endif

DEFS = -DJACKGRAPH_RESOURCE_DIR_DEFAULT=\"$(SHAREDIR)\" -DJACKGRAPH_VERSION=\"$(VERSION)\"

# -MMD -MP writes a .d file beside each .o listing the headers it included, so editing a header
# rebuilds everything that includes it -- this tree is full of header-only geometry with
# static_asserts in it, exactly the kind of edit that would otherwise be silently skipped.
DEPFLAGS = -MMD -MP

CXXFLAGS ?=
CXXFLAGS += -std=c++17 $(WARN) $(HARDEN) $(DEFS) $(DEPFLAGS) -Isrc

# --- objects -----------------------------------------------------------------------------------
# Cairo only, no X11: everything tools/uirender can link.
GFX_OBJS = src/gfx/canvas.o src/gfx/fontstack.o src/gfx/widgets.o src/gfx/combo.o

# The only place X11 lives.
PLAT_OBJS = src/platform/xerror.o src/platform/respath.o src/platform/fs.o \
            src/platform/wakepipe.o src/platform/x11window.o

# The model: the two servers, the config, and the pure-data graph types.
MODEL_OBJS = src/JackClient.o src/AlsaClient.o src/Config.o src/JackServerControl.o \
             src/Node.o src/Connection.o src/ClientBox.o

# The three cairo-only panels, and the application that drives them.
VIEW_OBJS = src/graphpanel.o src/settingspanel.o src/chrome.o src/app.o

TARGET = jack-graph
OBJS   = $(GFX_OBJS) $(PLAT_OBJS) $(MODEL_OBJS) $(VIEW_OBJS) src/main.o

# The audit links the gfx objects, the three panels, the pure-data model types they lay out, the
# config the settings panel reads, and respath/fs -- AND NOTHING ELSE. No X11, no JACK, no ALSA:
# if this list ever has to grow past that, something has reached across the line drawn above.
UIRENDER_TARGET = tools/uirender
UIRENDER_OBJS = $(GFX_OBJS) src/graphpanel.o src/settingspanel.o src/chrome.o \
                src/Node.o src/Connection.o src/ClientBox.o src/Config.o \
                src/platform/respath.o src/platform/fs.o tools/uirender.o

.PHONY: all clean install uninstall

all: $(TARGET) $(UIRENDER_TARGET)

# gfx objects: cairo only. NO X11_CFLAGS here, deliberately -- see the header comment.
src/gfx/%.o: src/gfx/%.cpp
	$(CXX) $(CXXFLAGS) $(GFX_CFLAGS) -c -o $@ $<

src/platform/%.o: src/platform/%.cpp
	$(CXX) $(CXXFLAGS) $(GFX_CFLAGS) $(X11_CFLAGS) -c -o $@ $<

# Everything else under src/: cairo plus the two servers the model talks to, and NO X11.
src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(GFX_CFLAGS) $(JACK_CFLAGS) $(ALSA_CFLAGS) -c -o $@ $<

# THE ONE EXCEPTION, explicit so it cannot spread: main.cpp is where the windows and the
# application meet, so it is the only file under src/ outside platform/ that sees an X header.
src/main.o: src/main.cpp
	$(CXX) $(CXXFLAGS) $(GFX_CFLAGS) $(X11_CFLAGS) $(JACK_CFLAGS) $(ALSA_CFLAGS) -c -o $@ $<

# The audit: cairo only.
tools/%.o: tools/%.cpp
	$(CXX) $(CXXFLAGS) $(GFX_CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDHARDEN) -o $@ $(OBJS) $(GFX_LIBS) $(X11_LIBS) $(JACK_LIBS) $(ALSA_LIBS)

$(UIRENDER_TARGET): $(UIRENDER_OBJS)
	$(CXX) $(CXXFLAGS) $(LDHARDEN) -o $@ $(UIRENDER_OBJS) $(GFX_LIBS)

DEPS = $(OBJS:.o=.d) tools/uirender.d
-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) tools/uirender.o $(TARGET) $(UIRENDER_TARGET)

# The fonts are not optional: the window draws its own text through FreeType, and without them it
# falls back to a cairo toy face whose metrics are not the ones the layout was audited against.
install: $(TARGET)
	$(INSTALL) -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(SHAREDIR)/fonts $(DESTDIR)$(APPSDIR)
	$(INSTALL) -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	$(INSTALL) -m 0644 resources/fonts/* $(DESTDIR)$(SHAREDIR)/fonts/
	$(INSTALL) -m 0644 resources/jack-graph.desktop $(DESTDIR)$(APPSDIR)/jack-graph.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET) $(DESTDIR)$(APPSDIR)/jack-graph.desktop
	rm -rf $(DESTDIR)$(SHAREDIR)
