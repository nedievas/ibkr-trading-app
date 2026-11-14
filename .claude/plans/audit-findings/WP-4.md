# WP-4  Per-frame cost and idle load

Read-only audit, written 2026-11-14 against 1.6.14 (`86a9b4b`). **Nothing here
was measured**: every cost is an order-of-magnitude estimate from the code, with
the reasoning shown. Assumed unit costs (typical desktop CPU): `gmtime` + our
`IsUSDST` ~100-200 ns; an ImGui `AddLine` / `AddRectFilled` ~30-60 ns; ImPlot
`PlotToPixels` ~10-20 ns; short-string `==` ~3-5 ns; a heap allocation ~30-50 ns.
Realistic sizes: bars per chart 250-1200 by default (`TradingStyle` history
horizons), 20,000 after long pan-left sessions; 1-10 charts; ~30 positions;
~40 chain rows by default, 800 for SPX in ALL mode.

## 1. What runs, in order

### Main loop (`src/main.cpp:8440`), every iteration
1. `glfwPollEvents()` (never `glfwWaitEvents`).
2. `PollConnectState()`.
3. `g_IBClient->ProcessMessages()` (swaps the queue, runs every `onXxx` callback, 5 ms budget checked every 64 messages, `IBKRClient.cpp:757/922`; leftovers are re-queued at the front).
4. Deferred client delete; auto-reconnect poll (`glfwGetTime() >= g_reconnectNextAttempt`, 5 s).
5. `glfwGetFramebufferSize`; swapchain rebuild when the size changed.
6. Iconified branch (below).
7. `ApplyPendingLayout()`, `ImGui_ImplVulkan_NewFrame`, `ImGui_ImplGlfw_NewFrame`, `HandleWindowResize()`, `ImGui::NewFrame()`, `HandleFontShortcuts()`.
8. `RenderMainUI()` -> `RenderTradingUI()` when Connected / LostConnection.
9. `ImGui::Render()`, `FrameRender`, `UpdatePlatformWindows` / `RenderPlatformWindowsDefault`, `FramePresent`. Present mode is FIFO only (`main.cpp:1423`), so the loop is paced by vsync.

### `RenderTradingUI` (`main.cpp:7459`), every frame, in order
1. Host window, menu bar. The Windows / Presets menu bodies run only while a menu is open. Per frame outside them: `AccountLabel`, `CalcTextSize` x3, a `std::string` for the account label.
2. `DockSpace`.
3. `DrainStyleSwitchQueue()` (empty -> return; one pop per 1 s).
4. Timer flushes, each `glfwGetTime()` gated at 1 s: `SaveChartModesFile`, `SaveChartSettingsFile`, `SaveTradingSettingsFile`, `SaveScannerSettingsFile`, `SaveSingletonSettingsFile` + `SaveOrdersHistoryFile`, `SaveWatchlistsFile` + `SaveWatchlistSettingsFile`; replay save only when dirty; equity curve when dirty and 15 s elapsed.
5. `DrainCompanyNameQueue()` (one per frame).
6. Every frame: `PushUnguardedHintsToWindows()`, `PushAlertsToCharts()`, `CheckUnacknowledgedOrders()`, `RefreshOpenOrdersIfDue()` (5 s timer), chain-symbol change check, `NotificationService::Tick()`.
7. `Render()` of every chart, order book, scanner, news, watchlist, replay, then replay group/open detector, `PruneClosedWatchlists`, Portfolio, Orders, WSH, Options Chain, Strategy Analysis (input built here every frame while open), Notifications, `RenderAlertsWindow`, `RenderSettingsWindow`.
8. After it in `RenderMainUI`: toast overlay, custom title bar.

### Iconified branch (`main.cpp:8475`)
```cpp
if (glfwGetWindowAttrib(g_AppWindow, GLFW_ICONIFIED)) {
    ImGui_ImplGlfw_Sleep(10);
    continue;
}
```
It runs after steps 1-5, so while minimised the loop still polls events (about 100 iterations/s), drains the IB queue (every tick still runs its handlers, including `ComputeIndicators`), runs the connect poll and the reconnect timer. It skips everything from step 7 on: no ImGui frame, no savers, no `Tick()`, no order refresh, no alert push. See WP4-12 for the consequence.

## 2. Findings, ranked by estimated cost

