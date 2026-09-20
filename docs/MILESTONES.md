# Milestones

完成的定義：**能建置、測試通過、行為經驗證**（不是「程式碼寫好了」）。

## Milestone 0 — Feasibility & Architecture ✅

交付：[`ARCHITECTURE.md`](../ARCHITECTURE.md)（執行架構分析、障礙、策略、IR、記憶體模型、同步模型、renderer、寬螢幕、build、驗證、待驗證假設）。

## Milestone 1 — 最小可執行原型 + CPU/recompiler + 測試 ✅

目標：開機 → 標題畫面 → 選單選擇 → 進入關卡，**以重編譯碼執行，且與參考直譯器 bit-exact**。

| 目錄 / 檔案 | 內容 | 狀態 |
|---|---|---|
| `CMakeLists.txt`, `cmake/ChaotixOptions.cmake`, `cmake/ChaotixGenerated.cmake` | 建置、選項、產生碼整合（固定分片） | ✅ |
| `src/cpu/m68k/m68k.h` | 狀態、bus fast path、ALU 語意（旗標、BCD、位移、MUL/DIV、例外、中斷） | ✅ |
| `src/cpu/m68k/m68k_decode.{h,cpp}` | 共用 decoder、block 邊界規則 | ✅ |
| `src/cpu/m68k/m68k_ops.h` | 共用指令語意（模板） | ✅ |
| `src/cpu/m68k/m68k_interp.{h,cpp}`, `m68k_disasm.cpp` | 參考直譯器、反組譯 | ✅ |
| `src/cpu/sh2/sh2.h`, `sh2_decode.{h,cpp}`, `sh2_ops.h`, `sh2_interp.{h,cpp}` | SH-2 同上（delay slot、DIV1、MAC、RTE/TRAPA） | ✅ |
| `src/runtime/rom.{h,cpp}` | ROM 載入、SHA-1、版本、checksum、MARS header | ✅ |
| `src/runtime/system.{h,cpp}` | Machine、排程、中斷、boot HLE | ✅ |
| `src/runtime/md_bus.cpp` | 68K 位址空間、手把（3/6 鍵）、SRAM、Z80 視窗 | ✅ |
| `src/runtime/sh2_bus.cpp` | SH-2 位址空間、32X 系統暫存器、FB、PWM、DREQ FIFO、code write tracking | ✅ |
| `src/runtime/sh2_onchip.cpp` | INTC、DIVU、DMAC、FRT、WDT | ✅ |
| `src/runtime/vdp.{h,cpp}` | MD VDP（port、DMA、中斷、HV、逐線 planes/window/sprites/S&H） | ✅ |
| `src/renderer/compositor.cpp` | 32X VDP（packed/direct/RLE）+ 優先權合成 | ✅ |
| `src/renderer/viewport.{h,cpp}` | simulation/render viewport 分離、aspect、scaling | ✅ |
| `src/renderer/image_io.{h,cpp}` | PNG 輸出、影像 hash | ✅ |
| `src/runtime/save.{h,cpp}` | Save API（SaveData/Config/ControllerMappings） | ✅ |
| `src/input/input.{h,cpp}`, `touch_controls.{h,cpp}` | 統一 InputState、虛擬按鍵 | ✅ |
| `src/game/recomp_api.h`, `recomp_dispatch.{h,cpp}` | 產生碼 ABI、dispatcher、RAM code 驗證、coverage recorder | ✅ |
| `tools/rom_analyzer/analysis.{h,cpp}`, `main.cpp` | code space、CFG、function、xref、MMIO 報表 | ✅ |
| `tools/recompiler/emit_m68k.cpp`, `emit_sh2.cpp`, `main.cpp` | C++ 產生器 | ✅ |
| `src/frontend/headless_main.cpp` | headless 執行、lockstep、trace、break、golden hash | ✅ |
| `src/platform/sdl_main.cpp`, `src/frontend/config.{h,cpp}`, `debug_overlay.{h,cpp}` | SDL3 前端 | ✅（Windows 實測） |
| `tests/` | 單元測試 + ROM 整合測試 | ✅ 3/3 ctest 通過 |
| `coverage/session1.cov` | 開機→關卡的執行 trace（只含位址） | ✅ |

驗證結果：`ctest` 全數通過（unit 33 項、`lockstep_boot_to_level` 3200 幀 bit-identical、`golden_frames`）。

