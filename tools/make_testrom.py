#!/usr/bin/env python3
"""Generate a minimal Sega Mega Drive test ROM.

The 68000 program sets up the VDP, enables the display, and paints the
whole screen red via the backdrop colour. Used to verify the emulator's
CPU + VDP + video pipeline end to end.
"""
import struct, sys

ROM = bytearray(b'\x00' * 0x400)

ENTRY = 0x000200

# ---- vector table (0x000..0x0FF) ----
struct.pack_into('>I', ROM, 0x00, 0x00FF0000)   # initial SSP (top of 68k RAM)
struct.pack_into('>I', ROM, 0x04, ENTRY)         # initial PC
for off in range(0x08, 0x100, 4):                # point all vectors at entry
    struct.pack_into('>I', ROM, off, ENTRY)

# ---- cartridge header (0x100..0x1FF) ----
def put(off, s, length):
    b = s.encode('ascii')[:length].ljust(length, b' ')
    ROM[off:off+length] = b

put(0x100, "SEGA GENESIS    ", 16)
put(0x110, "(C)GFM  2026.JUL", 16)
put(0x120, "GENS FOR MAC TEST ROM (RED SCREEN)              ", 48)
put(0x150, "GENS FOR MAC TEST ROM (RED SCREEN)              ", 48)
put(0x180, "GM 00000000-00", 14)
struct.pack_into('>H', ROM, 0x18E, 0x0000)       # checksum (ignored by emu)
put(0x190, "J               ", 16)              # I/O support
struct.pack_into('>I', ROM, 0x1A0, 0x00000000)   # ROM start
struct.pack_into('>I', ROM, 0x1A4, 0x000003FF)   # ROM end
struct.pack_into('>I', ROM, 0x1A8, 0x00FF0000)   # RAM start
struct.pack_into('>I', ROM, 0x1AC, 0x00FFFFFF)   # RAM end
put(0x1F0, "JUE             ", 16)              # region

# ---- program (from 0x200) ----
code = bytearray()
def movew_reg(regval):            # move.w #$80xx,($C00004).L  (VDP reg write)
    code.extend(b'\x33\xFC')
    code.extend(struct.pack('>H', regval))
    code.extend(b'\x00\xC0\x00\x04')

movew_reg(0x8004)   # reg0  = 0x04
movew_reg(0x8144)   # reg1  = 0x44  (display ON, mode 5)
movew_reg(0x8230)   # reg2  = plane A nametable @ 0xC000
movew_reg(0x8407)   # reg4  = plane B nametable
movew_reg(0x8700)   # reg7  = backdrop uses CRAM entry 0
movew_reg(0x8F02)   # reg15 = auto-increment 2

# set CRAM write address 0:  move.l #$C0000000,($C00004).L
code.extend(b'\x23\xFC\xC0\x00\x00\x00\x00\xC0\x00\x04')
# write red to CRAM[0]:      move.w #$000E,($C00000).L
code.extend(b'\x33\xFC\x00\x0E\x00\xC0\x00\x00')
# forever:                    bra.s *
code.extend(b'\x60\xFE')

ROM[ENTRY:ENTRY+len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "test_red.bin"
with open(out, 'wb') as f:
    f.write(ROM)
print(f"wrote {out} ({len(ROM)} bytes), code={len(code)} bytes")
