#include "runtime/patches.h"
#include "runtime/system.h"
#include "game/recomp_dispatch.h"
#include "test_framework.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

using namespace chaotix;

namespace {
constexpr uint8_t native_clip[] = {0, 0, 1, 0x40, 0, 0, 0, 0, 0, 0, 0xDF, 0};
uint32_t read32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
}
std::unique_ptr<Machine> level_machine() {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    std::copy(std::begin(native_clip), std::end(native_clip), m->rom.data.begin() + patches::kClipRectRom);
    m->reset();
    std::copy(std::begin(native_clip), std::end(native_clip), m->sdram + patches::kClipRectSdram);
    m->m68k.stopped = true;
    m->vdp.reg[12] = 1;
    m->vdp.reg[16] = patches::kLevelPlaneSize;
    m->level_seen_frame = m->frame_count;
    m->level_scene = patches::scene_key(*m);
    m->plane_shift = patches::plane_shift_for(53);
    m->wide_extra = 53;
    m->wide_extra_bottom = 16;
    return m;
}
// These tests drive begin_frame directly, so the level engine never runs.
// Moving the level to another place stands in for it running there, which is
// what records the scene whose tiles the plane ring now holds.
void stand_in_for_the_engine_in(Machine& m, uint8_t zone, uint8_t level) {
    m.wram[0xDFF3] = zone;
    m.wram[0xDFF5] = level;
    m.level_seen_frame = m.frame_count;
    m.level_scene = patches::scene_key(m);
}
bool shadow_is_clear(const Machine& m) {
    for (const auto& bank : m.fb_margin)
        for (uint8_t b : bank) if (b != 0) return false;
    return true;
}
}

TEST(widescreen_render, new_machine_has_cleared_margins) {
    // Poison allocator storage so this does not depend on fresh OS pages.
    void* storage = ::operator new(sizeof(Machine));
    std::memset(storage, 0xA5, sizeof(Machine));
    std::unique_ptr<Machine> m(new (storage) Machine());
    CHECK(shadow_is_clear(*m));
}

TEST(widescreen_render, reset_discards_pixels_from_both_shadow_banks) {
    auto m = level_machine();
    std::memset(m->fb_margin, 0xA5, sizeof m->fb_margin);
    m->reset();
    CHECK(shadow_is_clear(*m));
}

TEST(widescreen_render, bottom_clip_tracks_extra_rows_in_fixed_point_and_restores) {
    auto m = level_machine();
    auto* clip = m->sdram + patches::kClipRectSdram;
    patches::begin_frame(*m);
    CHECK(m->wide_active);
    CHECK_EQ(read32(clip + 8), uint32_t(239 << 8));
    CHECK_EQ(read32(clip + 4), 0u);
    CHECK_EQ(uint16_t(clip[0] << 8 | clip[1]), uint16_t(-56));
    CHECK_EQ(uint16_t(clip[2] << 8 | clip[3]), uint16_t(376));

    m->wide_extra_bottom = 0;
    patches::begin_frame(*m);
    CHECK_EQ(read32(clip + 8), uint32_t(223 << 8));
    m->wide_extra_bottom = 99;
    patches::begin_frame(*m);
    CHECK_EQ(read32(clip + 8), uint32_t(239 << 8));
    m->level_seen_frame = ~0ull;
    patches::begin_frame(*m);
    CHECK(!m->wide_active);
    CHECK(std::equal(std::begin(native_clip), std::end(native_clip), clip));
}

TEST(widescreen_render, extra_rows_display_shadow_sprites_including_centre) {
    auto m = level_machine();
    std::memset(m->fb_margin, 0, sizeof m->fb_margin);
    patches::begin_frame(*m);
    m->mars.adapter_ctrl = 1;
    m->mars.bitmap_mode = 0x81;
    m->mars.pal[1] = 31;       // native-buffer sentinel: red
    m->mars.pal[2] = 31 << 5;  // sprite: green
    auto* fb = m->mars.fb[1];
    for (int y = 0; y < kActiveLines; ++y) {
        const uint32_t word = (patches::kFbLineBase + uint32_t(y) * patches::kFbLineStride) / 2;
        fb[y * 2] = uint8_t(word >> 8);
        fb[y * 2 + 1] = uint8_t(word);
    }
    for (int y : {223, 224, 239}) {
        const uint32_t base = patches::kFbLineBase + uint32_t(y) * patches::kFbLineStride;
        fb[base + 160] = 1;
        fb[base + 161] = 1;
        m->sh2_write(0, 0x24020000 + base + 160, 2, 1);
        m->sh2_write(0, 0x24020000 + base + 320, 2, 1);
        CHECK_EQ(m->fb_margin[1][base + 320], 2);
        if (y >= kActiveLines) {
            CHECK_EQ(m->fb_margin[1][base + 160], 2);
            CHECK_EQ(fb[base + 160], 1);
        }
    }
    m->mars.fb_display = m->mars.fb_select_req = 1;
    m->run_frame();
    CHECK_EQ(m->fb_height, 240);
    for (int y : {223, 224, 239}) {
        CHECK_EQ(m->framebuffer[y * kScreenWidth + 53 + 160], 0xFF00FF00u);
        CHECK_EQ(m->framebuffer[y * kScreenWidth + 53 + 320], 0xFF00FF00u);
        CHECK_EQ(m->framebuffer[y * kScreenWidth + 53 + 161], y < kActiveLines ? 0xFFFF0000u : 0xFF000000u);
    }
}

