# Resource audit plan: memory, CPU / GPU, IB lines, code health

Status: **Phase 1 (read-only audit) approved and started 2026-11-14. No code changes until the findings are reviewed.**
Written 2026-11-14 against 1.6.14 (`86a9b4b`).

## How this plan is meant to be run

Two phases. Phase 1 is read-only and can be handed to smaller models, one work
package (WP) each, in parallel. Phase 2 (fixes) starts only after the user has
read the findings and picked what to fix.

### Rules for every Phase 1 worker

1. **Read-only.** No edits to `src/`, `tests/`, `CMakeLists.txt`, docs. The only
   file a worker writes is its findings file,
   `.claude/plans/audit-findings/WP-<n>.md`.
2. **A finding needs evidence.** Quote the code (`file:line`), and describe the
   concrete sequence of user actions / IB messages that triggers it. "This map
   is never cleared" is not a finding until the worker has read every insert
   site and can say how many entries a day of use adds and how big each is.
3. **Leads are not findings.** Each WP below lists leads from a first survey.
   They are unverified. A worker must close each one as *confirmed*, *refuted
   (why)* or *could not decide (what is missing)*. Refuting a lead is a good
   result.
4. **Do not propose rewrites.** Proposed fixes must be the smallest change that
   removes the problem (CLAUDE.md "Surgical Changes"). Larger ideas go under a
   separate "Ideas" heading with no severity.
5. **Finding format** (one block per finding):

   ```
   ### WP3-07  <one-line title>
   Where:      src/main.cpp:865, 4410 (insert), no erase site
   What:       <2-4 sentences, quoted code>
   Trigger:    <user actions / IB messages>
   Cost:       <bytes or ms or IB lines, per event and per trading day; say how estimated>
   Severity:   leak-unbounded | leak-bounded | crash-risk | waste-cpu | waste-ib | cosmetic
   Confidence: high (read every path) | medium | low
   Fix sketch: <smallest change; which pure helper + test if any>
   ```
6. Build and tests may be run (`cmake --build build`, `ctest --test-dir build`).
   The app itself cannot be exercised past the login screen without the user's
   IB Gateway, so live measurements are the user's part (WP-0).

### Suggested model per package

| WP | Kind of work | Model |
|---|---|---|
| 1, 3, 5, 8 | inventories by grep + reading each site | small (Haiku) |
| 2, 4, 6, 7 | lifetime / threading / per-frame reasoning | mid (Sonnet) |
| triage + Phase 2 design | cross-checking findings, deciding fixes | large |

---

## WP-0  Measurement baseline (user + one worker)

Goal: numbers before any change, so a fix can be shown to help.

- Worker prepares, **outside the repo's build dir**, an ASan/LSan build of the
  whole app without touching CMake files:
  `cmake -B build-asan -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DCMAKE_C_FLAGS="-fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"`
  and a suppression file (scratch dir) for the GPU driver / GLFW / fontconfig
  noise seen at the login screen. Confirms: start, reach login, close, and
  lists what LSan reports with no IB connection.
- Worker writes a **soak script for the user** (a checklist, not code): connect
  to paper; open 3 charts, Order Book, chain, scanner, watchlist; then 20x each
  of: change chart symbol, change style / timeframe, pan left, load a chain,
  switch expiry tabs, run a scan, open and close each window, switch account,
  disconnect and reconnect. Close the app. Send back `app.log` with the LSan
  report.
- Same script once under `heaptrack` (or Windows Task Manager working set
  noted every 10 minutes) for a 1-hour idle session with markets open: is
  memory flat?
- Record idle CPU % and GPU % with markets closed and nothing moving (see WP-4).

