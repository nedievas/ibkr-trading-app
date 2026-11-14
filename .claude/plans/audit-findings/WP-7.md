# WP-7 Files and logging (read-only audit, 2026-11-14, 1.6.14)

Overall: no per-tick or per-frame logging exists. One small waste (reconnect loop lines); everything else is cosmetic.

## printf / fprintf(stderr) inventory (src/, excluding vendor)
Searched `printf|fprintf|std::cerr|std::cout`; 61 call sites in 8 files (main.cpp 37, IBKRClient.cpp 8, TradingWindow 4, ChartWindow 4, NotificationService 4, ReplayWindow 2, NumberFormat.h comment only). Classification:
- **Startup / once per connect**: banner (`main.cpp:8070`), `[font]` 8195/8201, `[notify] assets` 8415/8421, `[log] redirect` 8037, `FinishConnect done` 4302, `Managed accounts` 4326, `Next valid order ID` 6097, `Display groups` 6077, `[news] entitled providers` 5745, `[news] topic feed` 2343 (one per provider).
- **Per user action**: `[ChartWindow] RequestNewData` ChartWindow.cpp:740 (per symbol / style change, 28 lines in the real prev log), `SetHistoricalData SHRINK/RATCHET` 424/433, `PrependHistoricalData session-filtered` 507 (per pan-left extend), `[ReplayWindow] SetDay` + `BarRangeForSession` ReplayWindow.cpp:101/127 and main.cpp:4446 (per Load), `[trade] Order #` TradingWindow.cpp:2244-2263, `[alerts]` main.cpp:677/781, `[presets]` 6635, `Connecting` / `Cancelled` / `Disconnected` 6188-6241.
- **Per order event** (IBKRClient.cpp): `[placeOrder]` + `leg conId=` 618-631 (one block per send; 25 + 36 leg lines in the real prev log), `[orderStatus]` 1200 and `[openOrder]` 1428 are deduplicated per order id by `m_lastOrderStatusLog` / `m_lastOpenOrderLog` (print only when the line changes, so the 5 s open-order re-read does not repeat them), `[whatIf]` 1344 / main.cpp:5766, `[IB Error ...]` main.cpp:5798 (per IB error; codes 200 on 22000-22999 and 2150 on 9001-9999 are already suppressed; known bulk codes 162/300/310/365/366 per 1.5.x notes).
- **Per tick / per frame**: none found.
- **Per timer (problem, small)**: see WP7-01.

### WP7-01  Reconnect loop writes 2 lines every 5 s for as long as the Gateway is down
Where:      src/main.cpp:6205 `printf("[IB] Auto-reconnect attempt to %s:%d...\n", ...)` and 6167 `printf("[IB] Reconnect failed - will retry in %.0fs.\n", ...)` (the latter contains an em dash, 3 UTF-8 bytes)
What:       `StartSilentReconnect` runs every `kReconnectIntervalSec` (5 s) while `LostConnection`; each failed attempt prints both lines. Real `app.prev.log` (Windows host): 258,458 bytes, 5,255 lines; 2,457 "Auto-reconnect attempt" + 2,455 "Reconnect failed" = 4,912 lines = 93 % of the file (about 3.4 h of Gateway down). Everything else in that file (orders, errors, news, connects) is ~340 lines. Current `app.log` (short session): 4,062 bytes, 62 lines.
Trigger:    Gateway / TWS closed or restarting while the app stays open.
Cost:       ~50 + ~55 bytes per 5 s = ~21 B/s = ~75 KB/hour = ~600 KB for an 8-hour outage; normal 8 h session without an outage: roughly 15-30 KB (extrapolated from ~300 order/IB lines per observed session). The real cost is signal: a long outage buries the useful lines, and since only one previous run is kept (`RouteOutput` renames app.log -> app.prev.log at start, `main.cpp:8026`), a crash followed by two quick restarts overwrites the log of the crash.
Severity:   cosmetic (disk size is trivial)
Confidence: high
Fix sketch: print the first failure and then one line per minute (or only on state change). Optionally keep app.prev2.log. Not worth doing alone.

