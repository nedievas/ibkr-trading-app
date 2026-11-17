# WP-1  Ownership and lifetime of raw pointers

Read-only audit of `src/main.cpp` (1.6.14, `86a9b4b`) and `src/core/services/IBKRClient.{h,cpp}`.
Nothing was built or run; all conclusions are from reading every create / delete path.

## Inventory of owning raw pointers

| Owner | Created | Deleted | Paths checked |
|---|---|---|---|
| `g_IBClient` | `LaunchConnectWorker` main.cpp:6111 | 6110 (relaunch), 6148 (Esc-cancel), 6164 (failed reconnect), 6173 (failed first connect), 6219 (`Disconnect()`), 8453 (deferred, lost connection), 8528 (exit) | see WP1-01, refutations R1-R3 |
| `g_chartEntries[].win` | `SpawnChartWindow` :1821 | `DestroyTradingWindows` :3940 only | closing a chart only hides it (`open()=false`) |
| `g_tradingEntries[].win` | :1980 | :3941 | same |
| `g_scannerEntries[].win` | :2040 | :3942 | same |
| `g_newsEntries[].win` | :2089 | :3953 | same |
| `g_replayEntries[].win` | :3364 | :3958 | same |
| `g_watchlistEntries[].win` | :3179 | :3252 (`PruneClosedWatchlists`, user closed it), :3955 | slot nulled, never erased; every reader checks `.win` |
| `g_PortfolioWindow`, `g_OrdersWindow`, `g_OptionsChainWindow`, `g_StrategyAnalysisWindow`, `g_WshCalendarWindow` | `CreateTradingWindows` :3480-3736 | `DestroyTradingWindows` :3960-3965 | `Create` is only reached right after `Destroy` (:4008-4009), so the `delete x; x = new` at 3480/3524-3527/3736 always deletes nullptr |
| `g_NotificationsWindow` | main() :8426 | :8536 | independent of connection; deleted after `DestroyTradingWindows`, before `g_NotificationService.reset()` |
| `IBKRClient::m_client` (`EClientSocket*`) | ctor :49 | dtor :54 after `Disconnect()` | see R5 |

No other `new` / `delete` in main.cpp. IBKRClient.cpp's other `new` are `shared_ptr` (`ComboLegSPtr`).

## Findings

### WP1-01  File -> Disconnect during an in-flight auto-reconnect deletes the client under the connect worker
Where:      src/main.cpp:7479 (menu item), 6210-6219 (`Disconnect`), 6125-6128 (worker), 6108 and 6138 (the only joins), core/services/IBKRClient.cpp:54, 62-64
What:       The File menu is drawn for `Connected` *and* `LostConnection` (:7966) and `if (ImGui::MenuItem("Disconnect")) Disconnect();` has no guard on `g_connectInFlight`. In `LostConnection` `g_IBClient` is non-null exactly while a silent reconnect attempt runs (`StartSilentReconnect` -> `LaunchConnectWorker(true)`), and that worker is sitting in `client->Connect(host, port, cid)` = `eConnect()`, which blocks through TCP connect + API handshake (the reason the worker thread exists, see comment :258-263). `Disconnect()` does:
            g_IBClient->Disconnect();
            delete g_IBClient;          // ~IBKRClient -> delete m_client
            g_IBClient = nullptr;
            ...
            g_Login.state = Disconnected
            DestroyTradingWindows();
            It never calls `AbortConnect()`, never joins `g_connectThread`, never clears `g_connectInFlight`. The worker lambda captured the raw `client` pointer (:6124), so it keeps calling `m_client->eConnect(...)` / assigning `m_reader`, `m_readerThread`, `m_sendThread` on freed memory (use after free), and if it was blocked on the socket `~EClientSocket` is destroyed under a blocked `recv`. The cancel path (:6140-6150) and the exit path (:8517-8522) both handle this correctly; this path does not.