### WP4-01  Chart draws and classifies every bar every frame, not just visible ones
Where:      src/ui/windows/ChartWindow.cpp:4398-4460 (`DrawCandlesticks`), 4681-4712 (`DrawVolumeChart`), 4618-4650 (`DrawSessionBands`), 4721 (RSI `PlotLine`); src/core/models/MarketData.h:123 (`BarSession`)
What:       `DrawCandlesticks` loops `for (int i = 0; i < n; i++)` over all `n = m_idxs.size()`: four `ImPlot::PlotToPixels` per bar, `core::BarSession((std::time_t)m_xs[i])` per bar on intraday charts (`gmtime` + `IsUSDST`), then `AddLine` + `AddRectFilled` + `AddRect`. There is no `i` range limit from `m_xMin/m_xMax`. ImGui does not CPU-cull primitives against the clip rect, so every off-screen bar still emits vertices. `DrawVolumeChart` repeats the O(n) shape (a max-volume scan plus one `AddRectFilled` per bar). `DrawSessionBands` calls `BarSession` up to twice per bar (the run-extension `while` plus the outer call). Every enabled indicator `PlotLine` is O(n) as well (ImPlot culls segments, not the loop).
Trigger:    Any visible chart (2 s of use). Cost grows with every pan-left (`PrependHistoricalData` has no cap).
Cost:       Per chart per frame, candles only: n x (4 x 15 ns + ~150 ns `BarSession` + ~150 ns draw calls) = ~0.35 us/bar. n=1000 -> ~0.35 ms; n=5000 -> ~1.7 ms; n=20000 -> ~7 ms plus up to ~300k vertices (16 per bar) uploaded each frame. Session bands add ~0.2-0.4 us/bar on intraday, volume ~0.1 us/bar, indicator lines ~0.05 us/bar/series. With 10 charts at n=1000: 4-8 ms/frame of a 16.7 ms budget; at 20k bars one chart alone can exceed the budget. D1/W1/MN charts skip `BarSession` (about 40% cheaper).
Severity:   waste-cpu
Confidence: high on the code path, medium on the numbers (not measured).
Fix sketch: Compute `[iLo, iHi]` from `m_xMin/m_xMax` (idxs are sequential, `iLo = max(0, (int)m_xMin - 1)`) and loop only that range in `DrawCandlesticks`, `DrawVolumeChart`, `DrawSessionBands`. Cache a `std::vector<uint8_t> m_sessions` built in `RebuildFlatArrays` / the live-append paths so `BarSession` is not called per frame. Pure helper `VisibleIndexRange(idxs, xMin, xMax)` next to `VisiblePriceRange`, tested.

### WP4-02  Every last-price tick recomputes all indicators over all bars, also for closed charts
Where:      src/ui/windows/ChartWindow.cpp:568-588 (`OnLastPrice`), 635-664 (`OnDayTick`), 515-565 (`UpdateLiveBar`), 4812 (`ComputeIndicators`), 4851 (`DetectStructure`); src/main.cpp:4770-4775, 4797-4812
What:       `OnLastPrice` mutates the last bar's close and calls `ComputeIndicators()`. That rebuilds from scratch: `SMA` x2, `EMA`, `ComputeBollinger` (O(n x period) with two inner loops), `RSI`, a `sessionStarts` pass that calls `std::gmtime` twice per bar, `SessionVwap`, then `DetectStructure` (ATR, regression, Donchian with an O(n x N) window loop when on, `FindSwings`, `ClusterLevels`, `KeepTopN`, breakouts). About 15 `std::vector<double>` of length n are freshly allocated and moved in each time. Daily charts do the same for tick fields 4, 6, 7, 14 (`OnDayTick`). The handlers in main.cpp do not check `ce.win->open()`, so a closed chart whose subscription is still alive pays the same.
Trigger:    One last-price tick on a subscribed chart symbol (about 1-4 per second per active symbol; not measured), plus one `UpdateLiveBar` per ~5 s for intraday keepUpToDate.
Cost:       Per call: VWAP `gmtime` 2n x ~100 ns = 0.2 ms at n=1000 (4 ms at n=20k); Bollinger n x 20 x 2 x ~1 ns = 0.04 ms (0.8 ms at 20k); the other series ~0.01 ms/1000 bars each; allocations 15 x n x 8 B = 120 KB at n=1000 (2.4 MB at 20k); `DetectStructure` is capped by `scanCap=1000` for swings but ATR / Donchian run over n. Estimate ~0.3-0.5 ms per tick at n=1000, ~5-9 ms at n=20k. At 4 ticks/s x 10 charts x n=1000: ~12-20 ms/s (about 2% of a core); at n=20k the same load is ~200-360 ms/s. Not a frame-rate problem, but this runs inside `ProcessMessages`, so it consumes the 5 ms budget (see section 4).
Severity:   waste-cpu
Confidence: high (path), medium (rates and cost).
Fix sketch: (a) In the handlers skip the recompute when the window is closed (the window still needs the last bar updated, but not the indicators; recompute on reopen). (b) Hoist the `sessionStarts` computation out of `ComputeIndicators` into the bar-append / rebuild paths (needs only the new bar). (c) Longer term: update only the last element for SMA/EMA/RSI/VWAP; not recommended without a test seam, so list as an idea.