Deliverable: `WP-0.md` with the build recipe, suppression list, the checklist,
and (after the user's run) the reports.

## WP-1  Ownership and lifetime of raw pointers

`main.cpp` has ~64 `new` / `delete` sites; every window and `g_IBClient` is a
raw owning pointer (cpp-style.md says to avoid these).

Check for each owner: every exit path deletes exactly once; nothing uses the
pointer after delete; lambdas that captured `idx` or a window pointer can't
fire after the window is gone.

Leads:
- `g_IBClient` is deleted at 6 places (6110, 6148, 6164, 6173, 6219, 8453,
  8528) with a "deferred teardown" flag (4369, 8451). Is there a path where a
  callback still running inside `ProcessMessages()` reaches a deleted client?
- `g_connectThread.detach()` at exit (8519) with the comment at 8517 about a
  use-after-free: what does the detached thread still touch?
- Watchlist close nulls the slot (3252) while other lambdas capture the index.
- `CreateTradingWindows` deletes and re-creates singletons (3480-3736): do any
  globals (pins, `g_analysisPin`, pending rolls, alert pushes) keep state that
  points into the old windows?
- `IBKRClient::~IBKRClient` deletes `m_client` after `Disconnect()`: are both
  threads joined before, on every path including a failed connect?

Deliverable also includes a table "pointer -> could be `std::unique_ptr` with
no behaviour change? yes / no, why", as an *idea*, not a finding.

## WP-2  Threads and locks

Three threads besides the UI: IB reader, IB send loop, connect worker; plus
miniaudio's.

- Map which members each thread touches and under which mutex
  (`m_queueMutex`, `m_sendMutex`, `m_socketMutex`).
- Most `Req*` calls take `m_socketMutex` on the UI thread and write to the
  socket there. What happens to the frame if the socket blocks (Gateway
  frozen, network stall)? Which calls go through the send queue and which
  don't, and is the split deliberate?
- `m_queue` is an unbounded `std::vector<IBMessage>`. It is drained once per
  frame. When the window is minimised (8475) or a modal blocks, does draining
  continue? Worst-case growth during a 300-level depth burst?
- `NotificationService`: every sound started is freed? Queue caps exist
  (`kMaxQueued`, `kMaxHistory`); confirm the miniaudio sound objects follow.

## WP-3  Containers that only grow

Inventory every container with process or connection lifetime and classify it:
bounded by design / cleared on disconnect / cleared on account switch / grows.

First-survey leads in `main.cpp` (insert sites exist, no `erase` / `clear`
found by grep; each needs reading):
`g_whatIfIds`, `g_seenBulletins`, `g_comboFilledAt`, `g_scannerHistFetched`,
`g_scannerFundFetched`, `g_companyNameRequested`, `g_companyNames`,
`g_symbolConIds`, `g_smartComponents`, `g_modifyInFlight` family.
Most are probably tiny; the task is to say so with numbers.

Others:
- `g_liveOrders`: are filled / cancelled orders ever removed? (2 erase sites.)
- `NewsWindow`: market list capped at 100 in one place and 200 in another
  (63, 84); stock and portfolio lists have no cap (75, 92). Article bodies?
- `ChartWindow::m_series`: every pan-left prepends more bars, no upper limit;
  each bar also feeds ~15 parallel vectors. What does 30 minutes of panning an
  M1 chart cost? Same for `ReplayWindow`.
- `PortfolioWindow`: trade history list; equity curve capped at 20000 with
  `erase(begin())` on a vector (370-372).
- `OrdersWindow::m_orders` in memory vs the 500-row file cap and 7-day prune.
- `IBKRClient::LocalOrderId` permId map; chain `m_deadContracts`,
  `m_expiryStrikes`, `m_quotes` across symbol changes.
- `ScannerWindow` sparkline `erase(begin())` per tick (243, 1196).
- Rotating reqId pools (1000 slots, wrap): can a wrapped id collide with a
  subscription that is still alive in a long session?

## WP-4  Per-frame cost and idle load

The loop is `glfwPollEvents()` + full render every vsync (`FIFO`), with no
idle path except when iconified (8475). So the app draws 60+ frames a second
all day whether or not anything changed.

- Measure / estimate what one frame costs with a typical layout, and list work
  done every frame that only needs doing on change:
  `ClassifyStrategies` in `PortfolioWindow::DrawPositions` (768),
  `ComputeVolumeProfile` in `DrawVolumeProfile`, `PushAlertsToCharts`,
  `PushUnguardedHintsToWindows`, `UpdateAllChartPendingOrders`,
  `BuildAnalysisInput` + `ComputeStrategyMetrics` for the analysis window,
  `RecomputeTicketMetrics`, the chain's `SyncSubscriptions`, title strings
  built with `snprintf` / `std::string` per window per frame.
- The once-a-second savers build and hash 7 config texts; `watchlists.cfg`
  and `chart-modes.cfg` included. Cost per second?
- Do closed or docked-but-hidden windows still compute? (`Render()` early
  return placement in each window.)
- What exactly does the iconified branch (8475) do: sleep, skip render, still
  drain the queue?
- *Idea for the user to decide, not a finding:* an idle mode
  (`glfwWaitEventsTimeout` when no input and no new IB messages; redraw on
  tick). Worker lists what would break: toasts timing out, blinking cursors,
  price-alert checks, 5 s open-order refresh, auto-reconnect timer.

## WP-5  IB subscriptions and request budget

IB limits market-data lines (about 100 by default) and paces history requests.
A leaked subscription costs a line for the rest of the session.

Build a table of every `Req*` that opens a stream, with: who opens it, the
reqId range, and where it is cancelled on (a) symbol change, (b) window close,
(c) account switch, (d) disconnect, (e) reconnect.

Leads:
- Chain cancels on close. Do Chart, Order Book (mkt + depth + tick-by-tick),
  Scanner (12 quotes per instance + history per row), Watchlist and News do
  the same when their window is closed with **x**, or only when destroyed?
- `pnlSingle` per position (9001-9999): cancelled when a position goes flat?
  After an account switch?
- Analysis pin feed 21200, alert feeds 23000+, news topic feeds 210-249, the
  futures health feeds 140-143 (subscribed when no chart uses market health?).
- New in 1.6.14: IV history is requested on every chain load, including each
  group broadcast. Any pacing risk when clicking through a watchlist quickly?
- `g_tickerSymbols` entries for rotated ids: erased on every path?
- Count lines used by the user's normal layout; how close to 100?

## WP-6  Vulkan / ImGui resources

- Descriptor pool size vs multi-viewport use; anything allocated per viewport
  or per resize and not returned.
- Shutdown order in `main()` (windows, ImPlot, ImGui, Vulkan, GLFW) and the
  swapchain-rebuild path.
- ImGui ID stack / table settings growth: tables whose ids were bumped
  (`##optchain2`, `##open2`, `##history2`, `##tradeHist2`) leave old entries
  in `imgui.ini`; saved custom presets each hold a full ini text. Size check
  of `imgui.ini` and `window-presets.cfg`.
- Font atlas with the 10-24 px scale: one atlas or one per size?

## WP-7  Files and logging

- `app.log`: unbuffered writes; which lines print per tick or per frame?
  Size after an 8-hour session? Only one previous run is kept.
- `equity-curve-<acct>.csv` rewritten every 15 s (up to 3000 rows);
  `orders-history.cfg`; `price-alerts.cfg` on every change.
- `AtomicWriteText` leftovers (`.tmp`) after a crash.

## WP-8  Code health inventory (no fixes)

- Compiler warnings in our own code on a clean build (known:
  `PortfolioWindow.cpp:505 cardW`, `ScannerWindow.cpp:551 autoLabel`,
  `TradingWindow.cpp:2318 kTypes`, `OptionsChainWindow.cpp:299 msg`,
  `:1315 itmBadgeW`). Full list, each marked dead code / real bug / harmless.
- Dead code: functions and members with no caller (e.g. leftovers of removed
  features: leg-in, alias, tick fills). List only.
- Docs that no longer match the code. Known examples to start from:
  architecture.md still says ticket legs "all sharing one expiry", lists
  `PORT_COL_*` and scanner `COL_*` keys that were removed, describes DOM
  click-to-trade as it was before 1.3.36; `OptionsChainWindow.h` header
  comment has the old reqId layout.
- Logic living in `main.cpp` (8551 lines) with no test, ranked by risk: the
  order-error path (`onError`), bracket children builder, combo-link keep set,
  open-order refresh reconciliation. For each: could it move to a pure helper
  with a test, yes / no. List only.
- Sanitizers cover `tests-core` only. Would `tests-ibkr` run clean under
  ASan with third-party frames suppressed? Try it in a scratch build dir.

---

## Phase 2 (after review)

1. Large model cross-checks every *high* / *crash-risk* finding against the
   code before it reaches the user; duplicates across WPs merged.
2. User picks. Each accepted fix is its own small version, per the repo's
   usual flow: pure helper + test where there is logic, build, 562+ tests,
   task-history entry, live check by the user.
3. Order of work proposed: crash-risk -> unbounded leaks -> IB line leaks ->
   idle load -> the rest. Refactors (smart pointers, splitting `main.cpp`)
   only if the user asks; they are not part of this audit's fixes.

## Decisions (user, 2026-11-14)

1. Findings files under `.claude/plans/audit-findings/`: yes.
2. The user runs the WP-0 soak in two parts: A (any time: windows, symbols,
   history, chains, account switch, reconnect) and B (30-60 min in market
   hours: depth, tape, live bars, news, queue under load).
3. Idle drawing: not decided. WP-4 measures the cost first. If it matters, the
   candidate is a capped rate (about 10-15 fps with no input or no focus, full
   rate on input), not a sleep-until-event mode.
4. Docs that no longer match the code are fixed (docs only) after WP-8 lists
   them; the rest of WP-8 stays a list.