## Milestone 2 — 音效（第一版完成 ✅，品質驗證待續）

已完成：Z80 直譯器、YM2612（log-sin/exp、相位/detune、EG、LFO、8 種演算法、回授、ch3 特殊模式、DAC、timer A/B）、
SN76489、PWM 混音、DC blocker、`SDL_AudioStream` 輸出（依模擬時間產生、以 frequency ratio 微調佇列）、
headless `--wav`。Lockstep 仍然 bit-exact（Z80 在兩邊一起執行）。

待做：SSG-EG、YM ladder effect、逐 sample 與硬體錄音比對。

原計畫內容：

1. `src/cpu/z80/z80.{h,cpp}`：Z80 直譯器（音效驅動在 Z80 上執行，程式由 68K 上傳；屬 audio compatibility layer）＋ 以 Z80 自身的 lockstep 單元測試。
2. `src/audio/ym2612.{h,cpp}`：FM 合成（6 聲道、4 operator、LFO、SSG-EG、DAC 聲道、timer A/B 與 status busy 旗標）。
3. `src/audio/sn76489.{h,cpp}`：PSG。
4. `src/audio/mixer.{h,cpp}`：依 MCLK 產生 sample（YM 53.69 MHz/144、PSG /16、PWM 可變 rate）→ 48 kHz 重取樣、混音。
5. 排程：Z80 加入時間片（68K 之後）；BUSREQ/RESET 真正生效；YM timer 讀取需與 Z80 同步。
6. SDL 音訊輸出（`SDL_AudioStream`），**以模擬時間為準**產生 sample，不讓音效驅動 simulation 速度。
7. 驗證：YM/PSG 暫存器寫入序列的 golden 波形 hash；lockstep 維持 bit-exact（Z80 加入兩邊）。

## Milestone 3 — 覆蓋率與效能 ✅

**覆蓋率**
- `tools/coverage/record_sessions.py`：8 個可重現的 session（開機→關卡、attract 30000 幀、Scenario 隨機遊玩、Training act 1–4、Options），以參考直譯器記錄到 `coverage/*.cov`（只含位址）。
- headless 新增 `--fuzz SEED:FROM:TO`（確定性隨機輸入）與 `--script FILE`。
- 靜態目標恢復（analyzer）：`jmp/jsr tbl(pc,Dn)` 的 **branch table**（140 個）、`move.w tbl(pc,Dx),Dy` + `jmp tbl(pc,Dy)` 的 **offset table**（12 個）、立即值/LEA 中的 **code pointer**（207 個）。錯判只會多產生不會被執行的程式碼。
- dispatcher：RAM 中由遊戲在執行期寫入的 `JMP abs.l` 跳板（例如 `0xFFFFE1A4`、V-int `0xFFFFC030`）以與直譯器相同的語意（12 cycles + block 邊界）直接執行後接回原生碼。
- dispatcher 統計新增 interpreter fallback 熱點清單。

**Held-out 驗證**（未用於 coverage 的輸入）

| Session | 68K 原生比例（前 → 後） | SH-2 |
|---|---|---|
| Training act 3, seed 99 | 99.9% → **100.0%**（9.6M 區塊中 1,032 次 fallback） | 100% |
| Scenario, seed 1234 | 96.6% → **99.7%** | 100% |
| Attract 36000 幀 | 99.3% → **100.0%**（4 次 fallback） | 100% |

**效能**（`--profile` 子系統計時；只做可被驗證為完全等價的最佳化）
- Renderer：MD plane 以「同一 tile row 的像素 run」處理、每行預先轉換 64 色 → video 0.675 → 0.312 ms/frame；**1,080 張參考畫面 hash 全部相同**。
- SH-2 idle-loop fast-forward（只在產生碼中）：`dt Rn; bf self` 延遲迴圈以封閉公式跳過；無 loop-carried state 且只讀取 ROM/SDRAM/cache/FB/COMM 的輪詢迴圈，在第一次迭代後一次扣掉剩餘時間片的 cycles（28 個迴圈）。依據：同一時間片內其他 CPU、中斷、計時器都不會改變狀態。直譯器仍逐次執行，**lockstep 36,000 + 12,000 + 9,000 幀 bit-identical**。
- 結果：held-out session 約 **1,090–1,210 fps（0.83–0.92 ms/frame）**，本 milestone 開始時約 780–900 fps。

