#!/bin/bash
# Builds the TLS measurements for the Falcon:
#   TLSB030.PRG  BearSSL 0.6 on the 68030: each piece of a TLS handshake
#   TLSDSP.PRG   Montgomery multiplication on the DSP56001 (2048 and 256 bits)
# Needs: gcc-m68k-linux-gnu, a56 (apt install a56), python3, MAIL built
# (make) for its runtime, and BearSSL 0.6: by default the copy inside
# github.com/arduino-libraries/ArduinoBearSSL (src/bearssl), fetched here.
set -e
H=$(cd "$(dirname "$0")" && pwd)
M=$(cd "$H/../.." && pwd)
W=${WORK:-$H/work}
mkdir -p "$W"
B=${BEARSSL:-$W/ArduinoBearSSL/src/bearssl}
[ -d "$B" ] || git clone --depth 1 https://github.com/arduino-libraries/ArduinoBearSSL "$W/ArduinoBearSSL"
CC=m68k-linux-gnu-gcc
GCCINC=$($CC -print-file-name=include)
CF="-m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie
 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -nostdinc -isystem $GCCINC
 -I$H/inc -I$M/atari/include -I$M/atari -I$B -I$W -DMAIL_TOS
 -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0
 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0"
LF="-m68030 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$M/atari/link.ld
 -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments"
RT="$M/build/crt0.o $M/build/tos.o $M/build/libc.o $M/build/nf.o"

# BearSSL for the 68030
mkdir -p "$W/obj"
for f in "$B"/*.c; do
  n=$(basename "$f" .c)
  [ "$n" = sysrng ] && continue
  [ "$W/obj/$n.o" -nt "$f" ] || $CC $CF -w -c -o "$W/obj/$n.o" "$f"
done
rm -f "$W/libbearssl.a"
m68k-linux-gnu-ar rcs "$W/libbearssl.a" "$W"/obj/*.o
$CC $CF -Wall -c -o "$W/tlsbench.o" "$H/tlsbench.c"
$CC $LF -o "$W/tlsbench.elf" $RT "$W/tlsbench.o" "$W/libbearssl.a" -lgcc
PRGFLAGS=7 python3 "$M/tools/elf2tos.py" "$W/tlsbench.elf" "$H/TLSB030.PRG"

# the DSP program and its test vectors
(cd "$W" && rm -f a56.out && a56 "$H/mont.a56" >/dev/null && python3 "$H/gen.py")
$CC $CF -Wno-array-bounds -c -o "$W/tlsdsp.o" "$H/tlsdsp.c"
$CC $LF -o "$W/tlsdsp.elf" $RT "$W/tlsdsp.o" -lgcc
PRGFLAGS=7 python3 "$M/tools/elf2tos.py" "$W/tlsdsp.elf" "$H/TLSDSP.PRG"