TEST(widescreen_render, intro_script_restores_native_clip_without_changing_viewport_size) {
    auto m = level_machine();
    stand_in_for_the_engine_in(*m, 6, 0); // INTRODUCTION, level zero
    m->vdp.cram[0] = 0x000E;
    m->wram[patches::kPlaneAStruct] = 1; // camera is past the left level edge
    m->run_frame();
    CHECK(m->wide_active);
    CHECK_EQ(m->framebuffer[0], 0xFFFF0000u);

    // The encounter sets bit2; unrelated camera bits are already present.
    m->wram[0xC21C] = 0x24;
    m->run_frame();
    CHECK(!m->wide_active);
    CHECK_EQ(m->fb_width, 426);
    CHECK_EQ(m->fb_height, 240);
    CHECK(std::equal(std::begin(native_clip), std::end(native_clip),
                     m->sdram + patches::kClipRectSdram));
    bool sides_black = true, bottom_black = true, centre_preserved = true;
    for (int y = 0; y < m->fb_height; ++y) {
        for (int x = 0; x < m->fb_width; ++x) {
            const auto px = m->framebuffer[y * kScreenWidth + x];
            if (y >= kActiveLines) bottom_black &= px == 0xFF000000u;
            else if (x < 53 || x >= 373) sides_black &= px == 0xFF000000u;
            else centre_preserved &= px == 0xFFFF0000u;
        }
    }
    CHECK(sides_black);
    CHECK(bottom_black);
    CHECK(centre_preserved);
    // The scene may keep its flag until the next level finishes loading.
    stand_in_for_the_engine_in(*m, 6, 5);
    m->run_frame();
    CHECK(m->wide_active);
    CHECK_EQ(m->framebuffer[0], 0xFFFF0000u);
}

TEST(widescreen_render, intro_mask_does_not_cover_gameplay_or_other_level_events) {
    auto m = level_machine();
    for (uint8_t flags : {uint8_t(0), uint8_t(2), uint8_t(0x20), uint8_t(0x80)}) {
        stand_in_for_the_engine_in(*m, 6, 0);
        m->wram[0xC21C] = flags;
        patches::begin_frame(*m);
        CHECK(m->wide_active);
    }
    for (uint8_t zone : {uint8_t(0), uint8_t(3), uint8_t(5)}) {
        stand_in_for_the_engine_in(*m, zone, 0);
        m->wram[0xC21C] = 4;
        patches::begin_frame(*m);
        CHECK(m->wide_active);
    }
    stand_in_for_the_engine_in(*m, 6, 0);
    m->wram[0xC21C] = 4;
    patches::begin_frame(*m);
    CHECK(!m->wide_active);
    m->wram[0xC21C] = 0; // reset/reload the same introduction
    patches::begin_frame(*m);
    CHECK(m->wide_active);
}