### WP4-03  Options chain: linear `FindQuote` over string-keyed quotes, `RecomputeExpectedMove` on every bid/ask tick, no row clipper
Where:      src/ui/windows/OptionsChainWindow.cpp:417-427 (`FindQuote`), 441-453 (`OnOptionPrice` -> `RecomputeExpectedMove`), 520-590, 1221-1700 (`DrawChainTable`), core/services/OptionChain.h:22 (`KeyEqual`)
What:       `FindQuote` scans `m_quotes` comparing `right`, `strike`, then two `std::string`s. `m_quotes` only shrinks in `CancelAll()` (expiry change / close / `RequestChain`); strikes that scroll out stay as `subscribed=false` entries, so it can grow to every contract touched in the current expiry. `DrawChainTable` has no `ImGuiListClipper`: it iterates `r.lo..r.hi` (default +/-20 = 41 rows, but `-1` = ALL, 800+ for SPX), per row an `isHidden` (two `DeadKey` string builds + set lookups), two `FindQuote`, and `snprintf`/`CalcTextSize` for every cell whether clipped or not. `OnOptionPrice` (bid/ask) and `OnOptionGreeks` call `RecomputeExpectedMove`, which builds a `ChainStrikeQuote` vector with two `FindQuote` (plus key string copies) per active strike and runs `ImpliedVolatilityVixStyle`.
Trigger:    Chain open on SPX/SPY with ~60-72 subscribed option contracts, each ticking bid/ask/model computation about once a second or faster.
Cost:       Default (41 rows, ~72 quotes): render 41 x 2 x 72 x ~4 ns = ~25 us + cell formatting ~100-200 us/frame; `RecomputeExpectedMove` ~41 x 2 x 72 x 4 ns = ~25 us + ~160 string allocations ~8 us, ~40 us per tick x ~150 ticks/s = ~6 ms/s. Worst case (ALL mode on SPX after scrolling: 800 rows, `m_quotes` ~1600): render 800 x 2 x 1600 x 4 ns = ~10 ms/frame (over budget), `RecomputeExpectedMove` the same ~10 ms per tick x 100+ ticks/s = a full core. Only the worst case is a real problem; the default is cheap.
Severity:   waste-cpu (default) / worst case could stall the UI in ALL mode
Confidence: medium (cost model; `m_quotes` growth read at 685-720, not exercised).
Fix sketch: Index `m_quotes` by a `std::unordered_map<std::string,size_t>` keyed with the existing `DeadKey` (already built per leg) so lookups are O(1); throttle `RecomputeExpectedMove` to a dirty flag evaluated once per frame; use `ImGuiListClipper` (rows have fixed height `rowH`). A pure `RowRange` helper would only be needed if the clipper conflicts with the overlay-rule capture.

### WP4-04  WSH markers: O(events x bars) with `gmtime` + `snprintf` per bar, every frame
Where:      src/ui/windows/ChartWindow.cpp:4515-4600 (`DrawWshMarkers`), called from `DrawCandleChart` (4390)
What:       For each WSH event it scans from bar 0 until the first bar whose formatted date >= the event date:
            ```cpp
            for (int i = 0; i < n; ++i) {
                std::time_t t = (std::time_t)m_xs[i];
                struct tm* tm = std::gmtime(&t);
                char barDate[11]; std::snprintf(barDate, ...);
                if (std::strcmp(barDate, evDate) >= 0) { plotIdx = m_idxs[i]; break; }
            }
            ```
            Upcoming events (earnings in the future, the common case) have no matching bar and scan all n. Also a per-event `sscanf`, lower-casing of `ev.type`, and a dashed line drawn as one `AddLine` per 10 px of plot height.
Trigger:    A chart with WSH events (needs the WSH entitlement; `g_wshDisabled` stops requests after error 10276 so this is empty for most sessions).
Cost:       events x n x ~250 ns (gmtime + snprintf + strcmp). 5 events x 1000 bars = ~1.2 ms/frame per chart; 5 x 20000 = ~25 ms/frame. Zero when there are no events.
Severity:   waste-cpu
Confidence: high (path), medium (numbers).
Fix sketch: Resolve each event to a bar index once (in `OnWshEvent` and after `RebuildFlatArrays`/`PrependHistoricalData`) and cache it in the event; binary search on a sorted date key. At minimum, precompute per-bar day keys and `break` at the first match using integer compare.

