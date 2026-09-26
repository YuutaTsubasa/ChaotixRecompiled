# Knuckles' Chaotix Recompiled — 架構文件 (ARCHITECTURE.md)

> 工程原則：**Correctness first, portability second, optimization third.**
> 本文件描述的每一項設計都已實作並經過驗證，除非明確標記為 *計畫中* 或
> **UNKNOWN — requires ROM analysis**。

---

## 0. 目前狀態摘要

| 項目 | 狀態 | 驗證方式 |
|---|---|---|
| ROM 驗證（SHA-1、版本偵測、MD checksum、MARS header 解析） | ✅ | `rom_analyzer`、啟動時檢查 |
| 68000 decoder / 共用語意 / 參考直譯器 | ✅ | 17 個單元測試 + lockstep |
| SH-2 decoder / 共用語意 / 參考直譯器（delay slot、DIV1、MAC、中斷） | ✅ | 16 個單元測試 + lockstep |
| ROM Analyzer：code space、CFG recovery、function map、xref、MMIO 報表 | ✅ | `rom_analyzer` 報表 |
| Static recompiler（68K + SH-2 → C++） | ✅ | lockstep 3200 幀 bit-identical |
| 32X / MD runtime（bus、VDP、32X VDP、comm、PWM、SH-2 on-chip、SRAM） | ✅ | 截圖 + golden hash |
| Boot HLE（不需要任何 BIOS 檔） | ✅ | 開機到標題畫面 |
| 標題 → 選單 → 存檔選擇 → 進入關卡 → 操作 | ✅ **（本階段目標）** | `lockstep_boot_to_level` 測試 |
| SDL3 前端（Windowed/Borderless/Fullscreen、HiDPI、integer/fit/stretch、aspect ratio、鍵盤/手把/觸控、debug overlay） | ✅（Windows 已實測） | `--autotest` 讀回實際輸出畫面 |
| 音效（Z80 直譯器 + YM2612 FM + SN76489 PSG + PWM 混音 → SDL3 audio stream） | ✅ 第一版（音質未經聽感/逐 sample 驗證） | WAV 輸出 + 頻譜檢查（有音高與節奏結構） |
| 真正寬螢幕（關卡中最多 448×224 ≈ 1.87:1，涵蓋 16:9 / 16:10；其他場景 4:3 加側邊黑條） | ✅ | `lockstep_widescreen`、`golden_frames_widescreen`、36,000 幀寬螢幕 lockstep；見 §8 |
| Android / iOS / macOS / Linux 建置 | ⏳ `CMakePresets.json`、Android Gradle 專案、iOS Info.plist 已建立；**尚未在這些平台實際建置** | 見 §9 |

實測數據（Windows x64、MinGW GCC 15、Release）：
- 重編譯後執行：約 **0.83–0.92 ms / frame**（原機一幀 16.7 ms），SDL 前端鎖定 59.92 Hz。
- 分派統計（未用於 coverage 的 held-out 輸入）：68K 區塊 **99.7–100%** 由原生產生碼執行，SH-2 **100%**（見 `docs/MILESTONES.md` Milestone 3）。
- Lockstep：重編譯版本與直譯器版本在 3200 幀（開機→標題→選單→關卡→跑、跳、吃環）中每一幀的 **所有 CPU 暫存器、cycle 計數、全部 RAM、VRAM/CRAM/VSRAM、32X frame buffer、輸出影像** 完全相同。

---

## 1. Knuckles' Chaotix / Sega 32X 執行架構分析

### 1.1 處理器與時脈（NTSC）

| 處理器 | 時脈 | 在本作中的角色（由 ROM 分析得出） |
|---|---|---|
| MC68000 | MCLK/7 ≈ 7.67 MHz | 遊戲主邏輯（物件、關卡、選單）、MD VDP 控制、Z80 控制、SRAM 存取、透過 comm port 對 SH-2 下指令 |
| SH-2 Master | MCLK×3/7 ≈ 23.01 MHz | 指令迴圈（`0x060008F8` 讀 COMM0 → jump table），32X 繪圖（frame buffer）、解壓縮、使用 DIVU |
| SH-2 Slave | 同上 | 將 ROM `0x7FC00` 的程式複製到 **cache data array（`0xC0000000`）** 執行，stack 也在 cache RAM（`SP=0xC0000800`）；PWM 相關 |
| Z80 | MCLK/15 | 音效驅動（程式由 68K 上傳） |

MCLK = 53.693175 MHz；1 條掃描線 = 3420 MCLK；1 幀 = 262 線；幀率 59.9227 Hz。

### 1.2 ROM 事實（已驗證）