Trigger:    Connect to Gateway, drop the connection (kill Gateway / VPN down so the host is unreachable and `connect()` blocks for the TCP timeout). State becomes `LostConnection`, a reconnect attempt starts every 5 s and may block for tens of seconds. While it blocks, File -> Disconnect.
            Follow-ups in the same sequence: `PollConnectState` later sees `g_connectResult != 0`, joins the worker (OK) and for `r==2` runs `delete g_IBClient` on nullptr (harmless); but a user clicking Connect before the worker returns freezes the UI in the `g_connectThread.join()` at :6108.
Cost:       Crash (heap corruption / segfault) or a UI hang; only when a reconnect attempt is blocked. Not measurable per day; one event per such incident.
Severity:   crash-risk
Confidence: medium (code path read end to end; whether `eConnect` blocks long enough depends on the network, not tested live)
Fix sketch: In `Disconnect()` when `g_connectInFlight`: call the same sequence as `CancelConnect()` + `PollConnectState`'s cancel branch (set `g_connectCancelled`, `g_IBClient->AbortConnect()`, join `g_connectThread`, then `Disconnect()` + delete). Smallest: at the top of `Disconnect()`: `if (g_connectInFlight) { g_IBClient->AbortConnect(); if (g_connectThread.joinable()) g_connectThread.join(); g_connectInFlight = false; g_connectCancelled = false; }`. No pure helper needed. Alternative: grey out the menu item while `g_connectInFlight`.

### WP1-02  Pending style switch survives `DestroyTradingWindows` and is applied to the next session's chart with the same index
Where:      src/main.cpp:1096 (`g_pendingStyleSwitches`), 1878-1883 (push), 1665-1699 (drain), 3909-3965 (`DestroyTradingWindows`, no clear)
What:       `g_pendingStyleSwitches` holds `{chartIdx, style, historyDuration, useRTH}`. `DrainStyleSwitchQueue` only checks `chartIdx < g_chartEntries.size()` and `ce.win`, then re-issues `ReqChartData(ce, sym, tf, p.useRTH, ..., duration)`. `DestroyTradingWindows` empties `g_chartEntries` but never touches the queue, and the drain returns early while `g_IBClient` is null, so an entry queued just before File -> Disconnect (or before `FinishConnect(false)` -> Destroy/Create) stays until the next connect.
Trigger:    Change the style combo of chart 0 (entry queued, drain happens up to 1 s later), click File -> Disconnect inside that second (or have any other entry still waiting behind the 1 s throttle with several charts), reconnect. New chart 0 exists, its own style was just restored from chart-modes.cfg with `ReqChartData` already issued; the stale entry now fires a second history request for chart 0 with the OLD preset's `historyDuration` / `useRTH` (e.g. "2 D" for a D1 chart restored as Swing).
Cost:       1 extra historical request (+ cancel of the one just issued) per stale entry, and the chart briefly shows the wrong horizon (wrong duration for the timeframe until the next change). Rare: needs a click within about a second of Disconnect. Estimated from the code, not measured.
Severity:   waste-ib
Confidence: high (every path read; the trigger window is narrow)
Fix sketch: `g_pendingStyleSwitches.clear();` in `DestroyTradingWindows()`.