### WP4-05  Tables render every row; several rebuild + sort per frame
Where:      OrdersWindow.cpp:307, 374-389, 596-625 (`ids`, `groups`, history `rows` + `std::sort` per frame), 910-935 (`ComboLabel` -> `ComboStrategyLabel` -> `ClassifyStrategies` per BAG row per frame); WshCalendarWindow.cpp:360-385; WatchlistWindow.cpp:577 (`SortedIndices`); TradingWindow.cpp:664-1500 (`DrawOrderBook`); ScannerWindow.cpp (row loop); no `ImGuiListClipper` anywhere in `src/ui` (grep: no hits)
What:       No window uses a clipper, so every row's cell formatting runs each frame even when the row is scrolled out of the table (ImGui only skips the draw of clipped items). Orders History: a vector of up to 500 `Order*` is rebuilt and `std::sort`ed every frame and `DrawOrderRow` is called for all of them; each BAG row calls `ComboLabel`, which runs `ClassifyStrategies` on a synthetic position list (string-keyed, allocating) every frame. `DrawAccountFilter` copies every order's account string into a vector each frame. WSH Calendar sorts all events every frame. Watchlist's `SortedIndices` is a `stable_sort` whose comparator assigns `std::string sa, sb` from description/symbol per comparison. The DOM ladder submits up to 300 levels per side with, per row, two `InvisibleButton`s (`PushID`), `FindDomOrder` (linear), `IsMouseHoveringRect`, a `m_volAtPrice` hash lookup and several `snprintf`.
Trigger:    A table window open and visible; cost scales with rows.
Cost:       Orders History with 500 rows: sort ~500 x 9 compares x 30 ns = ~0.15 ms; `DrawOrderRow` ~15 cells x ~0.3 us = ~5 us/row = ~2.5 ms/frame; with 100 BAG rows x `ClassifyStrategies` (~5-20 us each: several vector/string allocations) another ~0.5-2 ms. Total ~3-5 ms/frame at the file cap, ~0.3 ms with a typical 50 rows. Watchlist (100 items, description sort): ~700 compares x 2 string copies = ~70 us/frame. DOM at 300 levels/side: 600 rows x ~5-8 us = ~3-5 ms/frame; at the default 25 rows ~0.4 ms.
Severity:   waste-cpu
Confidence: medium (counts from code, per-row costs estimated).
Fix sketch: `ImGuiListClipper` over the sorted vector for Orders History / Open, WSH, Watchlist (rows have uniform height; the Orders bracket-tree rows do not, so clip the History tab only). Re-sort only when the order set or sort spec changes (a `m_ordersVersion` bumped in `OnOpenOrder`/`OnOrderStatus`/`RemoveOrder`/`LoadHistory`). Cache `ComboLabel` per orderId until `m_comboLegMeta` changes.

### WP4-06  Volume profile recomputed every frame over all bars
Where:      src/ui/windows/ChartWindow.cpp:1735-1750 (`DrawVolumeProfile`), core/services/ChartAnalysis.h:1038-1094
What:       `m_vp = ComputeVolumeProfile(m_highs, m_lows, m_volumes, priceLo, priceHi, m_ind.vpBins)` runs from `DrawCandleChart` each frame when the VP checkbox is on, over all n bars (not only visible ones; it does clip by price). Inputs change only on pan / zoom of the Y range or when bars change.
Trigger:    `m_ind.volumeProfile` on (Day Trading and Swing presets enable it).
Cost:       n x ~10 ns for the price test + overlap loops for bars in range (each bar touches a few of 50 bins, ~20-60 ns): ~0.03-0.08 ms at n=1000, ~1-1.5 ms at n=20k per chart per frame. Plus one `std::vector<VolumeBin>` allocation per frame.
Severity:   waste-cpu
Confidence: high (code), medium (numbers).
Fix sketch: Cache by `(priceLo, priceHi, vpBins, barRevision)`; with auto-Y on, the range changes only when the visible range or last bar changes. A `m_barRevision` counter bumped in the data-mutating paths is the cheapest invalidation signal.

