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
    for (uint8_t zone : {uint8_t(0), uint8_t(5), uint8_t(7)}) {
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