TEST(widescreen_render, the_special_stage_is_found_whatever_the_game_mode) {
    // Issue #21: a giant ring starts the special stage from a level, and the
    // game mode stays the level's (0x18); only the stage select uses 0x20.
    auto m = std::make_unique<Machine>();
    for (int i = 0; i < 8; ++i) {
        m->sdram[patches::kSpecialProjectSdram + 2 * uint32_t(i)] = uint8_t(patches::kSpecialProjectCode[i] >> 8);
        m->sdram[patches::kSpecialProjectSdram + 2 * uint32_t(i) + 1] = uint8_t(patches::kSpecialProjectCode[i]);
    }
    for (int i = 0; i < 4; ++i) {
        m->sdram[patches::kSpecialRectSdram + 2 * uint32_t(i)] = uint8_t(uint16_t(patches::kSpecialRect[i]) >> 8);
        m->sdram[patches::kSpecialRectSdram + 2 * uint32_t(i) + 1] = uint8_t(patches::kSpecialRect[i]);
    }
    for (uint8_t mode : {uint8_t(0x18), uint8_t(0x20)}) {
        m->wram[patches::kGameMode] = 0;
        m->wram[patches::kGameMode + 1] = mode;
        CHECK(patches::special_stage_active(*m));
    }
    // A level's own rectangle is not the special stage, code or not.
    m->sdram[patches::kSpecialRectSdram + 1] = 0;
    CHECK(!patches::special_stage_active(*m));
}

TEST(widescreen_render, world_entrance_is_widened_but_not_the_ending) {
    // Issue #61 (formerly #13 and #56, kept 4:3): the lobby (levels 0 and 1)
    // and the final boss (2) are widened; the ending's scenes (3-6) are not.
    auto m = level_machine();
    stand_in_for_the_engine_in(*m, 7, 0);
    patches::begin_frame(*m);
    CHECK(m->wide_active);
    for (uint8_t level = 0; level <= 6; ++level) {
        stand_in_for_the_engine_in(*m, 7, level);
        CHECK_EQ(patches::wide_zone(*m), level <= patches::kFinalBossLevel);
    }
    stand_in_for_the_engine_in(*m, 7, 3);
    patches::begin_frame(*m);
    CHECK(!m->wide_active);
    // WIDE LOBBY is gone: wide_everywhere changes nothing.
    m->wide_everywhere = true;
    stand_in_for_the_engine_in(*m, 7, 4);
    patches::begin_frame(*m);
    CHECK(!m->wide_active);
}

TEST(widescreen_render, a_lobby_room_is_black_outside_its_range) {
    // Issue #61: the stage select room holds the camera at 2816..2816; its
    // margins would show the corridor beside it and, as it scrolls down,
    // blocks the engine never draws. A wide range is no room: its ends move
    // as the player goes on, and what lies beyond them was just in view.
    auto m = level_machine();
    auto put = [&](uint32_t at, int v) { m->wram[at] = uint8_t(v >> 8); m->wram[at + 1] = uint8_t(v); };
    auto clip = [&](uint32_t at) { return int16_t(m->sdram[patches::kClipRectSdram + at] << 8 | m->sdram[patches::kClipRectSdram + at + 1]); };
    stand_in_for_the_engine_in(*m, 7, 0);
    put(patches::kPlaneAStruct, 2816);
    put(patches::kPlaneAStruct + 0xA, 2816);
    put(patches::kPlaneAStruct + 0x8, 2816);
    patches::begin_frame(*m);
    CHECK(m->wide_active);
    const patches::MarginCut room = patches::margin_cut(*m, 53);
    CHECK_EQ(room.left, 53);
    CHECK_EQ(room.right, 53);
    // Nothing is shown there, so the SH-2 does not draw there either.
    CHECK_EQ(clip(0), 0);
    CHECK_EQ(clip(2), 320);
    put(patches::kPlaneAStruct, 1573);
    put(patches::kPlaneAStruct + 0xA, 1552);
    put(patches::kPlaneAStruct + 0x8, 7872);
    patches::begin_frame(*m);
    const patches::MarginCut corridor = patches::margin_cut(*m, 53);
    CHECK_EQ(corridor.left, 0);
    CHECK_EQ(corridor.right, 0);
    CHECK_EQ(clip(0), -56);
    CHECK_EQ(clip(2), 376);
}