### WP4-07  Strategy Analysis window rebuilds its input and re-samples curves every frame while open
Where:      src/main.cpp:7895-7918; OptionsChainWindow.cpp:2204-2277 (`BuildAnalysisInput`), 2143 (`RecomputeTicketMetrics`); StrategyAnalysisWindow.cpp:182-330 (`DrawPayoffPlot`), `DrawStatsStrip`; PortfolioWindow `BuildAnalysisInput` -> `BuildPositionAnalysis`; main.cpp `AddChainOrderToAnalysis`
What:       While `open()`, each frame: input built (chain: one `FindQuote` per leg, `DaysToExpiry` per leg looped over `m_meta.expirations` with string compares; pinned: `BuildPositionAnalysis` with an `ImpliedVolFromPrice` bisection per option leg, ~26 iterations of `BlackScholesPrice`), `AddChainOrderToAnalysis` (second input + `CombineLegSets` + `ComputeStrategyMetrics`) when the add-order box is on, then in the window `BreakevensAtExpiry` x2, `ProbPayoffAtLeast` x2, and the 256-point sample loop calling `PayoffAtExpiry` and (when time value remains) `TheoreticalPnL` (one `BlackScholesPrice` per option leg per sample, `erfc` inside), plus `LognormalPdf` x256. Four std::vector<double>(256) allocated per frame. Inputs change only when quotes, the cart, the pinned legs, `m_evalDays` or the zoom change.
Trigger:    Strategy Analysis window open.
Cost:       4 legs x 256 samples x ~150-200 ns = ~0.15-0.2 ms (`TheoreticalPnL`) + ~0.03 ms (payoff, pdf) + ~0.05-0.15 ms input building (+ ~0.1 ms for pinned IV bisection: 4 legs x 26 x ~100 ns). About 0.3-0.6 ms per frame. 6 legs ~0.5-0.9 ms.
Severity:   waste-cpu (small)
Confidence: medium.
Fix sketch: Cache the sampled curves keyed on a hash of (legs, netPrice, spot, lo, hi, evalDays); the cheapest invalidation is comparing the `Input` struct plus (lo, hi, evalDays) to the previous frame. Don't add per-field versioning.

### WP4-08  Portfolio re-classifies strategies and rebuilds node ids every frame
Where:      src/ui/windows/PortfolioWindow.cpp:757-812 (`ClassifyStrategies`, `SortStrategyGroups`, `StrategyGroupKey`, `nodeId` string, `GetID`), `DrawPositionRow` labels (1293-1330); OptionStrategy.h:144-600
What:       Each frame in grouped mode: an `unordered_set` of ungrouped conIds, a `links` vector copied from `m_manualMerges` + `m_comboLinks`, `ClassifyStrategies` (bucket by underlying, `std::sort` of leg indices with string compares on `expiry`, `tryNamedMulti`, `decompose`, label building by string concat), `SortStrategyGroups`, the `m_lastGroups` rebuild, and per group `StrategyGroupKey` (vector<string> + sort) + `nodeId` + `ImGui::GetID`. Inputs (`m_positions` structure, ungroup/merge/link sets, sort) change only on position events, user clicks and sort; the rollups (prices / P&L) change every second via `OnPnLSingle`.
Trigger:    Portfolio window open, "Group" on.
Cost:       ~30 positions, maybe 8 groups: roughly 150-300 small allocations (~10 us) + sorts + string compares: ~50-150 us per frame. Minor.
Severity:   waste-cpu (small)
Confidence: medium.
Fix sketch: Cache `groups` keyed on a `m_positionsStructureRev` bumped when a position is added / removed / qty changes (not on price updates) plus the sets' revisions; re-run only the cheap rollup. Low priority: only do it together with other work in that file.

### WP4-09  Per-frame main.cpp pushes run for closed windows and copy small objects
Where:      main.cpp:791-798 (`PushAlertsToCharts`), 957-1072 (`PushUnguardedHintsToWindows`), 1770 (`CheckUnacknowledgedOrders`)
What:       Per frame, for every chart / order-book entry (open or not): `getSymbol()` returns a `std::string` copy (several times each), `PushAlertsToCharts` builds a `std::vector<PriceAlert>` per chart and moves it into the window (`SetPriceAlerts` reassigns, so the old vector is freed), `PushUnguardedHintsToWindows` builds `symToChart`, calls `getAutoLevels()` (copies two small `Level` vectors) per chart and again per trading window, copies a hint struct (with a string) into each window, and `SetAutoLevels` copies two `std::vector<double>`. None of these inputs change per frame.
Trigger:    Always while connected.
Cost:       10 charts + 10 order books: ~100-200 heap operations per frame = ~5-10 us. Negligible per frame; listed because the plan asked and because it is a pattern (allocation per window per frame) rather than a cost. Closed windows are included in the loops, but the work is tiny.
Severity:   cosmetic
Confidence: high.
Fix sketch: None needed. If touched anyway: skip windows with `!open()`.