### WP7-02  Unbuffered output: one write per log line
Where:      src/main.cpp:8047-8048 (`setvbuf(..., _IONBF, 0)`), Windows 8030-8031
What:       Deliberate (lines survive a crash and stdout/stderr stay in order). With glibc and MSVC an unbuffered `printf` formats into a temporary buffer and does one `write`, so cost is one syscall per line (~1-5 us local, more on a `/mnt/c` WSL path). At the observed ~300 lines per session and the 21 B/s reconnect loop it is unmeasurable.
Trigger:    any log line
Cost:       microseconds per line; < 1 ms per second even in the outage case
Severity:   cosmetic
Confidence: high
Fix sketch: none.

## Periodic / event file writes

All savers go through `core::services::AtomicWriteText` (write `<path>.tmp`, close, rename). Triggers:
- Once per second timer, **hash-diffed** (write only when the text changed): `chart-modes.cfg` (main.cpp:7765, `SaveChartModesFile` 2246), `chart-settings.cfg` (2447), `trading-settings.cfg` (2519), `scanner-settings.cfg` (2581), `singleton-settings.cfg` (2665) and `orders-history.cfg` (2709, same timer block), `watchlists.cfg` (2181) and `watchlist-settings.cfg` (3284). Nothing is written when nothing changed; the cost when idle is building and hashing 8 small texts each second (WP-4's territory; the real files are 0-3.5 KB each except orders-history.cfg at 22,598 bytes on the Windows host).
- `replay-windows.cfg`: only when `g_replayWindowsDirty` (set on group/open change and on a few edits, `main.cpp:3418/3447/7884/7887`), at most once a second; not hash-diffed, so a dirty flag with unchanged content still rewrites (trivial, user-action driven). The replay cursor itself is saved only on the next dirty write.
- `equity-curve-<account>.csv` (PortfolioWindow.cpp:455): every 15 s but only while `m_equityDirty`. `SampleEquity` sets dirty only when the value changed (`PortfolioWindow.cpp:366`), and account P&L updates every few seconds during market hours, so in practice it is written every 15 s while the market is open: file is ~25 bytes x (daily closes + today's per-minute points); real files 30 bytes - 5,186 bytes. 8 h = ~1,900 writes of <= ~15 KB = under 30 MB of writes, no growth (capped 3000 rows). Cosmetic.
- `price-alerts.cfg` (main.cpp:676): on every alert add / move / delete / fire only. `recent-symbols-*.txt` (RecentSymbols.h:41): on each symbol add only. `app-prefs.cfg`: on each Settings change plus Connect (14 call sites), small. `window-presets.cfg` (6633): on save/delete of a preset. `notifications.cfg` and `news-providers.cfg`: on setting change, using their own `.tmp` + rename (NotificationService.cpp:418, main.cpp:2300), not `AtomicWriteText`.
- Unconditional: none per frame.

## AtomicWriteText leftovers (src/core/services/state-io.h:67-92)
The stream is closed (scope ends) before `rename`, a failed write removes the `.tmp`, and a rename failure falls back to copy + remove. A crash between open and rename leaves one `<name>.tmp`, which the next save truncates and replaces, so at most one stale `.tmp` per target. Looked in both config folders (Linux `~/.config/ibkr-trading-app/`, Windows host `/mnt/c/Users/.../.config/ibkr-trading-app/`): **no `.tmp` files present**. No cleanup of orphan `.tmp` for a target that is never saved again (for example a closed feature), harmless.

### WP7-03  `scanner-settings.cfg`, `trading-settings.cfg`, `price-alerts.cfg` are 0-byte files
Where:      config folders (observed on Windows host: scanner-settings.cfg 0 B, trading-settings.cfg 0 B, price-alerts.cfg 0 B; Linux scanner-settings.cfg 0 B)
What:       Written empty on purpose when every window of that kind is closed / no alerts (1.5.63 behaviour). Not a defect.
Severity:   cosmetic
Confidence: high
Fix sketch: none.

## Sizes observed (names only, no content read)
Windows host config folder: app.log 4,062 B; app.prev.log 258,458 B; imgui.ini 7,996 B; window-presets.cfg 7,772 B (1 preset); orders-history.cfg 22,598 B; equity-curve files 30 - 5,186 B; other cfg files under 3.6 KB. Linux config folder: no log files (runs from a terminal, so `RouteOutput` does not redirect), imgui.ini 11,157 B, total folder ~100 KB.
