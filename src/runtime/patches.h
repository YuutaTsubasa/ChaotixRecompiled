// Runtime game patches (widescreen). Patches never modify ROM bytes: they are
// host hooks that run before specific 68K instructions. The reference
// interpreter and the generated code call the same hook at the same points
// (m68k::State::hook), so patched execution remains lockstep-comparable.
//
// All addresses below were found by ROM analysis of the verified image
// (sha1 0c2fff7b...); see ARCHITECTURE.md §8 for the evidence.
#pragma once
#include <cstddef>
#include <cstdint>

namespace chaotix {
class Machine;
}

namespace chaotix::patches {

// Level-engine plane streaming (68K, ROM 0x9786-0x99B6). Each plane keeps a
// 512 px wide ring of nametable columns. Natively the ring covers world
// [cam_x, cam_x + 512): new 16 px columns are drawn at cam_x when scrolling
// left and at cam_x + 512 when scrolling right, and rows span cam_x..+512.
// The hooks subtract a shift W from the X coordinate handed to the fill
// routines, so the ring covers [cam_x - W, cam_x - W + 512) and both
// widescreen margins show valid tiles.
enum : uint32_t {
    // Ring sprites (68K 0x1044): screen X in sprite coordinates is kept only
    // when 0x70 <= x < 0x1D0 (screen -16..335). The hooks shift d2 by +E
    // before the low compare and by -2E before the high compare, then restore
    // it, which widens the window to [-16 - E, 336 + E) without changing the
    // sprite position written afterwards.
    kRingCullLo = 0x88104C,      // cmpi.w #$70,d2