### WP4-10  Once-a-second savers
Where:      main.cpp:2175 (`SaveWatchlistsFile`), 2225 (`SaveChartModesFile`), 2426-2455, 2498, 2560, 2629-2670 (singleton), 2702 (`SaveOrdersHistoryFile`), 3263 (watchlist settings); OrdersWindow.cpp:51-110 (`SerializeHistory`)
What:       Every second, each builds its full text and hashes it (`std::hash<std::string>`); a write happens only when the hash changes (confirmed in every function; `SaveOrdersHistoryFile` returns early only on empty text). Per-instance texts go through `StateBlock` (`unordered_map<string,string>`) + `FormatStateBlocks` (sorted keys). Orders history is the heaviest: up to 500 terminal orders x ~20 fields (`SetDouble` formats with `%.6g`), and for each BAG order `ResolvedComboLabel` -> `ClassifyStrategies`. Watchlists serialise up to 10 x 100 symbols through an `ostringstream`.
Trigger:    Always while connected (these also run when windows are closed, they just skip the closed entries).
Cost:       Chart settings: 10 charts x ~100 keys = ~1000 map insertions x ~200 ns + sort ~0.2 ms = ~0.4 ms; trading + scanner + singleton + watchlist-settings together ~0.3-0.6 ms; orders history: 20 insertions x ~150 ns + format ~ 6-10 us/order = ~0.5 ms at 50 orders, ~3-5 ms at 500 orders; watchlists (1000 rows, `ostringstream`) ~0.3 ms. Total roughly 1-2 ms/s typical, ~5-8 ms/s at the caps; all land in one frame each second (a 1-8 ms hitch, below 1 frame except at the caps). No disk write unless something changed. Not a continuous CPU issue.
Severity:   waste-cpu (negligible)
Confidence: medium.
Fix sketch: None recommended. If a hitch is visible, stagger the savers across different frames (offset the timers) instead of all firing on the same frame; skip `SaveOrdersHistoryFile` until `m_orders` changed (a revision counter).

### WP4-11  Equity curve copies its series each frame
Where:      PortfolioWindow.cpp:1422-1470 (`DrawEquityCurve`)
What:       Four `std::vector<double>(n)` filled from `m_equityCurve` each frame (n up to 3000 on load, 20000 cap in memory) while the tab / panel is visible.
Cost:       n=3000: 12,000 doubles = 96 KB alloc + copy + one pass, ~20-40 us/frame. Negligible. `ImPlot::PlotShaded` + `PlotLine` over n points add the same again.
Severity:   waste-cpu (negligible)
Confidence: high.
Fix sketch: None needed.