**新測試**：`-DCHAOTIX_LONG_TESTS=ON` → `ctest -L long`：`lockstep_attract_long`（36,000 幀 lockstep）、`render_attract_long`（720 張 golden hash）。

待做：68K 輪詢迴圈同樣的 fast-forward、旗標活性分析、更多遊戲區域（其他 zone 需要可解鎖的存檔或更長的遊玩腳本）。

## Milestone 4 — 真正寬螢幕 ✅

設計與 ROM 分析證據見 [`ARCHITECTURE.md` §8](../ARCHITECTURE.md)。

- Renderer：MD 逐線輸出 [−E, 320+E)（planes、window、sprites；sprite overflow/collision 旗標仍依原生規則），32X 邊界由 host 影子緩衝提供；framebuffer 最寬 448 px。
- Patch 機制：68K host hook（`m68k::State::hook`），直譯器與產生碼在相同指令前呼叫 → 寬螢幕 lockstep bit-identical。
- 遊戲層：plane 串流環狀緩衝左移 80 px、32X 裁切矩形加寬（對齊 8）、環 sprite 可見範圍放寬、camera clamp 內縮、關卡場景判斷（其他場景 4:3 + 黑邊）。
- 使用者回報修正（第三輪）：填圖折返位置（`0x8F4F22` 重新讀取 plane.x）未套用位移，導致每次整列重畫少 8 欄 → 邊緣缺圖塊、垂直捲動時錯位、第二關之後明顯錯誤。新增驗證方式：4:3 與寬螢幕雙機比對中央 320 px（關卡中逐像素相同）。
- 使用者回報修正（第二輪）：環的 MD sprite 可見範圍、32X 裁切對齊 8、環狀緩衝偏移改為 96（左右餘裕平衡）、關卡鏡頭範圍外的邊界欄塗黑。
- 使用者回報修正：16:9（E = 53）時 32X 裁切邊界為奇數 → SH-2 address error → 32X 畫面凍結（「畫面殘留」）；環要進入 4:3 範圍才出現。兩者皆已修正，並以 E = 1…64 全部各跑 5000 幀確認 SH-2 正常。
- 前端：`Widescreen` 設定、F6、`--no-widescreen`；E 由顯示比例計算（16:9 → 426×224）；debug overlay 顯示 `wide E (active)`。
- 驗證：
  - E = 0：既有 5 項測試不變（包含 36,000 幀 lockstep 與 720 張 golden hash）。
  - 新測試 `widescreen_centre_matches_4_3`（4:3 與寬螢幕雙機並行，中央 320 px 必須逐像素相同；headless `--compare-native`，比對到鏡頭在關卡邊界依設計分歧為止）。
  - 新測試 `lockstep_widescreen`（E = 53，3200 幀）、`golden_frames_widescreen`（E = 64，標題 4:3 + 關卡）、`golden_frames_16_9`（E = 53，兩張關卡畫面；當機會讓畫面凍結而使 hash 不符）、`lockstep_attract_widescreen_long`（E = 53，36,000 幀，所有 attract demo zone）。
  - 注意：lockstep 本身抓不到「兩邊一起當掉」，因此寬螢幕測試一定要搭配 golden hash。
  - 邊界 nametable 內容與同一格在原生畫面中顯示時比對：E ≤ 80 時 > 99.5% 一致。
  - 截圖檢查 attract 中所有 zone（Botanic Base、Techno Tower、Speed Slider、Amazing Arena、Marina Madness）。

與原計畫的差異：物件啟動視窗與 culling 不需修改（32X 繪製器本身以裁切矩形判斷，物件在 ±64 px 內已存在）；
camera clamp 改變會影響依 camera 決定的物件啟動時機，因此「寬螢幕與 4:3 gameplay 完全一致」只在離關卡邊界 64 px 以上時成立。

待做：HUD 貼齊寬畫面左緣（選項）、特殊關卡 / Boss 房間的個別處理、>64 欄（需處理 plane 環狀緩衝的餘裕）。

## Milestone 5 — 全平台建置（設定已建立，待各平台實機驗證）

- `CMakePresets.json`：windows-msvc、windows-mingw、macos-universal、linux、android-arm64、ios。
- `platforms/android`：SDL3 Android Gradle 專案（`externalNativeBuild` 指向根 CMake）。
- `platforms/ios`：CMake Xcode 生成 + Info.plist、檔案匯入（Files app 放 ROM）。
- 各平台實機驗證 `--autotest` 截圖與 golden hash。