- 檔案：3,145,728 bytes，SHA-1 `0c2fff7bc79ed26507c08ac47464c3af19f7ced7`，MD checksum `B61C`（header 與計算一致）。
- MARS user header（`0x3C0`）：`MARS CHECK MODE`，SH-2 image 位於 ROM `0x077800`，大小 `0x9000`，複製到 SDRAM `0x06000000`；Master entry `0x060001A0`、VBR `0x06000000`；Slave entry `0x060001A4`、VBR `0x06000080`。
- 68K reset vector → `0x3F0`（Sega 標準 ICD/security code），`0x200` 是 32X 例外 jump table（`0x880200 + 6·(n−1)`）。
- 68K 在啟用 adapter 後從 `0x880000–0x8FFFFF` 執行，另有 bank-switched `0x900000` 視窗程式（例如 bank 2 的 `0x928BAE`）。
- SRAM：header `RA F8 20`，奇數位址 `0x200001–0x2003FF`；存取流程為遊戲把 routine 複製到 `0xFF0200` 執行：RV=1 → `$A130F1=3/1`（啟用/可寫）→ 讀寫 256 bytes ×2 份（checksum 種子 `0x4B52`）→ `$A130F1=2`。

### 1.3 記憶體映射（本專案 runtime 實作）

**68000**（24-bit bus，但 PC 為 32-bit）
| 位址 | 內容 |
|---|---|
| `000000–0000FF` | ADEN=0：卡匣；ADEN=1：32X 向量 ROM（`0x70` 可寫 = H-int 向量；`0xC0–0xFF` 是 security code 會 `JSR $C0` 的小 routine，HLE 為 NOP…RTS） |
| `000100–3FFFFF` | 卡匣 ROM（SRAM 啟用時 `0x200000` 疊上 SRAM） |
| `840000–87FFFF` | 32X frame buffer（FM=0 時；`0x860000` 為 overwrite image） |
| `880000–8FFFFF` | 卡匣 ROM 前 512 KB |
| `900000–9FFFFF` | 卡匣 ROM 1 MB bank（`$A15104`） |
| `A00000–A0FFFF` | Z80 空間（RAM、YM2612） |
| `A10000–A1001F` | I/O（版本、手把） |
| `A11100 / A11200` | Z80 BUSREQ / RESET |
| `A130EC` | `'MARS'` |
| `A130F1` | SRAM 控制 |
| `A15100–A153FF` | 32X 系統暫存器、comm、PWM、32X VDP、palette |
| `C00000–C0001F` | MD VDP |
| `E00000–FFFFFF` | 64 KB work RAM（鏡像） |

**SH-2**（兩顆共用，`0x2xxxxxxx` 為 cache-through 鏡像）
| 位址 | 內容 |
|---|---|
| `00000000–00003FFF` | Boot ROM（HLE：無限迴圈 stub） |
| `00004000–000043FF` | 32X 系統暫存器（中斷遮罩為 per-CPU） |
| `02000000–023FFFFF` | 卡匣 ROM |
| `04000000–0403FFFF` | Frame buffer（`+0x20000` overwrite image） |
| `06000000–0603FFFF` | SDRAM 256 KB |
| `40000000…` | cache purge |
| `60000000…` | cache address array |
| `C0000000–C0000FFF` | cache data array（**本作 Slave 在此執行程式**） |
| `FFFFFE00–FFFFFFFF` | on-chip：FRT、INTC、WDT、DIVU、DMAC、BSC、CCR |

### 1.4 本作實際使用的硬體（由 `rom_analyzer` MMIO 報表）

