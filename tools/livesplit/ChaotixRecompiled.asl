// Knuckles' Chaotix Recompiled -- LiveSplit auto-splitter (issue #47).
//
// Start, split and reset follow the program's own speedrun timer, and game
// time is its IGT (the sum of the level clocks, docs/SPEEDRUN.md):
//
//   start  a run starts: START on DATA LOAD hands over to the game (or the
//          START RUN key / menu entry)
//   split  a level is finished: its clock stops at the goal (and, if the
//          option is on, INTRODUCTION hands over to the lobby)
//   reset  the run is reset: the game went back to its title screen (or the
//          RESET RUN key / menu entry)
//
// In LiveSplit: Edit Layout > + > Control > Scriptable Auto Splitter, and
// pick this file. Compare against Game Time to race the in-game timer.
//
// How it finds the game: the program keeps a block in its own image that
// starts with "CHAOTIX_LIVESPLIT" (src/frontend/livesplit.h). The script scans
// the program for it once, then reads fixed offsets from it:
//
//   +24 u32 version (1)       +44 u32 run state (0 none, 1 running, 2 stopped)
//   +32 u64 work RAM address  +48 u32 run id      +52 u32 splits
//   +40 u32 frame             +56 u32 IGT frames  +60 u32 level clock frames
//                             +64 u32 1 in a TIME ATTACK run
//
// The 68K work RAM ($FF0000-$FFFFFF) is 64 KB at the address at +32, in the
// 68K's byte order (big-endian words). The script reads the place from it
// ($FFDFF2) to tell INTRODUCTION's split apart.

state("ChaotixRecompiled") {}

startup
{
    settings.Add("intro", true, "Split when INTRODUCTION hands over to the lobby");
    settings.Add("reset", true, "Reset when the game goes back to its title screen");
    settings.Add("time_attack", false, "Also time TIME ATTACK runs");
    refreshRate = 60;
}

init
{
    vars.Block = IntPtr.Zero;
    var module = modules.First();
    var scanner = new SignatureScanner(game, module.BaseAddress, module.ModuleMemorySize);
    // "CHAOTIX_LIVESPLIT" and the first byte of its NUL padding.
    var target = new SigScanTarget(0, "43 48 41 4F 54 49 58 5F 4C 49 56 45 53 50 4C 49 54 00");
    IntPtr block = scanner.Scan(target);
    if (block == IntPtr.Zero)
        throw new Exception("Chaotix Recompiled: the LiveSplit block was not found; retrying");
    int version = memory.ReadValue<int>(block + 24);
    if (version != 1)
        throw new Exception("Chaotix Recompiled: LiveSplit block version " + version + ", this script reads 1");
    vars.Block = block;
    vars.Watchers = new MemoryWatcherList {
        new MemoryWatcher<long>(block + 32) { Name = "wram" },
        new MemoryWatcher<uint>(block + 44) { Name = "state" },
        new MemoryWatcher<uint>(block + 48) { Name = "run" },
        new MemoryWatcher<uint>(block + 52) { Name = "splits" },
        new MemoryWatcher<uint>(block + 56) { Name = "igt" },
        new MemoryWatcher<uint>(block + 64) { Name = "timeAttack" },
    };
    vars.Watchers.UpdateAll(game);
    // The run going when the script attached is not started: only a new one.
    vars.StartedRun = vars.Watchers["run"].Current;
    // A word of 68K work RAM ($FF0000 + offset), big-endian.
    vars.ReadWord = (Func<int, int>)(offset => {
        long wram = vars.Watchers["wram"].Current;
        if (wram == 0) return -1;
        byte[] b = memory.ReadBytes(new IntPtr(wram + offset), 2);
        return b == null ? -1 : (b[0] << 8 | b[1]);
    });
}

update
{
    vars.Watchers.UpdateAll(game);
    if (vars.Watchers["timeAttack"].Current == 1 && !settings["time_attack"]) return false;
}

start
{
    var run = vars.Watchers["run"].Current;
    if (vars.Watchers["state"].Current != 1 || run == vars.StartedRun) return false;
    vars.StartedRun = run;
    return true;
}

split
{
    var splits = vars.Watchers["splits"];
    if (vars.Watchers["run"].Changed || splits.Current <= splits.Old) return false;
    // INTRODUCTION's split is taken as the game hands over to the lobby
    // (place 7); a level's at its goal, in its own place.
    if (!settings["intro"] && vars.ReadWord(0xDFF2) == 7 && splits.Current == 1) return false;
    return true;
}

reset
{
    if (!settings["reset"]) return false;
    var state = vars.Watchers["state"];
    var run = vars.Watchers["run"];
    // Reset, or a new run started over the one going (it starts next).
    return (state.Old == 1 && state.Current == 0) || (run.Changed && run.Old != 0);
}

isLoading
{
    // Game time is the program's own IGT, given below.
    return true;
}

gameTime
{
    return TimeSpan.FromSeconds(vars.Watchers["igt"].Current / 60.0);
}