#ifdef CHAOTIX_TEST_ROM
TEST(widescreen_render, intro_eggman_exit_is_masked_and_training_reopens_with_local_rom) {
    auto m = std::make_unique<Machine>();
    std::string error;
    const bool loaded = m->load_rom(CHAOTIX_TEST_ROM, &error);
    CHECK(loaded);
    if (!loaded) return;
    m->reset();
    m->wide_enabled = true;
    m->wide_extra = 53;
    install_recompiled_code(*m, true);
    int kick = -1, checked = 0;
    for (int f = 0; f < 3800; ++f) {
        uint16_t buttons = f >= 1400 ? PAD_RIGHT : 0;
        if (f >= 1440 && f < 3734 && (f - 1440) % 120 < 14) buttons |= PAD_C;
        if (f == 600) {
            m->stage_request = {};
            m->stage_request.place = 6;
            m->stage_request.level = 0;
            m->stage_request.player = 2;
            m->stage_request.combi = 7;
            m->stage_pending = true;
            kick = 0;
        }
        if (kick >= 0) {
            if (!m->stage_pending) kick = -1;
            else { if (kick % 24 < 6) buttons |= PAD_START; ++kick; }
        }
        m->input.pad[0] = buttons;
        m->run_frame();
        if (m->frame_count == 2200 || m->frame_count == 3800) {
            CHECK(m->wide_active);
            ++checked;
        }
        if (m->frame_count == 2400 || m->frame_count == 2900 || m->frame_count == 3500) {
            CHECK(!m->wide_active);
            bool masked = true, has_scene = false;
            for (int y = 0; y < kActiveLines; ++y) {
                for (int x = 0; x < m->fb_width; ++x) {
                    const auto px = m->framebuffer[y * kScreenWidth + x];
                    if (x < 53 || x >= 373) masked &= px == 0xFF000000u;
                    else has_scene |= px != 0xFF000000u;
                }
            }
            CHECK(masked);
            CHECK(has_scene);
            ++checked;
        }
    }
    CHECK_EQ(checked, 5);
}
#endif

TEST(widescreen_render, the_special_stage_result_reaches_the_68k) {
    // Issue #27: when a special stage is cleared, the SH-2 stores 1 at frame
    // buffer $1FE ($060074BA), and the 68K reads it there ($927A0E) to move on
    // to the next stage. Widened, everything outside the native columns went
    // to the host-side shadow, that store included: the 68K read 0 and the
    // first special stage came back every time. Bytes before the first picture
    // line are written as the hardware would; picture pixels are still moved.
    auto m = std::make_unique<Machine>();
    m->wide_active = m->wide_special = true;
    m->special_shift = 56;
    const int back = m->mars.fb_display ^ 1;
    m->sh2_write(0, 0x240001FE, 1, 2);
    CHECK_EQ(m->mars.fb[back][0x1FE], 0);
    CHECK_EQ(m->mars.fb[back][0x1FF], 1);
    CHECK_EQ(m->fb_margin[back][0x1FF], 0);
    // A polygon pixel at x' = 56 + 3 of line 0 is drawn at x = 3.
    m->sh2_write(0, 0x24000200 + 56 + 3, 0x7F, 1);
    CHECK_EQ(m->mars.fb[back][0x200 + 3], 0x7F);
    CHECK_EQ(m->mars.fb[back][0x200 + 56 + 3], 0);
}

TEST(widescreen_render, the_hud_keeps_to_the_left_edge_when_asked) {
    // Issue #28: the SH-2's HUD base X (MOV #32,R1 in the boot code) moves
    // left by the part of the margin that shows the level.
    auto m = level_machine();
    auto put = [&](uint32_t at, uint16_t w) { m->sdram[at] = uint8_t(w >> 8); m->sdram[at + 1] = uint8_t(w); };
    auto get = [&](uint32_t at) { return uint16_t(m->sdram[at] << 8 | m->sdram[at + 1]); };
    for (const auto& h : patches::kHudBaseX) { put(h.sdram, 0xE120); put(h.sdram + 2, h.next); }
    stand_in_for_the_engine_in(*m, 0, 1);
    // Camera well inside the level: the whole margin shows it.
    m->wram[patches::kPlaneAStruct] = 0x04;
    m->wram[patches::kPlaneAStruct + 1] = 0x00;
    patches::begin_frame(*m);
    CHECK_EQ(get(patches::kHudBaseX[0].sdram), uint16_t(0xE120));  // off by default
    m->wide_hud_edges = true;
    patches::begin_frame(*m);
    for (const auto& h : patches::kHudBaseX) CHECK_EQ(get(h.sdram), uint16_t(0xE100 | uint8_t(int8_t(32 - 53))));
    // At the level's start the left margin is blacked out: the HUD stays.
    m->wram[patches::kPlaneAStruct] = 0;
    m->wram[patches::kPlaneAStruct + 1] = 0;
    patches::begin_frame(*m);
    CHECK_EQ(get(patches::kHudBaseX[0].sdram), uint16_t(0xE120));
    // Code that is not the HUD's is left alone.
    put(patches::kHudBaseX[1].sdram + 2, 0x0009);
    m->wram[patches::kPlaneAStruct] = 0x04;
    patches::begin_frame(*m);
    CHECK_EQ(get(patches::kHudBaseX[1].sdram), uint16_t(0xE120));
}
