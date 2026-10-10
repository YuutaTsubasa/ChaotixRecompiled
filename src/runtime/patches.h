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

// What scene is on screen, as the game itself records it: the game mode the
// dispatcher above reads, plus the place and level the level engine reads.
// Measured modes: 0x00 boot and the SEGA logo, 0x08 title, 0x18 a level or the
// lobby, 0x38 an attract demo. The mode alone is not enough -- the lobby runs
// in the level's mode -- but no two scenes share all three, and pausing
// changes none of them, which is how a paused level is told apart from a scene
// that has replaced it.
constexpr uint32_t kGameMode = 0xDFDE;
constexpr uint32_t kSceneZone = 0xDFF2;
constexpr uint32_t kSceneLevel = 0xDFF4;

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
// int16 left/right in pixels, int32 top/bottom in 24.8 fixed point
// (native 0, 320, 0, 223 << 8).
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
// The shift does not depend on E: it is latched (at a full redraw) from
// whether widescreen is on at all, so the margins the window asks for can
// change while a level runs without the ring having to be re-established.
constexpr int plane_shift_for(int extra) { return extra > 0 ? 96 : 0; }

// The patches assume the level engine's plane geometry: 64 columns x 32 rows
// (reg 16 = 0x01), the 512 x 256 px ring described above. Scenes that set up
// a different plane (menus and transitions use 64x64) are left alone.
constexpr uint8_t kLevelPlaneSize = 0x01;

// The special stage (game mode 0x20, handler $885850) is not a level-engine
// scene: the master SH-2 draws its tube as polygons from code the stage copies
// into SDRAM. The polygon rasterizer ($06004858) takes its bounding box and
// clips against a rectangle stored as data at SDRAM 0x06003844: int16 left,
// right, top, bottom, inclusive. The special stage sets it to 16, 303, 4, 219
// (a 288 px picture inside the 320 px screen); other scenes use 0, 319, 0, 223.
// Widening it makes the tube's own geometry fill the margins, but only to the
// right: the rasterizer turns 8.8 x into pixels with SHLR8, a logical shift,
// so a span starting left of x = 0 gets a huge start and a garbage length.
//
// So the whole tube is drawn C px further right instead. Every projection in
// that code adds the screen centre as two ADD #80 (kSpecialCentreAdds); those
// immediates become 80 + C/2, the rectangle becomes [0, 319 + 2C], and the
// host moves every polygon pixel -- the rasterizer's CPU writes and its auto
// fills, which are the frame buffer's only non-overwrite writes here -- back
// by C. Pixels that land outside the native 320 columns go to the host-side
// shadow (Machine::fb_margin) that the compositor shows in the margins; the
// stage keeps data in the line padding, so they must not go there. Sprites
// (HUD, the player) are drawn with the overwrite image at their native
// positions and are only redirected, not moved. The one other non-overwrite
// writer is the per-frame clear in the boot code ($0600032C): it fills each
// of lines 4-219 from x = 16 for 145 words with 0. It is recognised by exactly
// that fill and clears the whole widened line instead, unmoved; moved, it
// would leave the right of the picture uncleared.
//
// The code patch bumps sdram_code_epoch, so recompiled code that covers those
// bytes is revalidated and the interpreter runs the patched routine.
constexpr uint32_t kSpecialRectSdram = 0x3844;
constexpr uint16_t kModeSpecialStage = 0x20;
constexpr int16_t kSpecialRect[4] = {16, 303, 4, 219};
// The clear's fill: start word within the line, length register, data.
constexpr uint16_t kSpecialClearStart = 0x08;
constexpr uint16_t kSpecialClearLen = 0x90;
// The stage's code is recognised by its vertex projection: SHLR8 R4;
// ADD #80,R4; ADD #80,R4; STS MACL,R0; MOV.W R4,@R7; SHLR8 R0; ADD #112,R0;
// MOV.W R0,@(2,R7).
constexpr uint32_t kSpecialProjectSdram = 0x4442;
constexpr uint16_t kSpecialProjectCode[8] = {0x4419, 0x7450, 0x7450, 0x001A,
                                             0x2741, 0x4019, 0x7070, 0x8171};
// The polygon projections' pairs of ADD #80,Rn (SDRAM offset of the first,
// Rn): every one whose output goes to the polygon dispatcher ($060045E8) --
// the tube's two vertex projections ($06004442, $06004C5E), the near-plane
// clippers ($060044F4, $0600456C) and a rotated one ($0600414A). The add at
// $06004BE0 places sprite objects (the player, the spheres), which are not
// moved back, so it stays at 160.
struct CentreAdd { uint32_t sdram; uint8_t reg; };
constexpr CentreAdd kSpecialCentreAdds[] = {
    {0x414A, 0}, {0x4444, 4}, {0x4558, 0}, {0x45D0, 0}, {0x4C60, 0},
};
// The special stage is drawing its tube: mode 0x20 with the stage's own
// rectangle, or the widened one (its intro and results use other rectangles).
bool special_stage_active(const Machine& m);

// A frame counts as a level scene when the level engine's per-frame plane
// update ran in the last 8 frames (it skips a few while a level loads), or,
// once it has stopped, until another scene redraws the planes: a pause stops
// the engine with the level still on screen, and the ring keeps the level's
// tiles until something else claims it. Other
// scenes (title, menus) stay 4:3 with black side bars; the special stage has
// its own support (special_stage_active, above). The
// scripted Eggman encounter in INTRODUCTION level 0 also stays native while
// C21C bit2 is set: its actors wait just outside the original viewport.
bool wide_scene_active(const Machine& m);

// WORLD ENTRANCE (the lobby, zone 7) stays 4:3 (issue #13): its rooms are
// narrower than a widened view, so the margins showed the neighbouring part of
// the layout -- the outdoor entrance beside the indoor rooms, drawn with the
// indoor tiles and palette -- or blocks the engine never streams; the
// palette changes where the camera reaches a point, which the margins pass
// first; and the catapult moves the camera faster than the shifted ring's
// 96 - E px of slack (natively 192) can keep up with, so chunks went missing
// even in the 4:3 centre. There the ring is not shifted, the camera clamp is
// left alone and the margins are black, exactly as a 4:3 window shows it.
constexpr uint16_t kWorldEntrance = 7;
bool wide_zone(const Machine& m);

// Which scene is on screen now, packed from the three words above. Equal keys
// mean the same scene; Machine::level_scene holds the key the level engine
// last drew under. Tests that stand in for the engine set it from this.
uint64_t scene_key(const Machine& m);

// Level camera position (plane A struct), for tooling and tests.
int camera_x(const Machine& m);
int camera_y(const Machine& m);
// The range the level engine clamps camera X to (plane A struct +$A, +$8),
// before the widescreen patch pulls it in: the room the player is in.
struct CameraRange { int left = 0, right = 0; };
CameraRange camera_range(const Machine& m);

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
