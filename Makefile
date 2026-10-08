# MAIL - an email client for the Atari ST/STE/TT/Falcon, in C.
#
#   make            builds MAIL.PRG with a stock m68k ELF cross compiler
#                   (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
#                   No MiNTLib needed: the program is freestanding and
#                   tools/elf2tos.py writes the TOS header.
#   make test       builds the mail core for this computer and runs the
#                   unit tests (tests/); `make itest` also runs the
#                   protocol tests against a local Dovecot.
#
# The Atari build follows Claude ST (github.com/whomper/atari_claude).

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DMAIL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Isrc -Iatari
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

CORE    := util charset mime conn imap pop3 smtp store compose mail futil bidi
ATARI   := libc tos plat_tos net_tos sting gem
UI      := main draw win folders list reader editor dialogs

OBJDIR  := build
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h)

all: MAIL.PRG

MAIL.PRG: $(OBJDIR)/mail.elf tools/elf2tos.py
	python3 tools/elf2tos.py $< $@

$(OBJDIR)/mail.elf: $(OBJS) atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/crt0.o: atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/sting_s.o: atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR):
	mkdir -p $@

# ---- host build: the same mail core, for tests and mail-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -fsanitize=address,undefined
HOSTSRC    := $(addprefix src/,$(addsuffix .c,$(CORE))) tests/host/plat_posix.c

$(OBJDIR)/mail-cli: tests/host/mail_cli.c $(HOSTSRC) $(HDRS) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/host/mail_cli.c $(HOSTSRC)

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ tests/unit.c $(HOSTSRC)

test: $(OBJDIR)/unit
	$(OBJDIR)/unit

itest: $(OBJDIR)/mail-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) MAIL.PRG

.PHONY: all test itest clean