### WP1-03  Reconnect does not redo the P&L subscriptions; `g_pnlSubscribed` / `g_pnlSingleConIds` stay set
Where:      src/main.cpp:4841-4843 (`!g_pnlSubscribed` gate, ReqPnL 9000), 5017-5023 (`g_pnlSingleConIds.find(conId) == end` gate, ReqPnLSingle), reset only at 6214-6229 (Disconnect) and 7708-7718 (account switch); reconnect branch of `FinishConnect` 4222-4290 has neither reset
What:       After a lost connection the old `IBKRClient` (and its IB session) is deleted and a new one connected (`isReconnect == true` skips Destroy/Create). The reconnect branch re-requests account updates and positions, but `g_pnlSubscribed` is still true and every conId is still in `g_pnlSingleConIds`, so `onAccountValue` never calls `ReqPnL(9000)` again and `onPositionData` / `onPortfolioUpdate` never call `ReqPnLSingle` again. The new session has no P&L subscriptions at all.
Trigger:    Any unexpected disconnect + successful auto-reconnect (Gateway nightly restart, network blip).
Cost:       Since 1.5.69 `OnPnLSingle` is also what refreshes each Portfolio row's price / market value / unrealized P&L every second, so after the first reconnect those fields only change on `updatePortfolio` (minutes), and the account P&L header and Day P&L stop. Silent until the user switches account or disconnects. No leak.
Severity:   cosmetic (stale live data; no memory or IB-line cost)
Confidence: high (grep shows no other writer of the two variables)
Fix sketch: In the reconnect branch of `FinishConnect`: `g_pnlSubscribed = false; g_pnlSingleConIds.clear(); g_pnlReqIdToSymbol.clear(); g_pnlReqIdToConId.clear(); g_pnlSingleNextReqId = 9001;` (same five lines as the account-switch path; the old client is gone so there is nothing to cancel).