    kRingCullHi = 0x881052,      // cmpi.w #$1D0,d2
    kRingCullDone = 0x881058,    // both passed
    kRingCullExit = 0x881074,    // rts (rejected paths land here)
    // The game mode dispatcher (ROM 0x3262): reads the mode from $FFDFDE and
    // jumps to its handler, which then runs its own loop. This is the only
    // point the game passes through between scenes, so it is where a stage the
    // host asked for (runtime/stage_select.h) can be started.
    kModeDispatch = 0x883262,
    kFullRedrawA = 0x8897AC,     // redraw 16 rows (unmasked X); latches W
    kFullRedrawB = 0x889810,     // redraw 16 rows (X masked by plane width); latches W
    kRowsA_X = 0x8897BE,         // after d0 = plane.x (full redraw A)
    kRowsB_X = 0x889822,         // after d0 = plane.x (full redraw B, masked)
    kPlaneUpdateA = 0x88984E,    // per-frame plane scroll update (level scenes only)
    kRowUpA_X = 0x889868,        // after d0 = plane.x (new top row)
    kRowDownA_X = 0x889878,      // after d0 = plane.x (new bottom row)
    kColLeftA_X = 0x8898A2,      // after d0 = plane.x (new left column)
    kColRightA_X = 0x8898B6,     // after d0 = plane.prev_x + 512 (new right column)
    kPlaneUpdateB = 0x8898C4,    // per-frame plane scroll update, masked variant
    kColLeftB_X = 0x8898E2,      // masked variants of the above
    kColRightB_X = 0x8898F4,
    kRowUpB_X = 0x88991A,
    kRowDownB_X = 0x889926,
    // The fill routines take X in d0, but the routine that works out where a
    // row wraps inside the 64-column ring (ROM 0x4F22) re-reads plane.x from
    // the struct. With a shifted X the two disagree and 8 columns of the ring
    // are never filled (missing tiles at the screen edge), so the hook applies
    // the same shift there.
    kFillSplitX = 0x8F4F26,      // after d2 = plane.x (re-read)
    // Camera clamp (ROM 0x9A76, every zone): camera X ($FFDFE8) is clamped to
    // [plane.$A, plane.$8] of the plane A struct at $FFC1DE. The hooks pull
    // both bounds in by E so the margins never show past the level edges.
    kCamClampMax = 0x889A94,     // after d1 = right bound
    kCamClampMin = 0x889A9C,     // after d1 = left bound
    // Camera Y ($FFDFEA) is clamped to [plane.$E, plane.$C]; only the bottom
    // bound needs pulling in, because the extra rows are below the screen.
    kCamClampBottom = 0x889AC0,  // after d1 = bottom bound
};

// Plane A struct in 68K work RAM: +0 camera X, +8 right bound, +A left bound,
// +10 camera Y, +C bottom bound, +E top bound.
constexpr uint32_t kPlaneAStruct = 0xC1DE;

// Sorted: the interpreter binary-searches this list.
inline constexpr uint32_t kM68kHooks[] = {
    kRingCullLo, kRingCullHi, kRingCullDone, kRingCullExit, kModeDispatch,
    kFullRedrawA, kRowsA_X, kFullRedrawB, kRowsB_X, kPlaneUpdateA, kRowUpA_X, kRowDownA_X,
    kColLeftA_X, kColRightA_X, kPlaneUpdateB, kColLeftB_X, kColRightB_X, kRowUpB_X, kRowDownB_X,
    kCamClampMax, kCamClampMin, kCamClampBottom, kFillSplitX,
};

constexpr bool hooks_sorted() {
    for (size_t i = 1; i < sizeof kM68kHooks / sizeof kM68kHooks[0]; ++i)
        if (kM68kHooks[i - 1] >= kM68kHooks[i]) return false;
    return true;
}
static_assert(hooks_sorted(), "kM68kHooks must be sorted");

constexpr bool is_m68k_hook(uint32_t pc) {
    for (uint32_t h : kM68kHooks)
        if (h == pc) return true;
    return false;
}

// 32X sprite/polygon renderers (master SH-2, SDRAM image copied from ROM
// 0x77800) clip against a rectangle stored as data at SDRAM 0x06003834:
// int16 left, int16 right, int32 top, int32 bottom (native 0, 320, 0, 223).
// The patch widens it to [-C, 320 + C) with C = E rounded up to 8 (the
// blitters store 16-bit words at bound-aligned addresses; an odd bound makes
// the SH-2 take an address error). Frame buffer lines are 512 bytes
// apart with 320 shown, so out-of-screen pixels fall into the line padding
// (x < 0 at the end of the previous line). Some zones keep data in that
// padding, so those writes are redirected to a host-side shadow
// (Machine::fb_margin) that the compositor reads for the margins.
constexpr uint32_t kClipRectSdram = 0x3834;
// Level frame buffer layout (line table: line y at byte 0x200 + y * 0x200).
constexpr uint32_t kFbLineBase = 0x200;
constexpr uint32_t kFbLineStride = 0x200;
constexpr uint32_t kClipRectRom = 0x77800 + kClipRectSdram;

// Widest margin (px per side) the patches support: leaves the plane ring
// (below) 16 px of slack on each side, one 16 px column step.
constexpr int kMaxWideExtra = 80;
// Extra rows below the screen. The plane ring is 256 px tall against 224
// visible lines and holds [camera_y, camera_y + 256), so no patch is needed;
// the last 16 px block is the one being refreshed as rows scroll past, which
// leaves 16 usable rows (measured: 0 stale cells at 16, ~10% at 32).
// Rows *above* the camera are not possible: the engine refreshes rows in
// 16 px steps and overwrites the row that just left the top of the screen,
// so a shift large enough to protect a top margin leaves nothing below.
constexpr int kMaxWideExtraBottom = 16;
// Plane ring shift: the 512 px ring then covers [cam - 96, cam + 416), which
// leaves 96 - E px of slack on both sides (slack hides new 16 px columns
// arriving a step late when the camera moves fast). W stays a multiple of 16
// so the "camera crossed a block" test and the column drawn stay in step.
constexpr int plane_shift_for(int extra) { return extra > 0 ? 96 : 0; }

// The patches assume the level engine's plane geometry: 64 columns x 32 rows
// (reg 16 = 0x01), the 512 x 256 px ring described above. Scenes that set up
// a different plane (menus and transitions use 64x64) are left alone.
constexpr uint8_t kLevelPlaneSize = 0x01;

// A frame counts as a level scene when the level engine's per-frame plane
// update ran in the last 8 frames (it skips a few while a level loads). Other
// scenes (title, menus, special stages) stay 4:3 with black side bars.
bool wide_scene_active(const Machine& m);

// Level camera position (plane A struct), for tooling and tests.
int camera_x(const Machine& m);
int camera_y(const Machine& m);

// Margin columns on the left that fall left of the level's origin, where no
// layout exists. Non-zero only where a room is narrower than the widened view
// (the camera clamp keeps the margins inside the level otherwise).
struct MarginCut { int left = 0, right = 0; };
MarginCut margin_cut(const Machine& m, int extra);

// Host-side per-frame step (start of Machine::run_frame): applies or removes
// the 32X clip override. Runs identically in every machine, so lockstep
// comparison still holds.
void begin_frame(Machine& m);

// Installs the hooks on the machine's 68K (called from Machine::reset).
void install(Machine& m);

} // namespace chaotix::patches