- SH-2：`SYS_INTMASK`、`VINT/CMD/PWM clear`、`DREQ SRC/LEN/FIFO`、`COMM0/6/8/10/12/14`、`PWM CTRL/CYCLE/LCH/RCH`、`VDP MODE/FBCTRL`、**FRT**（同步初始化）、**CCR**（cache purge）、**DIVU**（7 處）。
- 68K：Z80 BUSREQ（35 處）/RESET、I/O、`MARS_ID`、SRAM 控制、adapter/INT/BANK/**DREQ**（FIFO）、COMM0/2/4/8/12、32X VDP mode、MD VDP data/ctrl、PSG。

---

## 2. Static recompilation 的主要技術障礙與對策

| 障礙 | 本作中的證據 | 對策 |
|---|---|---|
| 間接跳躍 / jump table | SH-2 指令迴圈 `jmp @r2`；68K `jsr tbl(pc,d0.w)` 物件分派（bra table）、物件 handler 指標 | recursive descent + 靜態 jump table / offset table / code pointer 恢復 + **trace-guided**（coverage 檔提供動態入口）；仍未知者由直譯器 fallback，永不出錯只會變慢 |
| 執行期產生的跳板 | `0xFFFFE1A4: jmp ($8F5380).l`（每關改寫目標） | dispatcher 以直譯器相同語意執行 `JMP abs.l` 後接回原生碼 |
| 等待迴圈（效能） | Slave `dt r0; bf` 延遲、Master 輪詢 COMM0 | 產生碼中對無狀態自迴圈做精確 fast-forward（同一時間片內外部狀態不變）；lockstep 證明等價 |
| 在 RAM 執行的程式 | SH-2 image 在 SDRAM；Slave 在 cache RAM；68K 在 `0xFF0200`、`0xFFFFC0xx` 跳板 | 以「code space」描述 runtime 位址 ↔ ROM bytes；RAM code 產生**驗證範圍**，runtime 比對 RAM 與 ROM，被改寫就改用直譯器 |
| Self-modifying / 覆寫 | 可能載入 overlay | 寫入含程式碼的 SDRAM page / cache 範圍會遞增 **code epoch**，下次進入時重新驗證 |
| Bank switching | `0x900000` 視窗 | 以 `(pc, bank)` 當 key 產生程式碼；dispatcher 依目前 bank 查表 |
| SH-2 delay slot | 大量 `bsr`/`jsr`/`rts` + slot | branch target（含 PR、RTE pop）先算，再執行 slot，再轉移；slot 內 PC-relative/非法指令由 analyzer 檢查（本 ROM：0 個） |
| 三顆 CPU 同步、busy-wait | comm port 輪詢、FS 等待、FEN 等待 | 固定時間片 + 只在 block 邊界檢查中斷/時間片；直譯器與產生碼使用**同一規則** → 可做 bit-exact lockstep |
| 中斷可能發生在任何時刻 | V/H/CMD/PWM | 中斷只在 block 邊界被接受（兩種執行方式一致）；時間片 1/4 掃描線，延遲遠小於硬體可觀察差異 |
| 32-bit PC | `jsr ($E1A4).w` 使 PC=`0xFFFFE1A4` 並 push 32-bit 返回位址 | PC 不截斷；產生碼只接手 24-bit 乾淨的 PC |
| code/data 混雜 | security code 後接 Z80 程式與字串 | 只從已知入口展開；解碼失敗 → INVALID（執行到才會例外，不會污染其他程式） |

---

## 3. 每種元件採用的策略

| 元件 | 策略 | 理由 |
|---|---|---|
| 68000 遊戲程式 | **Static recompilation → C++** | 遊戲邏輯主體；完全靜態可得 |
| SH-2 遊戲程式（Master/Slave） | **Static recompilation → C++**（含 RAM code 驗證） | 32X 繪圖與命令處理 |
| 指令語意 | **Transpilation 共用語意**（`m68k_ops.h`/`sh2_ops.h` 模板） | 直譯器與產生碼同源，避免兩套實作分歧 |
| 32X/MD 暫存器、中斷、DMA、FIFO、PWM timer、DIVU、FRT | **Runtime compatibility layer** | 硬體行為，無法「重編譯」 |
| 32X BIOS（68K、Master、Slave boot ROM） | **HLE**（`boot_hle_start_sh2`） | 不需使用者提供 BIOS；僅重現 comm handshake（`M_OK`/`S_OK`/checksum）與 SDRAM 載入 |
| MD VDP / 32X VDP 繪圖 | **精簡硬體抽象**（逐掃描線合成） | 遊戲依賴 H-int/逐線效果；輸出為一般 RGB 影像交給 renderer |
| Z80 音效驅動 + YM2612 + PSG | **Audio compatibility layer**：Z80 直譯器（`src/cpu/z80`）+ YM2612/PSG 合成（`src/audio`） | Z80 程式是 68K 在執行期上傳的資料；音效晶片必須模擬。YM timer/status 永遠以模擬時間推進（驅動程式可觀察），合成只在前端啟用音效時執行 |
| RAM 跳板（68K） | **參考直譯器 fallback** | 由遊戲在執行期產生，不存在於 ROM |

---

## 4. A. C/C++ transpilation vs B. LLVM IR

| 面向 | A. C/C++ | B. LLVM IR |
|---|---|---|
| Portability | 任何有 C++17 編譯器的平台（MSVC、GCC、Clang、Android NDK、Xcode）直接編譯 | 需要與各平台 toolchain 相容的 LLVM 版本；MSVC 生態不友善 |
| Optimization | 交給各平台最佳化器；我們以 `static constexpr` 指令 + `always_inline` 讓編譯器把定址模式/大小的分支全部常數折疊 | 可做更精細的旗標消除，但本專案瓶頸不在 CPU（1 ms/frame） |
| Debugging | 產生碼可讀（每行附反組譯註解），可直接下中斷點；可與直譯器 lockstep 比對 | 需要 IR 層級除錯工具 |
| Build complexity | 一般 CMake 目標 | 需額外整合 LLVM（體積大、版本管理） |
| Mobile | Android/iOS 原生 toolchain 直接支援 | iOS 禁止 JIT；AOT 可行但需自行管理 bitcode/目標三元組 |
| Undefined behavior | 以 `uint32_t` 無號運算、明確遮罩、`int64_t` 中間值避免 UB；位移計數在語意層檢查 | IR 可精確控制，但產生器更複雜 |
| Endian | 記憶體以**大端 bytes**儲存，讀寫時組合 → 與主機 endian 無關 | 同樣需要處理 |
| Memory model | 單執行緒、確定性排程；不依賴 C++ 記憶體模型 | 同 |
| 長期維護 | 語意集中在兩個 header；產生器只負責控制流 | IR 產生器與語意兩套 |

**選擇：A（portable C++17）。** 關鍵設計是「產生碼只編譯控制流，語意與參考直譯器共用」，使得驗證（lockstep）可以做到 bit-exact，而且每個平台都能用原生編譯器產生原生執行檔。

---

## 5. IR 設計

本專案的 IR 是**機器層級的已解碼指令 + 控制流圖**：

```
ROM bytes ──decoder──▶ m68k::Insn / sh2::Insn        （每條指令：op、size、EA/暫存器、立即值、
                                                      已解析的 PC-relative 位址、分支目標、cycles、flags）
          ──analysis──▶ InsnInfo → Block → Function   （leader、successor、call、indirect exit）
          ──emitter───▶ C++（goto 標籤 = block，native call = 子程式呼叫）
```

- `Insn` 是共用的：直譯器、反組譯器、analyzer、recompiler 全部使用同一個 decoder（`m68k_decode.cpp` / `sh2_decode.cpp`）。
- **Block 邊界規則** `ends_block(insn)` 也是共用的：分支、例外、SR 修改、STOP/SLEEP、特權指令。這條規則決定中斷與時間片在哪裡被觀察，是 lockstep 能 bit-exact 的基礎。
- Function = 入口 + 以 branch/fallthrough 可達的 block；call 目標成為新的 function。coverage 提供的動態入口只是 **leader**，只有沒被任何 function 包含時才成為新入口（避免重複產生，重複率約 1.2×）。
- 語意層（`op_*` 模板）是可執行的規格；產生碼以 `static constexpr Insn I = {...}` 呼叫它，編譯器負責把它特化成直線程式碼。
- 未來（*計畫中*）：在 IR 上做 CCR/T 旗標活性分析，省略未被讀取的旗標計算。

產生碼範例（SH-2，delay slot 與常數折疊）：

```cpp
  // 060008D8: bsrf r1
  { [[maybe_unused]] static constexpr sh2::Insn I = {...};
    c->cycles -= 2;
    uint32_t t_ = sh2::branch_target(c, I);   // 先取目標、設定 PR
    // slot 060008DA: mov #1,r0
    c->cycles -= 1;
    { ... sh2::exec_simple(c, I); }          // 再執行 slot
    c->pc = t_; SH2_BOUNDARY();               // 邊界：時間片/中斷檢查
    { int r_ = ::recomp::sh2_call_dynamic(c); // 原生呼叫，返回位址檢查
      if (!r_ || c->pc != 0x060008DCu) return r_; }
    goto L_060008DC;
  }
```

---

## 6. Runtime memory model

- **Fast path**：68K 以 64 KB page、SH-2 以 16 KB page 的指標表直接存取 ROM/RAM（大端 bytes）；其他位址走 callback 到 `Machine`（MMIO）。
- **大端儲存**：所有 guest 記憶體保持原始 byte 順序，讀寫時組合，主機 endian 無關（x64、ARM64 相同行為）。
- **MMIO**：`md_bus.cpp`（68K）、`sh2_bus.cpp`（SH-2 + 32X 共用暫存器）、`sh2_onchip.cpp`（per-CPU）。
- **Code write tracking**：SDRAM 中含產生碼的 16 KB page 不給 fast write 指標；寫入命中程式碼範圍時遞增 `sdram_code_epoch` / `cache_code_epoch[cpu]`。
- **Cache**：不模擬 cache 行為（資料一致性比硬體更強）；**cache data array 以 4 KB per-CPU RAM 實作**（本作必要）。*UNKNOWN — requires ROM analysis*：是否有畫面依賴 stale cache。
- **SRAM**：`$A130F1` 控制；由 `SaveStore` 持久化於 `SaveData/`。

---

## 7. 68K ↔ SH-2 同步模型

1. 時間單位為 MCLK。每條掃描線切成 4 個固定時間片（855 MCLK）。
2. 每個時間片依序執行：68K → Master SH-2 → Slave SH-2，各自執行到時間片終點（可超出少量 cycles，下個時間片扣回）。
3. 中斷在掃描線開始時產生（MD V-int 於第 224 線、H-int 計數器、32X V/H、FB swap），在 CPU 的下一個 block 邊界被接受；CMD/PWM 中斷在寫入/計時器觸發時立即更新 pending。
4. comm port、FIFO、FS/FEN/PEN 狀態讀取使用「讀取者自己的當下時間」（`m68k_now_mclk` / `sh2_now_mclk`）計算 HBLANK/VBLANK。
5. 整個模型是**確定性的**：相同輸入 → 相同結果，不依賴主機速度或執行緒排程。

*計畫中（最佳化）*：偵測 busy-wait 迴圈（comm 輪詢）並快轉 cycles，必須保持 lockstep 一致。

---

## 8. Renderer abstraction 與真正的寬螢幕

### 8.1 抽象

```
Machine（simulation）                                   Platform（SDL3）
 MD VDP 逐線 [-E, 320+E) → RGB + backdrop 標記 ┐
 32X VDP 逐線（邊界來自 fb_margin 影子緩衝）    ├─ 合成 → framebuffer (320+2E)×224 XRGB8888
                                               ┘              │
                                        compute_viewport(config, 輸出大小, image_aspect)
                                                              │
                                        SDL_Renderer（D3D11/12、Metal、Vulkan、GL/GLES）
```

- **Simulation viewport**（原生 320×224，寬螢幕時每側多 E 欄）與 **render viewport**（任意解析度）完全分離；輸出解析度不影響遊戲時序。
- `AspectRatio = Auto / 4:3 / 16:9 / 16:10 / 21:9 / W:H`；`Widescreen = true/false`（F6 切換，`--no-widescreen`）。
  E = `widescreen_extra(顯示比例)` = ⌊(320·A/(4/3) − 320)/2⌋，上限 64：16:9 → 53（426 px）、16:10 → 32（384 px）、21:9 → 64（448 px，外側 pillarbox）。
- 影像比例 `image_aspect = 4/3 · fb_width / 原生寬`（像素形狀不變，不拉伸）。`Stretch` 為明確選項。
- E = 0 時所有路徑與原版完全相同（既有 golden hash 與 lockstep 測試不變）。

### 8.2 遊戲層修改（`src/runtime/patches.{h,cpp}`）

**機制**：patch 不改 ROM 位元組，而是「在特定 68K 指令之前執行的 host hook」。`m68k::State` 帶排序好的 hook 位址表；
參考直譯器在每條指令前二分搜尋，recompiler 在相同位址的指令前輸出 `if (c->hook) c->hook(c, pc);`。
兩邊在完全相同的點呼叫同一函式，因此寬螢幕模式下 **lockstep 仍然 bit-identical**。
每幀開頭的 host 步驟（`patches::begin_frame`）只改 SDRAM 中的資料，對兩台機器同樣確定。

所有位址皆由本 ROM 的分析取得（證據如下）：

| 層 | ROM 分析結果 | 修改 |
|---|---|---|
| MD plane 串流（68K `0x9786–0x99B6`） | 每個 plane 以 512 px 環狀緩衝保存 **[cam_x, cam_x+512)**：往左捲時在 cam_x 畫新 16 px 欄，往右捲時在 prev_x+512 畫；列更新也從 cam_x 畫 512 px（`0x8F4E82` 列 / `0x8F4EAA` 欄；VBlank 時 `0x8F50F4` 寫入 VRAM）。量測：原版左邊界外 1 欄即失效、右邊有 192 px 餘裕。 | 在 12 個「d0 = plane.x」之後的點把 X 減去 W = 96（無遮罩版本夾在 ≥ 0），環狀緩衝變成 [cam−96, cam+416)，左右各留 96−E / 96−E px 餘裕。W 於整面重畫（`0x97AC`/`0x9810`）時鎖定。填圖程式另有一處（`0x8F4F22`）會**重新從結構讀 plane.x** 來算「這一列在 64 欄環狀緩衝裡的折返位置」，必須套用相同位移，否則每次整列重畫都會有 8 欄沒填到（畫面邊緣缺圖塊、垂直捲動時整列錯位）。驗證：同時跑 4:3 與寬螢幕兩台機器（相同輸入），關卡中**中央 320 px 逐像素完全相同**，直到鏡頭在關卡邊界依設計被夾住為止（E = 64 / 53 / 32 各約 1040 幀）。 |
| 32X 合成 | packed pixel / run length 模式的像素值 0（direct colour 的 0000h）在硬體上是透明的，顯示 MD 圖層，與調色盤內容無關。原本依調色盤的優先權位元判斷，當 pal[0] 沒有 through 位元時會畫成不透明黑色，蓋住 MD 背景（使用者回報：過場畫面的背景出現一塊塊黑色方塊）。 | `compositor.cpp` 改為值 0 即無 32X 像素。影響 attract 720 張 golden 中的 149 張（黑色區域改為顯示 MD 內容、天空底色改用 MD 背景色）。 |
| 32X sprite / 多邊形繪製（主 SH-2，SDRAM `0x06001380` 等） | 裁切矩形是資料：SDRAM `0x06003834` = int16 left/right、int32 top/bottom（原值 0/320/0/223，來自 ROM `0x7B034` 的 SDRAM 映像）。FB 行距 512 bytes、顯示 320。部分 zone 把行尾 padding 當資料儲存區。繪製器以 16-bit word 寫入與裁切邊界對齊的位址。 | 關卡中把裁切改為 [−C, 320+C)，C = E 進位到 8 的倍數（奇數邊界會讓 SH-2 發生 address error 而當機，E = 53 時曾發生）。sprite 寫入走 overwrite image 區；落在原生欄外的位元組改寫到 host 端 `Machine::fb_margin` 影子（不碰遊戲資料），auto fill 清除某行時一併清除該行影子。compositor 從影子讀邊界。 |
| 環（MD sprite，68K `0x1044`） | 環以 MD sprite 繪製；sprite 座標 X 只在 `0x70 ≤ x < 0x1D0`（畫面 −16…335）時才輸出。環物件本身在鏡頭前方 448–576 px 就已生成。 | 比較前把 d2 暫時 +E / −E、比較後還原，等效把範圍放寬為 [−16−E, 336+E)，輸出的 sprite 座標不變。 |
| 32X 物件的畫面外旗標（68K `0x182E`） | 物件在鏡頭 −64…+384 px 外設定 off-screen 旗標。 | 已涵蓋 E ≤ 64，不需修改。 |
| Camera clamp（68K `0x9A76`，所有 zone） | camera X（`$FFDFE8`）夾在 plane A 結構（`$FFC1DE`）的 [$A, $8]。 | 兩個界線各內縮 E（房間比畫面窄時置中），邊界不會顯示關卡外。 |
| 場景判斷 | 關卡每幀呼叫 plane 更新 `0x984E`/`0x98C4`。 | 最近 8 幀內有執行 → 寬螢幕；否則（標題、選單、特殊關卡、過場）4:3 + 黑邊。 |
| 關卡邊界 | 鏡頭夾制（上）讓邊界通常落在關卡內。房間比畫面窄時鏡頭置中，邊界會超出該房間的鏡頭範圍，但那裡仍是引擎從關卡配置串流進來的圖塊（實測：選關大廳等窄房間看起來是連續的場景）。 | `patches::margin_cut` 只把「關卡原點左側」（沒有配置資料）的部分塗黑。 |
| HUD | 由 32X 繪於固定位置 | 保持在原生 4:3 區域內（未移動）。 |
| 遊戲模式分派器（68K `0x3262`） | `move.w $FFDFDE,d0; andi.w #$78,d0; jsr $883270(pc,d0.w)` — 16 個模式各一格 `jmp`。每個模式處理常式自己跑迴圈，只有在該場景結束時才回到這裡，因此這是遊戲在兩個場景之間唯一會經過的點。 | 不改變執行；只在此處套用 host 要求的關卡（TIME ATTACK，見 §8.3）。因為直譯器與生成碼在同一條指令前呼叫同一個 hook，lockstep 仍 bit-identical（測試 `stage_select_starts_the_chosen_level`）。 |

**限制 / 已知差異**
- E 上限 64（plane 環狀緩衝兩側至少保留 16 px 餘裕）。21:9 以上會在外側 pillarbox。
- 關卡進行中才開啟寬螢幕時，W 要到下次載入關卡才鎖定，在那之前顯示黑邊。
- camera clamp 改變會影響依 camera 決定的物件啟動時機；寬螢幕模式下 attract demo 的重播結果與原版不同（E = 0 完全相同）。

### 8.3 遊戲自帶的 STAGE SELECT（TIME ATTACK 的來源）

Knuckles' Chaotix 內含一個**完整但出貨版到不了**的關卡選擇畫面：遊戲模式 `0x30`（分派表 `0x883270` 第 6 格 → `$8F6F6C`）。
它提供的欄位剛好就是 time attack 需要的全部，而且它自己的「按下 Start 開始」常式（`$8F72C8`，內含 `$8F7352`）就是遊戲正規的進入關卡路徑。
以下全部由 ROM 分析取得，非推測（`rom_analyzer --disasm` / `--refs-to`）：

| 畫面欄位 | 變數 | ROM 證據 |
|---|---|---|
| PLACE | `$FFFBC0`，開始時抄進 `$FFDFF2`（zone） | 名稱表 `$8F739E`（11 筆，每筆 0x15 bytes）；`$8F72F2 move.w $FFFBC0,$FFDFF2` |
| LEVEL | `$FFDFF4` | 合法性表 `$8F738A`：每個 place 一個 byte，bit N = 有第 N 關（五個遊樂設施 = `0x3E` → 1–5；TRAINING = `0x1F` → 0–4；INTRODUCTION = `0x3F` → 0–5）。`$8F7378` 就是用 `btst` 查這張表，不合法時只播「不行」的音效 |
| AT-TIME | `$FFDFF6`，只取 `and #6` | 名稱表 `$8F74B1`：MORNING / DAY / SUNSET / NIGHT |
| PLAYER / COMBI | `$FFE038` / `$FFE03A` | 名稱表 `$8F74E8`：MIGHTY、`**********`、KNUCKLES、CHARMY BEE、VECTOR、BOMB、HEAVY、ESPIO。開始時各 `lsl` 兩次（`$8F7352`），成為 `$8818E4` 那張角色美術指標表的 byte 位移；`$8818AE` 依此載入兩名角色的圖 |
| PLAYERS | `$FFE04C` | `$8F7362` 以 `$FFE04C` 查 `$8F739A` 的 2-byte 表，寫回 `$FFE04C` 與 **`$FFE05C`**（`0xFE` 或 `0x10`）。`$FFE05C` 是「某位玩家要讀哪一個手把緩衝」的位移：`$8A8778 lea $FFC138,a0; move.b $FFE05C,d0; addq.b #5,d0; adda.w d0,a0`，`0x10` 指到第二個手把的資料 |

最後 `$8F731A move.w #$18,$FFDFDE`，也就是關卡模式。

**驗證**：把 `$FFE038` 掃過 0/4/8/…/28 逐一截圖，得到的七個角色與上表名稱一一對應（slot 1 如其名是壞的，載入的是 MIGHTY 的圖卻配錯 mapping）；把 PLAYERS 設為 2 之後，第二個手把按住右鍵會讓 SDRAM（SH-2 的遊戲狀態）出現 1423 bytes 差異且夥伴實際分離，設為 1 時則是 0 bytes——**這個 ROM 確實有雙人模式**，只是出貨版沒有入口。
只把 `$FFE04C` 或 `$FFE05C` 在關卡中途改掉沒有作用；必須走 `$8F72C8` 的順序（`src/runtime/stage_select.cpp` 即為該常式的轉錄）。

**實作**：`runtime/stage_select.{h,cpp}` 是這些表與那段常式；`Machine::stage_request` / `stage_pending` 由前端寫入，
由 `patches.cpp` 在模式分派器 hook 套用（關卡處理常式自己跑迴圈，前端在幀邊界寫 `$FFDFDE` 不會生效）。
前端的 TIME ATTACK 頁只提供 place 0–6（七組真正可以跑的關卡）；`NOT USED` / `BONUS STAGE` / `SPECIAL STAGE` 走別的模式，未納入。

**尚未解決**：如何辨認「這一輪跑完了」（而不是死掉）。腳本輸入無法跑到終點，所以最佳成績的記錄還沒有做——計時仍然是遊戲自己 HUD 上的 `$FFE052`。

### 8.4 每個按鍵在遊戲裡做什麼（前端 CONTROLS 顯示用）

前端的按鍵設定頁要能說明每個鍵的功能，而這些不能用「Sonic 系列大概都這樣」帶過。
做法是在關卡裡取一個快照，然後用同一個瞬間重播，每次只按一個鍵，**逐幀比對畫面雜湊**與「什麼都不按」的差別
（單看最後的 SH-2 狀態會漏掉跳躍這種會落地還原的動作）。

| 鍵 | 觀察到的結果 |
|---|---|
| 上 / 下 / 左 / 右 | 抬頭 / 蹲下 / 左右移動 |
| A | 按下的**當幀**環數 −10（`$FFE008`），夥伴被拉向玩家，畫面出現 32X 精靈縮放的近景；連按時第二次未扣（動作進行中）。鏡頭在事後仍往前，不是把玩家拉回夥伴那邊 |
| B | 遊戲自己在畫面上印出 **`HOLD!`** 對話框 —— 夥伴定住 |
| C | 跳躍（可見跳躍弧線與滾球姿勢） |
| START | 暫停（畫面出現 `PAUSE`） |
| X / Y / Z / MODE | **完全沒有作用**。在標題畫面、按下 Start 後遊戲自己的畫面、以及關卡中各取快照測試，60 幀畫面 0 幀有差異、SH-2 狀態 0 bytes 差異；同批測試中作為對照組的 C 在關卡中是 57/60 幀有差異，證明測試本身有效 |

也就是說本作是三鍵遊戲。6 鍵手把仍可接（`$8F454A` 的控制器類型跳表會讀進 `$FFC136`/`$FFC137`），但多出來的鍵沒有被使用。

## 9. 共用 build 架構

```
CMakeLists.txt
 ├─ chaotix_cpu        (decoder / semantics / interpreters)      ─┐
 ├─ chaotix_runtime    (32X/MD runtime, renderer, input, save)    ├─ 所有平台共用
 ├─ chaotix_game       (dispatcher + generated/*.cpp)            ─┘
 ├─ rom_analyzer, chaotix_recomp   (host tools，非交叉編譯時才建)
 ├─ chaotix_headless   (驗證/測試)
 ├─ ChaotixRecompiled  (SDL3 前端，唯一的平台相依程式：src/platform/)
 └─ tests/             (ctest)
```

- **產生碼流程**：`-DCHAOTIX_ROM=<ROM>` → 建置時執行 `chaotix_recomp`（輸出檔名固定分片 16+8+1，CMake 可預先宣告）→ 編譯進 `chaotix_game`。
- **交叉編譯（Android/iOS）**：在主機上先產生 `generated/`，行動平台建置時偵測到既有產生碼直接使用（`ChaotixGenerated.cmake`）。
- Windows：Visual Studio / Ninja（MSVC 或 MinGW）；macOS：Xcode / Ninja；Linux：Ninja / Make；Android：Gradle + CMake（SDL3 Android 專案）；iOS：Xcode + CMake（`-G Xcode -DCMAKE_SYSTEM_NAME=iOS`）。
- 平台特定程式碼只在 `src/platform/sdl_main.cpp`；遊戲與 runtime 不引用任何 DirectX/Metal/Android API。
- 存檔路徑：`SDL_GetPrefPath` → `SaveData/`、`Config/`、`ControllerMappings/`、`Screenshots/`。

---

## 10. Validation strategy

| 層級 | 內容 | 工具 |
|---|---|---|
| Instruction Tests | 手冊已知答案：旗標、BCD、位移、MUL/DIV、MOVEM、例外、DIV1 除法序列、MAC 飽和、delay slot 順序 | `chaotix_tests`（33 項） |
| CPU State / Memory Tests | 每幀比較直譯器與重編譯版本：暫存器、cycle、RAM、SDRAM、cache RAM、VRAM/CRAM/VSRAM、32X FB/palette/comm | `chaotix_headless --lockstep`（ctest `lockstep_boot_to_level`） |
| Rendering Tests | 固定輸入腳本下的輸出影像 hash（golden）；SDL 前端 `--autotest` 讀回實際呈現畫面 | ctest `golden_frames`、`ChaotixRecompiled --autotest` |
| Gameplay Tests | 腳本輸入：開機→標題→選單→存檔→關卡→移動/跳躍/撿環 | 同上 |
| Coverage | 記錄執行過的 block，回饋給 recompiler | `--coverage` |

「Original behavior」目前以本專案的參考直譯器代表。*計畫中*：以獨立的第三方參考（真機或成熟模擬器的 trace）比對參考直譯器，以驗證 runtime 硬體行為本身。

---

## 11. Debugging

- `ChaotixRecompiled`：F1 overlay（render/sim FPS、emu/present ms、原生執行比例、68K/SH-2 PC/SR、comm、32X 狀態），F5 切換頁面（68K 暫存器、SH-2 暫存器、記憶體監看），F12 截圖，Tab 快轉。
- `chaotix_headless`：`--trace CPU:FROM:TO:FILE`（逐指令 trace 含暫存器）、`--break CPU:PC`（block 歷史 + 暫存器）、`--state-every N`、`--shot`、`--coverage`、`--lockstep`、`--expect-hash`。
- Logging：`runtime/log.h`，未實作的硬體存取以 rate-limited log 回報。

---

## 12. 法律與專案邊界

- 儲存庫**不包含** ROM、BIOS 或任何遊戲素材；`.gitignore` 排除 `__ROM__/`、`generated/`、截圖。
- 使用者自行提供合法取得的 ROM；工具只做 **驗證（SHA-1）→ 分析 → 重編譯 → 執行期載入**。未通過驗證的 ROM 不使用產生碼。
- `generated/` 由使用者的 ROM 產生，屬衍生資料，不得散布；coverage 檔只含位址與偏移量（不含 ROM 內容）。
- 不需要 32X BIOS：開機以 HLE 重現。

---

## 13. 必須以 ROM 分析驗證的假設（狀態）

| 假設 | 狀態 |
|---|---|
| MARS header 各欄位與 SH-2 image 位置 | ✅ 已驗證 |
| Boot handshake：`M_OK`/`S_OK` 於 COMM0/COMM4、checksum 於 COMM8 | ✅ 已驗證（68K `0x9A6`、`0x7C2`） |
| Slave 在 cache data array 執行程式、來源 ROM `0x7FC00` | ✅ 已驗證（coverage 自動比對） |
| delay slot 內沒有 PC-relative / 非法指令 | ✅ 已驗證（analyzer：0 個） |
| SH-2 SDRAM 程式碼在執行期不被改寫 | ✅ 本測試路徑中驗證 0 次失效（其他場景 UNKNOWN） |
| SRAM 位置、控制方式、checksum | ✅ 已驗證 |
| 遊戲不依賴 cache 不一致（stale cache） | UNKNOWN — requires ROM analysis |
| DREQ FIFO 使用的場景與傳輸大小 | UNKNOWN — requires ROM analysis（已實作一般路徑） |
| 特殊關卡 / 過場的 32X 繪圖模式（RLE / direct color） | UNKNOWN — requires ROM analysis |
| Camera / 物件啟動視窗 / culling 的資料結構（寬螢幕所需） | UNKNOWN — requires ROM analysis |
| 音效驅動與 68K 的握手是否要求 Z80 實際執行 | ✅ Z80 已執行；流程不受阻，關卡載入時序與 Z80 互動略有變化（golden hash 已更新） |
| 6 按鍵手把在本作中的用途 | ✅ 已驗證：X / Y / Z / MODE 完全未使用，本作是三鍵遊戲（見 §8.4） |
| 遊戲自帶的 STAGE SELECT（模式 `0x30`）與其全部變數 | ✅ 已驗證（見 §8.3；含雙人模式 `$FFE05C`） |
| 關卡「跑完」的訊號（用於記錄最佳成績） | UNKNOWN — requires ROM analysis |
