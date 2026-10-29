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
namespace sh2 {
struct State;
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
    // A 32X object's pieces (68K 0x881270-0x88128A): each piece of the
    // object's mapping is pushed onto the 32X command list (a5, growing down)
    // as op 0x01, `01 BANK FRAME FLAGS X.w Y.w SX.w SY.w`; this is the rts
    // after the last one (issue #36, see kBackdropMaps).
    kPiecesDone = 0x88126E,
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
    // Every sound request ($8F6E76, sound in d0) once it holds the Z80 bus,
    // before the id is queued for the Z80 driver at $A01C0A. Ids below $2A
    // are music; $8F6E72 is the same with the id kept at $FFFCFC as the
    // current music, which the game requests again after a jingle
    // ($88E962). Measured with the driver: $F1 stops the music (sound
    // effects carry on), $F0 lowers it. See audio/music_mods.h.
    kSoundRequest = 0x8F6E8A,
};
constexpr uint8_t kFirstSoundEffect = 0x2A;
constexpr uint8_t kSoundLowerMusic = 0xF0;
constexpr uint8_t kSoundStopMusic = 0xF1;
// Z80 RAM: non-zero while the game is paused and the driver holds the music.
constexpr uint32_t kZ80Paused = 0x1C10;

// MARINA MADNESS draws its clouds and sea (issue #36) as four screen-fixed
// 32X objects at X 160, each a two-piece mapping (in the ROM's 68K bank at
// 0x9D944C): the same frame of bank 34 at X - h and X + h, h = 96 for the
// clouds and 80 for the three bands of sea below them. The two copies meet at
// X, so the picture repeats every 2h; but they cover only X - 2h .. X + 2h,
// -32..352 and 0..320, and the margins showed the sky behind them. In a
// widened level the hook at kPiecesDone pushes more copies of the same
// command, 2h apart, until the clip rectangle is covered. The mappings are
// told by their address and checked by their bytes: two pieces, the same
// line and frame, X - h and X + h, no flags.
inline constexpr uint32_t kBackdropMaps[] = {0x9D944C, 0x9D9454, 0x9D945C, 0x9D9464};
// The game stops adding objects to the command list once a5 is below this
// (68K 0x8811CA); the extra copies keep above it.
constexpr uint32_t kCommandListLimit = 0xD0A0;

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
    kRingCullLo, kRingCullHi, kRingCullDone, kRingCullExit, kPiecesDone, kModeDispatch,
    kFullRedrawA, kRowsA_X, kFullRedrawB, kRowsB_X, kPlaneUpdateA, kRowUpA_X, kRowDownA_X,
    kColLeftA_X, kColRightA_X, kPlaneUpdateB, kColLeftB_X, kColRightB_X, kRowUpB_X, kRowDownB_X,
    kCamClampMax, kCamClampMin, kCamClampBottom,
    // Ring tether options (runtime/tether.h): tether::kCharsUpdate, kForce,
    // kClampBefore, kClampAfter, kPartnerSide and the kBothObjects compares.
    0x889C10, 0x89138E, 0x891392, 0x8913A2, 0x891494,
    0x896566, 0x898C48, 0x898E6C, 0x89A2C8, 0x89FF7A, 0x8A3266,
    kFillSplitX, kSoundRequest,
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

// Record-only SH-2 hooks (the 32X sprite recorder, runtime/sprite_record.h;
// see docs/SPRITES_32X.md). They run before the instruction at these master
// SH-2 addresses -- the entries of the SDRAM routines copied from ROM 0x77800
// -- in the interpreter and in the generated code alike, read registers and
// memory, and write nothing the machine sees, so lockstep still holds. A
// routine run from another address (a ROM or cache-through copy) is not
// hooked.
enum : uint32_t {
    kSh2RecolourCopy = 0x06000810,  // r1 graphic, r2 scratch buffer, r3 colour offset
    kSh2Blit = 0x06001380,          // r1 X, r2 Y, r3 flags, r4 graphic
    kSh2BlitScaled = 0x06001F18,    // also r5 SX, r6 SY (8.8)
    kSh2BlitWarped = 0x060023EC,    // r1-r4, and r5 -> 16 bytes of stretch parameters
};
// Sorted, like kM68kHooks.
inline constexpr uint32_t kSh2Hooks[] = {kSh2RecolourCopy, kSh2Blit, kSh2BlitScaled, kSh2BlitWarped};
constexpr uint32_t kSh2HookCount = uint32_t(sizeof kSh2Hooks / sizeof kSh2Hooks[0]);