### WP4-12  Minimised window: queued notifications, sounds and order refresh stall
Where:      main.cpp:8475 (iconified `continue`), 7857 (`NotificationService::Tick` is called only from `RenderTradingUI`), core/services/NotificationService.cpp:248-253, 293-307
What:       `Notify()` does not play a tone directly: it queues a `PendingPlay` (`dueAt = tNow` for the tone, `+ kVoiceDelayMs` for the voice) that only `Tick()` dispatches, and `Tick()` runs inside `RenderTradingUI`. The iconified branch skips the frame, so a price alert or fill that arrives while the window is minimised queues its tone + voice and plays nothing until the window is restored (then the backlog plays at once). The toast overlay and Notifications window are also not updated. The same branch skips `RefreshOpenOrdersIfDue` (TWS-placed orders go stale while minimised), `CheckUnacknowledgedOrders`, the savers (a crash while minimised loses unsaved changes since the last frame) and `DrainStyleSwitchQueue`. The alert check itself still works (`CheckPriceAlerts` runs from the tick callback in `ProcessMessages`).
Trigger:    Minimise the window with an alert / order active.
Cost:       Correctness, not CPU. Estimated effect: silent alerts while minimised, which defeats the point of an alert for a user who minimises the app.
Severity:   cosmetic (behaviour bug: silent notification; not in the plan's severity list, closest class)
Confidence: high (read both sides: queue-then-Tick; Tick only in RenderTradingUI).
Fix sketch: Call `g_NotificationService->Tick()` once per loop iteration before the iconified `continue` (it is a mutex + empty-deque check when idle). Optionally also run `RefreshOpenOrdersIfDue()` there. Any idle/capped mode has to keep Tick in the unconditional part of the loop for the same reason.

## 3. Closed and hidden windows

Every `Render()` starts with `if (!m_open) return false;` before any work (Chart 790, TradingWindow 495, Scanner 422, News 172, Watchlist 690, Replay 223, Portfolio 465, Orders 249, WSH 415, Chain 3166 (also `CancelAll()` once on the transition), Analysis 36; `RenderAlertsWindow` returns on `!g_alertsOpen`). `Begin()` returning false (collapsed / docked-hidden tab) also returns before any `Draw*`, **except** work placed before `Begin`:
- `ReplayWindow::Render` runs `ApplyEngineTick()` (clock advance, bar evaluation, `OnCursorMove` broadcast) before `Begin`, so a docked-hidden or collapsed replay keeps running (open-but-not-visible). Probably intended (a clock), but it is per-frame work for an invisible window.
- `ScannerWindow::Render` runs the auto-refresh timer and the simulated quote drift before `Begin`.
- `WatchlistWindow::Render` runs `ProcessCdQueue()` (one contract lookup per frame) before `Begin`.
- `WshCalendarWindow::Render` runs the closed->open transition check first.

Data and compute that continue for closed (`open()==false`) windows, because the IB callbacks in main.cpp don't gate on `open()`:
- Charts: `OnLastPrice` / `OnDayTick` / `UpdateLiveBar` -> `ComputeIndicators()` (WP4-02), as long as the chart's market data subscription exists (WP-5 owns whether closing cancels it; no `Cancel*` is tied to `open()` in the grep of `main.cpp`, lines 2231-2566 are the save paths only).
- Order books: `OnTick`, depth updates.
- `PushAlertsToCharts` / `PushUnguardedHintsToWindows` iterate closed entries (tiny, WP4-09).
- The Options Chain is the exception: closing it cancels its streams.

Menu-built strings (`snprintf` into `char lbl[64]`) exist only while the Windows menu is open. Window titles are `snprintf` into `char[80]` per open window per frame (~0.1-0.2 us each, ~20 windows -> ~3 us total) and ImGui hashes the title each `Begin` (the `###` suffix is hashed from the `#` only, but the whole string is scanned): negligible.

## 4. Leads from the plan, closed

| Lead | Result |
|---|---|
| `ClassifyStrategies` in `DrawPositions` per frame | **Confirmed**, small (WP4-08). |
| `ComputeVolumeProfile` per frame | **Confirmed** (WP4-06). |
| `PushAlertsToCharts` | **Confirmed**, negligible (WP4-09). |
| `PushUnguardedHintsToWindows` | **Confirmed**, negligible (WP4-09). |
| `UpdateAllChartPendingOrders` per frame | **Refuted.** It is not called from `RenderTradingUI`. The 14 call sites (main.cpp:1763, 1810, 1919, 1960, 5057-6000) are all inside order / fill / error callbacks or modify paths, i.e. event-driven. |
| `BuildAnalysisInput` + `ComputeStrategyMetrics` + curve sampling | **Confirmed**, ~0.3-0.6 ms/frame while the window is open (WP4-07). `ComputeStrategyMetrics` itself is O(legs x breakpoints) and trivial; the sampling loop dominates. |
| `RecomputeTicketMetrics` per frame | **Refuted as per-frame.** Called from 20+ edit sites (stepper, expiry, leg add/remove, price, qty) and `ApplyDefaultLimitIfReady` only while the default is pending; not per frame. Cost when it runs: `FindQuote` per leg + `ComputeStrategyMetrics`, ~10-30 us. |
| Chain `SyncSubscriptions` per frame | **Mostly refuted.** Per frame it does `VisibleStrikeRange()` and two int compares (debounced 250 ms, then re-arms at `now + 1e9`). The expensive part (desired/current vectors, `DiffSubscriptions` with two sorts) runs only after the visible range changed. Cheap. The real chain costs are in WP4-03. |
| Title strings built per frame | **Confirmed, negligible** (section 3). |
| Savers: cost per second | Estimated in WP4-10. |

### Other per-frame / per-event work the plan missed
- `ProcessMessages` re-queue: when a batch isn't fully handled in 5 ms, the rest is moved back to the front of `m_queue` (`IBKRClient.cpp:929-938`), a vector move of the whole remainder each frame. Only matters under backlog (depth flood), see the idle section.
- `ChartWindow::SetPriceAlerts` reassigns a vector per chart per frame (covered in WP4-09).
- `XTickFormatter` per visible tick label per frame: small.
- `DrawHoverTooltip` only when `m_hoverIdx >= 0`.

## 5. Idle-mode analysis (an idea, not a finding)

Context for the user's decision 3 (capped rate of ~10-15 fps when there is no input or no focus, full rate on input; not sleep-until-event).

What depends on frames continuing, and whether a cap would show:

| Behaviour | Where | Frame-driven? | Noticeable at 10-15 fps? | Breaks with sleep-until-event? |
|---|---|---|---|---|
| IB message drain (`ProcessMessages`) | main loop, before the frame | Per loop iteration, 5 ms budget per call (checked every 64 messages) | **Yes, if the cap also slows the drain.** The budget is per call: at 60 fps a flood gets up to ~300 ms/s of handler time; at 10 fps only ~50 ms/s. Each chart tick can cost 0.3-0.5 ms+ (WP4-02), so ~100 ticks/s would back the queue up. Fix: keep draining at a higher rate than the draw rate (call `ProcessMessages` on every wake, drawing only when due), or raise the budget in capped mode. | **Yes**: nothing wakes the loop when the reader thread pushes a message (`glfwPostEmptyEvent` is not used anywhere in the repo). The reader thread would need to post an empty event on `Push()`. |
| Price-alert check | `onTickPrice` -> `CheckPriceAlerts` | No (callback) | No | Same wake requirement as above. |
| Alert / notification sound dispatch | `NotificationService::Tick()` in `RenderTradingUI` | Yes | Tone/voice delayed by up to one frame period (~70-100 ms at 10-15 fps; voice already has a built-in delay) | **Yes** (and already broken when minimised, WP4-12). Move `Tick()` into the unconditional part of the loop. |
| Toast timeouts / fade | `NotificationOverlay.cpp` (clock-based) | Redraw-driven | Fade steps visibly coarser at 10 fps; timeouts correct | Toast would never disappear without a timed wake. |
| Reconnect timer | main loop, `glfwGetTime() >= g_reconnectNextAttempt` (5 s) | Per loop iteration | No | Needs a timeout on the wait (`glfwWaitEventsTimeout(<= 0.5)`). |
| Open-order refresh (5 s), unacknowledged-order check (5 s), `PruneOldHistory` (60 s) | `RenderTradingUI`, Orders `Render` | Frame-gated | No | Same: needs a timeout wake and the work must not live only inside `Render`. |
| Style-switch throttle (1 s/queue entry) | `DrainStyleSwitchQueue` | Frame-gated | No (1 s granularity) | Needs a timeout wake while the queue is non-empty. |
| Once-a-second savers | `RenderTradingUI` | Frame-gated | No | Need a timeout wake. |
| `DrainCompanyNameQueue`, Watchlist `ProcessCdQueue` | "one per frame" | **Yes: rate = fps** | **Yes**: 100 pending names take ~1.7 s at 60 fps, ~7-10 s at 10-15 fps (IB lookup rate limit not the bottleneck). Low impact (a column fills in slowly). | Would stall entirely. |
| Symbol search debounce (0.25 s, `SymbolSearch.h:144-151`) | `ImGui::GetTime()` in the widget | Frame-gated | Debounce can fire up to one frame late (<= 100 ms). Fine. | Typing generates input events (fine); the debounce expiry itself needs a timed wake. |
| Chain scroll debounce (250 ms) | `SyncSubscriptions` in `Render` | Frame-gated | Fine (<= 100 ms later). | Needs a timed wake after the last range change. |
| DOM follow-resume (4 s) | `TradingWindow.cpp:1086/1122` | Frame-gated | Fine | Needs a timed wake. |
| DOM fill flash (3 s fade), `flashAge += DeltaTime` | `TradingWindow::Render` | Frame-driven animation | Visibly steppy at 10 fps, time-correct | Animation would freeze. |
| Replay clock | `ReplayWindow::ApplyEngineTick`, `DeltaTime` | Time-based (uses `io.DeltaTime`) | Cursor moves in bigger steps; bar crossing still evaluated per crossed bar, so fills stay correct. At 10 fps and high speed several bars per frame. | Would stop advancing; needs continuous frames while playing. |
| Scanner auto-refresh, quote drift | `ScannerWindow::Render` | Frame-gated | No | Needs timed wake. |
| Text cursor blink, tooltip delay, double-click, `ButtonRepeat` (the ticket's -/+), hover highlights, tab/window animations | ImGui internals | Need frames | Cursor blink and repeat are slower but fine at 15 fps; at 10 fps `-`/`+` hold-repeat would step every 100 ms instead of every 50 ms. | Frozen until the next event. |
| Live chart data | tick handlers | Event-driven; the draw is redundant when nothing changed | No (data arrives about 4x/s) | A redraw is needed on each data event (post an empty event from the reader thread). |
| Iconified | main loop | Already ~100 iterations/s with a 10 ms sleep, no drawing | n/a | n/a |

Verdict: a capped rate (~15 fps with no input and no focus, full on input) is safe for everything on the list **provided three things stay outside the draw cap**: (1) `ProcessMessages` must run on every loop iteration (or its budget be scaled up), because the 5 ms per-call budget makes handler throughput proportional to fps; (2) `NotificationService::Tick()` and the 1 s / 5 s timers must not live only inside the drawn path (WP4-12 shows they already misbehave when the draw is skipped); (3) `DrainCompanyNameQueue` / `ProcessCdQueue` should be time-gated instead of "one per frame". Sleep-until-event would additionally need a wake from the IB reader thread and a timeout wake for the 5 s / 1 s / 0.25 s timers, replay playback and toast animation; it is a larger change and agrees with the user's choice to prefer the capped rate.

How much a cap would save: the per-frame costs above scale with frames, so a cap of 15 fps cuts WP4-01 / 05 / 06 / 07 / 08 by ~4x while idle. WP4-02 and WP4-03 (per tick) do not shrink, since they run in the message handlers. The baseline idle draw cost today is, by this estimate, ~0.5-1 ms/frame for a modest layout (2-3 charts at n~500, DOM, Portfolio) and several ms for a large one (10 charts); on the GPU side, the vertex count and upload per frame for the chart bars (WP4-01) matter at large n. The first step with the best ratio is fixing WP4-01 (draw only the visible bars), which helps at full rate and idle alike.
