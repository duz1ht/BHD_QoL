# Dynamic FNT atlas dimensions

`[PatchGroups] AllowHighResolutionFonts=1` enables this feature (also the default
when the INI is absent). Set it to `0` and restart the game to disable it. This
implementation reads the font assets supplied to the game. It does not generate
glyphs, select vector families, or change text size, spacing or aspect ratio.

## Supported format

The audited FNT0 format stores 32 bytes of header, 224 glyph entries of 20 bytes,
then raw 32-bit color/alpha pages. The header specifies page count and reference
width, but does not declare the physical dimensions of a page. Each glyph entry
contains a page index and four normalized UV coordinates.

The loader detects page dimensions from the exact byte count:

```text
pageBytes = (fileBytes - 4512) / pageCount
pageBytes = side * side * 4
```

Supported sides are 256, 512, 1024, 2048 and 4096. A file can contain 1 through 16
pages (the native object's arrays hold 16). All pages in one file must use the
same side. Different files can use different sides simultaneously. The validator
rejects missing/extra bytes, zero reference width, invalid page counts and
non-finite, reversed or out-of-range glyph UVs/page indices. Rectangular pages,
compressed pixel formats, appended mipmaps and other FNT variants are not covered.

The logical reference width and normalized UVs are retained exactly. For example,
upscaling every original page by four with UVs and header unchanged preserves
layout while providing 1024x1024 physical pages. Enlarging the canvas and changing
packing without updating logical metrics is not equivalent. This feature cannot
infer the intended visual size of an incorrectly authored replacement.

## Native integration

The supported reference executable is the x86 `dfbhd.exe` used by the existing
BHD_QoL patches. Its SHA-256 in the descompilation inventory is
`8bd3a5099bf3f9bacc2cfa956f07069bfcb1fc8e81b32d93e1afa636bf10ef48`.
The implementation checks instruction signatures; it does not recompute this
whole-file hash at runtime.

| Native address | Role |
| --- | --- |
| `0x00564890` | Load file from disk/PFF. The hook records the returned buffer's exact length from `0x00F65684` for `.fnt` names. |
| `0x0056EF20` | Native game memory release. The hook forgets any associated file record before the pointer can be reused. |
| `0x005ACFF0` | Common FNT loader. The hook validates each known file on load. |
| `0x005AD0C2` | FNT texture-creation call site. HD page uploads use the original pixels and detected physical side. |

File-length tracking is bounded to 256 simultaneously pending file buffers and
cleared as those buffers are freed; font textures do not occupy these records
after their input file is released. The bookkeeping uses fixed storage and a
lock; the general free hook skips the lookup when there are no pending records.
If tracking fills, untracked font loads are rejected rather than guessing a
length. Opaque in-memory buffers that do not pass through the file loader retain
the native 256-page contract when tracking has not overflowed.

For a valid 256x256 file, the original loader receives the original buffer. For
HD files, a temporary logical staging file retains the unchanged metadata and
contains 256x256 CPU mirrors, one per original page. Its lifetime covers the
native load call. The GPU upload hook substitutes the appropriate original HD
page at each upload and requests 32-bit color/alpha through the native texture
manager. The context is thread-local and restored on return or C++ unwinding.

This split is necessary because the native loader advances input pages by a
fixed 262144 bytes and may copy those pages for its CPU drawing path. Passing an
HD buffer directly would misread page 2 and later, and would produce an incorrect
CPU mirror. The staging data makes those fixed operations correct; GPU textures
still receive the original HD pixels, not the reduced mirrors. Logical geometry
and normalized UVs remain valid in the original renderer's 256-unit coordinates.

No source file is rewritten, no external pixel buffer is retained by the DLL,
and native texture/material/CPU-buffer ownership is preserved. Managed texture
creation and native hardware limits remain in effect. A failed HD allocation is
released and retried using the logical mirror; this fallback has lower definition.
Invalid files or insufficient staging memory return load failure without passing
an incorrectly sized buffer to the original loader.

All hook instructions are verified before writing. Trampolines copy whole,
non-relative entry instructions. Write permissions for every hook are acquired
before changing any game instruction; failure restores protections and leaves
the font hooks unapplied. No per-frame or per-string validator is installed.

## Validation

Debug and Release builds each pass the project's 14 CTest cases. New coverage:

- Alternating 256/512/1024/2048/4096 inputs and multiple-page offsets.
- Metadata preservation, unchanged input buffers and alpha-correct CPU mirrors.
- Truncation, trailing bytes, unsupported dimensions and invalid glyph metadata.
- A synthetic native module exercising the actual x86 font/free trampolines and
  their calling conventions.
- Mixed-resolution uploads and their physical page addresses/format flags.
- Failed/null HD texture allocation and cleanup before mirror fallback.
- File length tracking, pointer reuse, release cleanup and tracking overflow.
- Signature rejection and failed write permission with no partial patch writes.
- Restoration of the upload context after a simulated allocation exception.

The standalone resource check also reads all 23 original FNTs extracted under the
separate decompilation reference directory. All detect as 256x256, and their
logical copies match the source byte-for-byte. The four hook signatures and two
native texture helper signatures were checked against the reference EXE.

```powershell
& '.\out\build\winmm-release\fnt_layout_test.exe' `
  'C:\Users\DUZ\Desktop\CodeProjects\DFClones\re_bhd\bhdgame_rea\assets\pff\localres'
```

These tests do not launch DFBHD. Actual gameplay, PFF/loose-file precedence,
resolution changes and Alt+Tab still need runtime validation. In particular,
compare a mixed set of real 256x256 and 1024x1024 replacements, including a font
with two or three pages, and confirm layout and all characters. Also compare the
optional CPU drawing path against GPU text, and check device-loss recovery. The
DLL has been built in the project output directory, not installed into the game.