constexpr bool sh2_hooks_sorted() {
    for (size_t i = 1; i < kSh2HookCount; ++i)
        if (kSh2Hooks[i - 1] >= kSh2Hooks[i]) return false;
    return true;
}
static_assert(sh2_hooks_sorted(), "kSh2Hooks must be sorted");

constexpr bool is_sh2_hook(uint32_t pc) {
    for (uint32_t h : kSh2Hooks)
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

// The special stage (handler $885850) is not a level-engine scene. The game
// mode does not tell it apart: the stage select starts it as mode 0x20, but
// in play a giant ring jumps to the handler from the level ($891052) and the
// mode stays the level's 0x18 (issue #21: it stayed 4:3 that way). It is told
// apart by its own code and rectangle instead (special_stage_active). The
// master SH-2 draws its tube as polygons from code the stage copies
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
// The recompiled code reads those immediates as it runs (see
// is_patched_sh2_immediate below), so it stays valid when they change.
constexpr uint32_t kSpecialRectSdram = 0x3844;
constexpr int16_t kSpecialRect[4] = {16, 303, 4, 219};
// The stage frames its 4:3 picture with black Mega Drive sprites of this
// pattern (issue #21): 2x4 cells at X 0 and 304 down the sides, 4x1 cells
// above and below. Widened, the side columns would cut through the picture,
// so they are not drawn (Vdp::hidden_frame_tile), and the margins are black
// on the rows above and below the stage's rectangle, where the bars are.
constexpr uint16_t kSpecialFrameTile = 0x038;
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
// Two more scenes are drawn by that polygon code, with the screen's own
// rectangle (kFullRect) rather than the stage's (issues #38 and #39), and are
// widened with the same machinery: the rectangle becomes [0, 319 + 2C], the
// polygons are drawn C px further right and their pixels are moved back by C.
// Only where the polygons get their X differs.
//
// The results screen after a cleared stage ("GOT THE CHAOS RING"). The master
// SH-2, idle between commands, calls the routine at SDRAM 0x06007640 once a
// vblank (0x060011C4); the special stage's SDRAM image (from ROM, at
// kSdramImageRom + offset) has one there that draws the fixed list of polygons
// at 0x06007688: a long `00 COLOUR FLAGS N`, then N vertices (int16 x, y), up
// to a zero long. It is a 2D design laid out for 320 columns: every
// background polygon touches x = 0 or x = 319. Widened, the list's vertices
// get x + C, and those on the screen's edges go to the widened edges (0 and
// 319 + 2C), so the background reaches the margins; nothing else moves. The
// results screen only ever follows the stage, which is how it is told from
// the stage's intro, the one other time this rectangle and code are loaded.
//
// The bonus stage (issue #38) draws its items with the blitters and a few
// polygons, and clears each line it draws on with one fill: word 0 of the
// line, length register kBonusClearLen, data 0 (kept whole, as the stage's
// clear is). Its tunnel is a Mega Drive plane picture of concentric rings
// that ends at the 4:3 edges. Each ring is a run of palette indices 1..12,
// one index per w px, with w one more for every ring further out (1 px from
// r = 80, then 2, 3, 4 at the screen's corners); the palette cycles them. The
// margins carry the rings on (Vdp::radial_lut), measured from the picture
// itself, so they cycle with it. A picture that is not rings centred on the
// screen is not widened.
//
// The final boss (WORLD ENTRANCE level 2, issue #61) is a level, and its
// arena, floor and pillars, is projected by the stage's code onto the
// screen's rectangle; widened like the stage, the arena reaches the margins
// (it was cut at the 4:3 edges, which showed its floor as stretched bars).
// Its line clear is the bonus stage's, and its fight begins with a white
// flash: one polygon with the screen's corners, clipped to exactly the
// native line, which is drawn over the whole widened line (is_widened_clear).
enum class PolyScene : uint8_t { None, Stage, Results, Bonus, Boss };
constexpr int16_t kFullRect[4] = {0, 319, 0, 223};
constexpr uint32_t kSdramImageRom = 0x77800;
constexpr uint32_t kResultsCodeSdram = 0x7640;
constexpr uint32_t kResultsTableSdram = 0x7688;
constexpr uint32_t kResultsTableEnd = 0x7818;
constexpr uint16_t kBonusClearLen = 160;
// The bonus stage's polygons come from the 68K as offsets from the screen
// centre, which the boot code's handler ($06000CD0) puts at X 160 with
// MOV #-96,R10 (then EXTU.B, SHLL16) followed by MOV #112,R11. Widened, that
// immediate becomes 160 + C, and the polygons' pixels are moved back by C.
constexpr uint32_t kBonusCentreSdram = 0x0CE6;
constexpr uint16_t kBonusCentreNext = 0xEB70;
constexpr int kBonusCentre = 160;
// The same handler places the items it draws from that centre too, then
// blits them scaled (kSh2BlitScaled) from 0x06000DF2, returning here. They are
// drawn with the overwrite image, which is not moved back, so the hook there
// takes the shift off their X instead (sh2_hook). The HUD and the items the
// 68K places itself are blitted from elsewhere, unchanged.
constexpr uint32_t kBonusItemReturn = 0x06000DF6;
// Ring colours by radius, in quarter pixels, far enough for the corners of
// the widest margins (r = 266 at kMaxWideExtra).
constexpr int kRadialLen = 4 * 288;

// The fill the widened scene clears a whole line with (the special stage's
// or the bonus stage's), which then clears the margins too, unmoved.
bool is_widened_clear(const Machine& m, uint32_t word_addr, uint32_t words, uint16_t data);

// The level HUD (issue #28). The SH-2 draws SCORE, TIME and RINGS from the
// 68K's HUD command ($06000C9C), at base positions that are immediates in the
// boot code: MOV #32,R1 then MOV #16/#32/#48,R2. With Machine::wide_hud_edges
// the X immediates become 32 - M in a widened level, M being the part of the
// left margin that shows the level (margin_cut blacks out the rest near a
// level's start), so the HUD keeps to the left edge of the picture. The level
// clip already reaches into the margin, and its pixels there go to the
// shadow like any sprite's. Like the special stage's adds, the recompiled code
// reads these immediates as it runs.
struct HudBase { uint32_t sdram; uint16_t next; };
constexpr HudBase kHudBaseX[] = {
    {0x0CA2, 0xE210}, {0x0CAC, 0xE220}, {0x0CB4, 0xE230}, {0x0CC0, 0xE210}, {0x0CCA, 0xAFF4},
};
constexpr int kHudX = 32;

// The SH-2 immediates above change at run time: the special stage's ADD #80
// pairs, the HUD's MOV #32 and the bonus stage's centre. Code covering them
// used to fail validation and run in the interpreter (the patches bumped sdram_code_epoch to make sure of
// it), and those routines are the special stage's busiest (issue #31: it
// lagged on a slow PC in widescreen). The recompiler emits
// these instructions with the immediate byte read from SDRAM as they run, and
// leaves that byte, and only that byte, out of the validated code.
// `sdram` is the SDRAM offset of the instruction word.
constexpr bool is_patched_sh2_immediate(uint32_t sdram) {
    for (const CentreAdd& c : kSpecialCentreAdds)
        if (sdram == c.sdram || sdram == c.sdram + 2) return true;
    for (const HudBase& h : kHudBaseX)
        if (sdram == h.sdram) return true;
    return sdram == kBonusCentreSdram;
}

// The special stage is drawing its tube: its code is loaded and the stage's
// own rectangle, or the widened one, is set (its intro and results use other
// rectangles, and the levels the rectangle of other scenes).
bool special_stage_active(const Machine& m);
// The results screen and the bonus stage (see PolyScene).
bool results_screen_active(const Machine& m);
// Fills lut (kRadialLen entries) with the tunnel's ring colours.
bool bonus_tunnel(const Machine& m, uint8_t* lut);

// A frame counts as a level scene when the level engine's per-frame plane
// update ran in the last 8 frames (it skips a few while a level loads), or,
// once it has stopped, until another scene redraws the planes: a pause stops
// the engine with the level still on screen, and the ring keeps the level's
// tiles until something else claims it. Other
// scenes (menus) stay 4:3 with black side bars, the title shows its horizon
// (title_margins); the special stage has
// its own support (special_stage_active, above). The
// scripted Eggman encounter in INTRODUCTION level 0 also stays native while
// C21C bit2 is set: its actors wait just outside the original viewport.
bool wide_scene_active(const Machine& m);

// The title screen (issue #77): game mode $08, from the intro (the city, then
// the road, Metal Sonic) to the logo with PUSH START. Its horizon, the road
// and the bands of floor, is the Mega Drive's planes, and the game fills all
// 64 columns of them, wider than the screen: in the margins the floor goes on
// as the game drew it (measured: the left margin is the exact mirror of the
// right on every title frame of the 36000-frame attract run, and black while
// the planes are empty). The city, Metal Sonic, the logo, the characters and
// the girders are one 32X picture made for the 4:3 screen, which the margins
// do not extend; beside it they show the sky (the black backdrop) and the
// floor, which is what lies there. Nothing is patched, so the game runs as
// ever; only the side bars go. The SEGA logo and the boot screens (other
// modes) are all 32X and keep their bars.
bool title_margins(const Machine& m);

// WORLD ENTRANCE, zone 7: the lobby (levels 0 and 1), the final boss (2) and
// the ending's scenes (3-6). It was kept 4:3 (issues #13, #56); it is
// widened since issue #61, all but the ending's scenes, which stay 4:3. What
// went wrong, and how each is handled:
// - The camera is left alone there: the widescreen camera clamp moved it,
//   and the lobby's events (rooms, palettes, the catapult) and the boss
//   follow the camera, so the run itself changed (the catapult landed four
//   frames early). Unclamped, the picture's centre is the 4:3 one and the
//   margins show what lies beside it.
// - The lobby's plane B is not a scrolling layer but a set of 512 px
//   pictures side by side, one per part of the lobby, that the game selects
//   by moving plane B's X ($889144) and scrolls by line within the 512 px
//   ring (the sky beside the catapult). Shifted like plane A, the ring held
//   96 px of the next picture instead of its own wrap-around: the blocks
//   missing from the sky during the catapult, inside the 4:3 view too. Plane
//   B keeps the native ring in the lobby; plane A, which scrolls with the
//   camera, is shifted as in a level.
// - Rooms narrower than the view (the stage select room, the Combi
//   Catcher) are black outside the range the camera is held to, and the
//   palette areas are drawn in their own colours (lobby_margins, below).
// - The final boss's arena is drawn by the special stage's polygon code with
//   the screen's rectangle; it is widened as the stage is (PolyScene::Boss).
// The scripted Eggman encounter in INTRODUCTION level 0 stays 4:3 on purpose
// (the user's decision: Eggman walks out to the left, and widened that looks
// wrong); nothing lifts that mask any more. Machine::wide_everywhere (the
// former WIDE LOBBY option) has no effect.
constexpr uint16_t kWorldEntrance = 7;
constexpr uint16_t kLastLobbyLevel = 1;
constexpr uint16_t kFinalBossLevel = 2;
bool wide_zone(const Machine& m);
bool in_lobby(const Machine& m);
// The final boss's arena is on screen, drawn by the special stage's code
// (its projection loaded) with the screen's rectangle, or our widened one.
bool final_boss_active(const Machine& m);

// The lobby's palette areas (issue #61). Every frame ($8891C6) the lobby
// picks palette lines 0 and 1 by where plane A is: area k while its X is at
// least k of the bounds at $88922A (0x180, 0x4C0, 0x900, 0x1010, then -1),
// 32 colours from the table at $9BA764 (in the zone's ROM bank): a word at
// +10 * (time of day, $FFDFF6 & 6, halved) + 2k, the offset of the colours
// from itself. The 4:3 view shows world [X, X + 320), so area k's colours
// are only ever seen on world [bound(k), bound(k + 1) + 320); the margins
// reach past that by E, into the next area's scenery, which then showed in
// the wrong colours until the view itself got there (a blue-grey pillar
// before the COMBI CATCHER). Those columns are drawn with the neighbouring
// area's colours instead (Vdp::alt_cram). The game also cycles a few of
// these colours: what the game shows now in place of the table's colours is
// kept. When the game's colours match no area (a fade), they are black.
constexpr uint32_t kLobbyAreaBounds = 0x922A;     // ROM offset ($88922A)
constexpr uint32_t kLobbyPalettes = 0x9BA764;     // 68K, banked
constexpr int kLobbyAreas = 5;
constexpr int16_t kLobbyBounds[kLobbyAreas - 1] = {0x180, 0x4C0, 0x900, 0x1010};
// Colours of an area the game may be cycling when it is matched.
constexpr int kLobbyCycled = 8;
// Fills Machine::lobby_* for this frame's picture, margins of e px.
void lobby_margins(Machine& m, int e);

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

// The master SH-2's hooks at kSh2Hooks, while a scene needs them (the widened
// bonus stage: kBonusItemReturn). Like the 68K hooks, they run before the same
// instruction in the interpreter and the recompiled code, and depend only on
// machine state, so lockstep holds.
void sh2_hook(Machine& m, sh2::State& c, uint32_t pc);

// Installs the hooks on the machine's 68K (called from Machine::reset).
void install(Machine& m);

} // namespace chaotix::patches