### WP1-04  Reconnect does not re-request several other streams whose owners think they are subscribed
Where:      src/main.cpp:4222-4290 (reconnect branch re-subscribes charts, order books, news seeds, futures feeds, TWS groups only)
What:       Not re-requested after a reconnect: (a) pinned analysis underlying 21200 (`PinAnalysis` is the only requester, :645-647; `g_analysisPin.active` stays true, `spot()` freezes); (b) Options Chain quotes (the window's `m_quotes` / `SyncSubscriptions` diff sees no change); (c) Watchlist items (their `subscribed` flag stays true); (d) Replay history is one-shot so unaffected. (e) Price-alert feeds are handled (`RestartAlertFeeds` at the end of `FinishConnect`).
Trigger:    Reconnect with a chain loaded / a watchlist open / an Analysis pin active.
Cost:       Dead quotes in those windows until the user changes symbol or reloads. No leak. (b) and (c) are only read from main.cpp side, not from inside the windows' own sync code; that part belongs to WP-5.
Severity:   cosmetic
Confidence: low-medium (main.cpp side verified; window-internal behaviour not read)
Fix sketch: Out of scope here; WP-5 should confirm (b)/(c). (a) can be fixed by re-issuing the 21200 request from the reconnect branch when `g_analysisPin.active`.

### WP1-05  `g_clientDeletePending` branch raises the "Disconnected" toast once per queued `MsgConnection(false)`
Where:      core/services/IBKRClient.cpp:950-953 (`connectionClosed` pushes), 975-979 (`error` 1100/1300/5xx pushes again); main.cpp:4360-4380
What:       A dropped socket usually yields `connectionClosed()` and an error 1100/507 as well, i.e. two `MsgConnection{false}`; both are dispatched in one `ProcessMessages` (client still alive, deletion is deferred), and each runs the `else` branch: two `ConnectionLost` toasts and two "[IB] Disconnected" lines. No lifetime problem (flag is idempotent, `delete` happens once).
Trigger:    Gateway killed.
Cost:       One extra toast + notification-history row per drop.
Severity:   cosmetic
Confidence: medium (the double push is certain from the code; whether IB really emits both depends on the failure)
Fix sketch: In the handler, `if (g_clientDeletePending) return;` at the top of the `connected == false` branch.

## Leads closed

R1  `g_IBClient` deleted at 6 places; can a callback inside `ProcessMessages()` reach a deleted client? **Refuted.** The only delete reachable from a dispatch is the lost-connection one, and it is deferred (`g_clientDeletePending`, :4369-4373 -> :8451-8456, after `ProcessMessages` returned). The other deletes (`LaunchConnectWorker`, `PollConnectState`, `Disconnect`, exit) are called only from the main loop top, login UI, menu or `main()`, never from a message callback. Within one dispatch batch remaining messages still see a live client. `Disconnect()` also clears `g_clientDeletePending` (:6221) and `StartSilentReconnect` clears it after relaunch, so no double delete of a new client. `delete nullptr` makes the remaining overlaps harmless. (The separate hazard, deleting under the connect worker, is WP1-01.)
R2  `g_connectThread.detach()` at exit (:8519): **Refuted as a crash risk** (residual noted). The orphaned worker only touches its by-value copies (`client`, `host`, `port`, `cid`) and `g_connectResult` (`std::atomic<int>`, trivially destructible). The client is deliberately leaked, so its members, `m_reader`, and the threads it may still start are never destroyed; reader callbacks in `IBKRClient.cpp` only `Push()` into the client's own queue (no globals, grep'd). `g_connectThread` itself is non-joinable after detach, so its static destructor is fine. Residual: if the worker finishes `Connect()` after the window closes, two leaked threads run until `exit()`; harmless at process exit.
R3  Watchlist close nulls the slot (:3252) while other lambdas capture the index: **Refuted.** The deleting code is `PruneClosedWatchlists`, called after the render loops (:7890), never from inside a watchlist's own callback or `Render`. A deleted window's `std::function`s die with it, so no lambda can fire afterwards. Lambdas that capture `idx` and look at `g_watchlistEntries` re-check bounds and `.win` (:3209); the rest use reqIds only. Every loop over `g_watchlistEntries` (2178, 3267, 3901, 4569, 4694, 5607, 7566, 7874) checks `.win`. Slot reuse (:3230, `NextWatchlistSlot`) keeps indices stable. Residual for WP-5: a reused slot reuses the same reqId range, so late ticks from the closed instance's cancelled subscriptions can reach the new instance for a few ms.
R4  `CreateTradingWindows` deletes and re-creates singletons: do globals keep state pointing into the old windows? **Refuted for pointers, one real leftover.** `CreateTradingWindows` is only called straight after `DestroyTradingWindows` (:4008-4009), so the deletes at 3480/3524/3525/3526/3736 act on nullptr. `Destroy` calls `UnpinAnalysis()` first, so `g_analysisPin` is reset; no global holds a window pointer (all access is through the `g_*Window` globals, which every handler null-checks; verified by grep for unguarded `g_...Window->` uses, all are inside the window's own callbacks or follow an `if`). `g_pendingRoll` does not exist as a global (`m_pendingRoll` is a window member and dies with the window). Alert pushes go through `g_priceAlerts` (no window refs). `g_pendingStyleSwitches` does keep an index into the old chart vector: WP1-02. `g_pendingBracketStops` / `g_pendingLocalAccept` / `g_modifyInFlight` hold order ids not window refs (growth belongs to WP-3).
R5  `~IBKRClient` deletes `m_client` after `Disconnect()`: both threads joined on every path? **Refuted (all paths join), with one caveat.** `Disconnect()` joins the send thread, then the reader thread, then resets `m_reader` (which joins EReader's own thread), and only then does the dtor `delete m_client`. Failed `Connect()` (`eConnect` false): nothing was started (`m_reader` null, threads not joinable), so the dtor path is trivially safe. Esc-cancel: `AbortConnect` + join of the worker + `Disconnect()` + delete (:6139-6150). Lost connection: `connectionClosed` sets `m_running=false`, the reader thread loop exits by itself, `~IBKRClient` -> `Disconnect()` joins it. Caveat: the join order is only correct if no *other* thread is inside `Connect()`; that is WP1-01.
R6  Lambdas capturing `idx` into vectors (`g_chartEntries[idx]`, `g_tradingEntries[idx]`, `g_scannerEntries[idx]`, `g_newsEntries[idx]`, `g_replayEntries[idx]`) after the vector shrinks: **Refuted.** These vectors are only ever cleared in `DestroyTradingWindows`, which also deletes every window (and with it its callbacks); entries are otherwise never erased, and `Spawn*(idx)` is always called with `idx == size()` (menu, loaders' pre-pass, `ApplyPresetEntries`). No callback fires before its `push_back` (checked each `Spawn*`; `AddDefaultsIfEmpty` for the watchlist runs before `push_back` but only touches reqId-based lambdas).
R7  Iterator invalidation of an entries vector while a window's `Render()` spawns a window of the same kind: **Refuted.** The only in-render spawn is the Watchlist's `OnOpenChart` / `OnOpenOrderBook` (:3212-3226), which push to `g_chartEntries` / `g_tradingEntries` after those render loops already ran in the frame (:7871-7872) and never to the watchlist vector being iterated. Menu / preset spawns run before the render loops.
R8  `Disconnect()` / `Destroy` mid-frame from the menu while later code in `RenderTradingUI` still runs: **Refuted.** Everything after the menu checks the globals (`g_PortfolioWindow` etc. at 7486-7620, 7872-7915) or iterates now-empty vectors; windows are not deleted while their own `Render` is on the stack.
R9  Window dtors calling IB after the client is gone: **Refuted.** Only `~WshCalendarWindow` does work (`CancelAll`), through `OnCancelWshEvents`, which checks `g_IBClient`; `Disconnect()` deletes the client first, then `DestroyTradingWindows`, and the callback is null-safe.
R10 Window pointers used after delete from `ProcessMessages` when `FinishConnect(false)` runs `Destroy` + `Create` inside a message callback (:4008): **Refuted.** Later messages in the same batch address windows through the globals / vectors that were just rebuilt, all null-checked; no reference to a deleted window is held across the call (callers `onManagedAccounts` / `onConnectionChanged` return right after).
Could not decide:
C1  `ProcessMessages()` runs on a client while its connect worker may still be finishing `Connect()` (assigning `m_readerThread` / `m_sendThread`) after `eConnect` returned: the reader thread only starts inside `Connect()`, so messages can reach the UI before `g_connectResult` is set, and `FinishConnect` then calls `Req*` / `PostSend` concurrently with the worker's last assignments. Only the send-thread assignment is unsynchronised and the window is microseconds; I could not construct a crash from it. Needs WP-2's thread map.

## Ideas (no severity)

Pointer -> could it be `std::unique_ptr` with no behaviour change?

| Pointer | unique_ptr, no behaviour change? | Why |
|---|---|---|
| `ChartEntry/TradingEntry/ScannerEntry/NewsEntry/ReplayEntry::win` | yes | one owner, deleted only in `DestroyTradingWindows`; entries are moved into vectors (move-only is fine). `delete e.win; e.win = nullptr` becomes `e.win.reset()`. Raw `ce.win` reads (`ce.win->...`) work unchanged; `UpdateChartPendingOrders(ce.win)` etc. would need `.get()`. |
| `WatchlistEntry::win` | yes | same; the slot-null protocol is already `.win` null checks. `delete we.win` in the prune loop becomes `reset()`. |
| `g_PortfolioWindow`, `g_OrdersWindow`, `g_OptionsChainWindow`, `g_StrategyAnalysisWindow`, `g_WshCalendarWindow` | yes | `x.reset(new ...)` replaces `delete x; x = new` (deletes nullptr today anyway). Dozens of `if (g_X)` still compile. Note `~WshCalendarWindow` calls back into main.cpp, so keep `reset()` before the globals it uses are torn down (already true). |
| `g_NotificationsWindow` | yes | main()-scoped, one delete at exit. |
| `g_IBClient` | **no** (as is) | the connect worker, the deferred-delete flag and the exit path deliberately separate "who deletes" from "who still holds a pointer"; the worker captures a raw copy and the exit path intentionally leaks it. A `unique_ptr` would delete at the wrong moment (WP1-01 shows the lifetime is not simply scoped) unless a `shared_ptr` is used for the worker copy. |
| `IBKRClient::m_client` (`EClientSocket*`) | yes | one owner, deleted in the dtor after `Disconnect()`; `EClientSocket` has a public virtual dtor. Member order matters: it must be destroyed before `m_signal` and after `m_reader` (already reset in `Disconnect`). |
