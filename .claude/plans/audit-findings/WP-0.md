# WP-0 findings: measurement baseline

Date 2026-11-14, against 1.6.14 (86a9b4b).

## 1. ASan build recipe (worked first time)

```
cmake -B build-asan -S . -DCMAKE_BUILD_TYPE=Debug -DIBKR_BUILD_TESTS=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan -j4      # 1m12s wall, exit 0, binary 128 MB
```
Configure took 27 s. No build problems. Only warnings are the known ones
(e.g. OptionsChainWindow.cpp:1315 itmBadgeW).

**Git-ignore:** `build-asan/` is NOT ignored. `git status` shows `?? build-asan/`
(only `build/` is in .gitignore; the `CMakeCache.txt` pattern matches a file
inside it, not the directory). .gitignore was not edited. Do not `git add -A`;
delete the directory or ignore it locally before committing.

## 2. Start under ASan/LSan

Run: `DISPLAY=:0 ASAN_OPTIONS=detect_leaks=1 timeout 15 ./build-asan/ibkr-trading-app --console`
and the same with `timeout -s INT`.

- It starts, opens a window and runs (`[font] loaded ...`, `[notify] assets ...`).
  No ASan error (overflow, use-after-free) in either 15 s run.
- Both runs ended with exit 124 (killed by timeout). **No LeakSanitizer report
  was printed.** SIGTERM/SIGINT end the process without the normal exit path
  (the app installs no signal handler, and ASan does not run the leak check on
  a signal). There is no xdotool/wmctrl here to close the window. So **a
  leak-at-exit report could not be obtained**; it needs a normal close (menu
  Exit / window close button) by the user.
- **Surprise, tell the user:** the second run did not stay at the login screen.
  Its output shows `FinishConnect done`, open orders 37253 (BAG) and 37319
  (STK), and IB errors 354, 10276 and 10092. So the app connected to a running
  IB Gateway/TWS. Most likely an Enter key reached the focused login form
  (main.cpp:6521 "Enter submits the form"; display :0 is the user's session).
  It used the saved login form from app-prefs.cfg, and `SaveAppPrefsFile()`
  runs on connect (main.cpp:6528), so app-prefs.cfg may have been rewritten
  with the same values. The audit sent no orders. The run lasted 15 s. I could
  not tell whether the account was paper or live. No further runs were made.
- With stdout piped the version banner did not appear (std::cout buffered,
  process killed before flush). Not a bug.
- LSan reports in app code: none (no report obtained).

### Draft suppression file (not tested against a real report)
Saved at
`/tmp/claude-1000/-home-nedievas-development-ibkr-trading-app/a2603aef-0ea5-4559-a7b9-a48dac393fb6/scratchpad/wp0/lsan.supp`
(scratch only; the user should paste the list into `~/lsan.supp`):
```
leak:libvulkan
leak:libnvidia
leak:libGLX
leak:libEGL
leak:libGLdispatch
leak:libdbus
leak:libX11
leak:libxcb
leak:libXrandr
leak:libXi
leak:libfontconfig
leak:libfreetype
leak:libglfw
leak:libdrm
leak:libwayland
leak:libdzn
leak:libLLVM
leak:libspa
leak:libpipewire
```
This list is from knowledge of typical GPU/windowing noise, not from observed
output. Reading a report: a leak with any frame in src/ (main.cpp, ui/windows/*,
core/services/*) is ours; a stack only inside the libraries above is noise.
`fast_unwind_on_malloc=0` gives full stacks so app frames deeper in a stack are
not hidden by a library-level suppression.

## 3. Soak checklist for the user

### Launch (both parts)
Write the suppression list above to `~/lsan.supp`, then:
```
cd <repo>
ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:log_path=$HOME/.config/ibkr-trading-app/asan \
LSAN_OPTIONS=suppressions=$HOME/lsan.supp \
DISPLAY=:0 ./build-asan/ibkr-trading-app
```
Where output goes (main.cpp `RouteOutput`, line 8002): with no terminal on
stderr, stdout and stderr both go to `~/.config/ibkr-trading-app/app.log` (the
previous run is renamed `app.prev.log`; copy app.log away before a second
run). If started from a terminal, or with `--console`, output stays on the
terminal and no app.log is written. ASan/LSan write through `log_path`, so
their report lands in `~/.config/ibkr-trading-app/asan.<pid>`. The leak report
appears only if the app exits normally: quit with menu Exit or the window
close button, never Ctrl+C or kill. Do not press Enter on the login screen
unless you mean to connect.
Send back: `asan.<pid>`, `app.log`, and the memory numbers.

### Part A (any time, market closed is fine)
1. Launch as above. Connect to the **paper** account.
2. Open 3 Charts (different symbols), Order Book, Options Chain, Scanner, Watchlist.
3. Note memory as "A0": `ps -o rss= -p <pid>` (KB; get the pid with `pgrep ibkr-trading`).
4. Do each of these 20 times, noting memory after each block of 20:
   a. change chart symbol (all 3 charts);
   b. change chart style / timeframe;
   c. pan a chart left far enough to load older bars each time;
   d. load an Options Chain (different underlyings, include SPX);
   e. switch expiry tabs in the chain;
   f. run a Scanner scan;
   g. close and reopen each window (Chart, Order Book, Chain, Scanner, Watchlist, Portfolio, Orders, News);
   h. switch account in the menu bar (if you have 2+ accounts);
   i. disconnect and reconnect.
5. Note memory "A1". Leave 5 minutes idle, note "A2".
6. Close the app normally (Exit menu).
7. Copy `app.log`, and send it with `asan.<pid>` and the A0/A1/A2 numbers.

### Part B (market hours, 30-60 min)
1. Launch as above, connect, set up the normal layout: Order Book with depth
   and tape on, charts with live bars, News, Watchlist, Portfolio.
2. Every 10 minutes write down memory: Linux `ps -o rss= -p <pid>` (KB);
   Windows (non-ASan release build) Task Manager > Details > Working set.
3. At each check also note the log size: `wc -c ~/.config/ibkr-trading-app/app.log`
   (only when started without a terminal).
4. After 30-60 min close normally (Exit menu). Send `asan.<pid>`, `app.log` and
   the table (time, RSS, log bytes).
A table that flattens is fine; a steady climb (more than about 5 MB per
10 minutes) is a lead. ASan inflates RSS, so compare the trend, not the
absolute number.

## 4. Idle CPU / GPU baseline (market closed, nothing moving, normal layout)
Use the release build (`build/`), not the ASan one.
- Linux CPU: `top -b -n 6 -d 5 -p <pid>` (use the last 5 samples) or
  `pidstat -p <pid> 5 6`. Record %CPU.
- Linux GPU (NVIDIA): `nvidia-smi dmon -s u -c 12` (sm / mem %) or
  `nvidia-smi --query-gpu=utilization.gpu,power.draw --format=csv -l 5`.
  Under WSLg `nvidia-smi` may show nothing for the app; note that if so.
- Windows: Task Manager > Details, add the CPU and GPU columns, read
  ibkr-trading-app.exe after 60 s idle; Performance > GPU for the 3D engine.
- Take three readings: (1) normal layout, app focused; (2) window minimised
  (the iconified path); (3) app closed (system baseline).
