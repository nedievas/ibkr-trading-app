# WP-3 findings: containers that only grow

Read-only pass, 2026-11-14, against `86a9b4b` (1.6.14). Written by the WP-3 worker. No source files were edited.

Method: grep for every file-scope `static` container in `src/main.cpp`, every
container member in the windows named by the plan, and `IBKRClient.h`; then read
the insert, erase and clear sites. Line numbers are from the tree at audit time.
Byte sizes are estimates from the struct definitions (std::string SSO 32 B on
x64, unordered_map node about 32 B overhead). Daily counts are estimates from
the code paths, not measured; WP-0 soak numbers should replace them.

## 1. Summary table

Column "max / cleared": what bounds the container and when it is emptied.

### 1a. `src/main.cpp` file-scope containers

| Name | Decl | Max size / bound | Cleared when |
|---|---|---|---|
| g_chartEntries, g_tradingEntries, g_scannerEntries, g_newsEntries, g_watchlistEntries, g_replayEntries | 162-167 | kMaxMultiWin = 10 each | DestroyTradingWindows (3938-3944) |
| g_activePreset | 172 | string | never (persisted) |
| g_pendingLocalAccept | 198 | bounded by in-flight orders; erased on terminal status (5122-5128) and on accept (487) | erase only |
| g_scannerPrevClose | 201 | distinct scanned symbols (~hundreds/session) | DestroyTradingWindows 3972, 6237 |
| g_scannerVolume | 203 | same | 3973, 6238 |
| g_scannerHistSym / g_scannerHistBars | 209-210 | keyed by reqId 18000+25*idx: <=250 keys; erased on done (4518-4519) | on done; stale entries overwritten on reuse |
| g_scannerHistFetched | 211 | distinct symbols scanned, 80-100 B each | **never** |
| g_scannerFundSym | 218 | reqId keyed, erased 4623, 5428 | 5841 |
| g_scannerFundFetched | 219 | distinct symbols, ~80 B each | **never** |
| g_tickerSymbols | 253 | live subs + fixed slots; erased on every rotation (1537, 1652) and cleanup (3938-3979) | yes |
| g_pendingBracketStops | 256 | one per bracket whose entry is live; erased on cancel/reject/fill (5130, 5295) | not cleared on disconnect (tiny) |
| g_groups | 307 | kNumGroups = 10 (array) | n/a |
| g_liveOrders | 313 | **every order in the session**; finished orders are never removed; erased only for external orders dropped by refresh (5078) | DestroyTradingWindows 3968 |
| g_positions | 316 | held symbols; erased when flat (4960, 5001 path) | 3969, 7693 (account switch) |
| g_symbolCommissions | 325 | distinct symbols traded | 3971 |
| g_optionPositions | 330 | held option contracts | 3970, 7696 |
| g_whatIfIds | 335 | one per margin check; never erased | **never** (~40 B each) |
| g_smartComponents | 353 | bboExchange values (<~20) | never |
| g_symbolConIds | 357 | distinct chart symbols, ~50 B | **never** |
| g_accountNames | 367 | managed accounts | Settings edit |
| g_pnlSingleConIds | 398 | **one per conId ever held in the connection**; never erased on flat | Disconnect (6229), account switch (7718) |
| g_pnlReqIdToSymbol / g_pnlReqIdToConId | 400-401 | same as above, mirrors | same |
| g_unguarded / g_lastUnguardedSymbols | 542, 558 | positions with no stop (small) | rebuilt each recompute (887, 939) |
| g_priceAlerts | 658 | user-created | on delete |
| g_alertFeedIds / g_alertFeedSyms / g_alertLast | 660-662 | alerted symbols; SyncAlertFeeds removes (697-699) | 715-717, 3873-3875 |
| g_comboFilledAt | 865 | one per BAG order that reached Filled; never erased | **never** (~50 B each) |
| g_pendingStyleSwitches | 1096 | one per chart (latest wins, 1880) | drained at 1/s |
| g_portfolioSymbols | 1108 | every symbol held in the session (never pruned on flat) | 3967 only |
| g_lastReplayCursorBroadcast | 1604 | gid keys, <=10 | never (tiny) |
| g_modifyInFlight | 1710 | erased when IB answers (1731, 5929, 5940); stuck if IB never answers | answer |
| g_refreshSeenExternal / g_modifiedDuringRefresh | 1719, 1722 | cleared at each refresh start (1743-1744, 5073) | each re-read |
| g_newsTopicFeeds | 2329 | providers (~10) | 3897, 4293 |
| g_companyNames | 3322 | distinct symbols, ~80 B | **never** |
| g_companyNameRequested | 3323 | distinct symbols | **never** |
| g_companyNameQueue | 3324 | drained one per frame (3350) | n/a |
| g_companyNameReqToSym | 3325 | ring of 1000 reqIds (20000-20999); erased on first reply (5592) | overwritten on reuse |
| g_seenBulletins | 3865 | every bulletin in the process (dedupe across reconnect, intentional) | **never** (~100 B each) |
| g_customPresets | 6615 | user-created | user |
| function-local s_warned (1771), s_comboFillToasted (5160), s_nameBufs (7349) | | bounded by ids / names | never |

### 1b. Window members

| Container | File:line | Max / bound | Cleared when |
|---|---|---|---|
| ChartWindow m_series.bars + 24 parallel vectors (m_xs, m_idxs, m_opens..m_volumes, sma/ema/bb/rsi/vwap/atr/donch/kelt) | ChartWindow.h:352-387; insert ChartWindow.cpp:410, 493-496, 524-557, 617-620; prepend 448-497 | **unbounded** within one symbol/TF; about 240 B per bar | RequestNewData (739-745), symbol/TF change |
| ChartWindow m_wshEvents | ChartWindow.h (vector); ChartWindow.cpp:700 | WSH events of one symbol | ClearWshEvents on symbol change (ChartWindow.h:212, main.cpp:1842) |
| ChartWindow m_drawings, m_pendingOrders, m_alerts, m_dismissedUnguarded | ChartWindow.h:414-432 | user-driven | symbol change / per frame copy |
| NewsWindow m_marketNews | NewsWindow.h:86; cap 100 (NewsWindow.cpp:63), 200 (84) | 200 | replaced |
| NewsWindow m_stockNews / m_portfolioNews | :87-88; push 75, 92 | per IB reply; cleared on refresh (550, 533, 163) | refresh |
| NewsWindow m_itemProviders / m_itemArticleIds | :101-102; insert 56-57, 68-69 | **unbounded per session**, ~150-200 B per headline, never erased | window destroyed (disconnect) |
| ReplayWindow bars (m_xs..m_vwapSd2Dn) | ReplayWindow.h:166-180 | one day (replaced on SetDay, 93-114) | SetDay |
| ReplayWindow m_userFills / m_simFills | :191, :194; push 2015 | one day; m_simFills cleared 159, 438, 521 | SetDay / reset |
| PortfolioWindow m_trades | PortfolioWindow.h:154; insert at begin 288 | **unbounded per session**, ~200 B per fill | window destroyed only (ResetAccountData keeps it, 319-321) |
| PortfolioWindow m_equityCurve | :155; push 370, cap 20000 with erase(begin) 371-372; consolidated to one point/day on save (431-446) | in practice <= ~1440 points intraday + one per past day | LoadEquityCurve (386-394) |
| PortfolioWindow m_companyNames | :153; insert 215 | distinct symbols | window destroyed |
| PortfolioWindow m_ungroupedSets / m_comboLinks / m_manualMerges | :178, :182, :186 | user / combo submits; pruned in serialized text only (80-88), in-memory copy not pruned | window destroyed |
| OrdersWindow m_orders | OrdersWindow.h:133; insert 146, 156, 221; erase 191, 201 | terminal orders for 7 days (PruneOldHistory once a minute, 194-206, 260) plus live orders | PruneOldHistory / RemoveOrder |
| OrdersWindow m_queriedFills | :136; push 242 | one per queried execution, dedupe by execId | "Clear" button (534) |
| OrdersWindow m_comboLegMeta / m_comboLegAsked | :205-206; insert 887, 915 | distinct BAG leg conIds, never erased | window destroyed |
| OrdersWindow m_savedComboLabel / m_fromHistory | :207-208 | rows | prune / RemoveOrder |
| OptionsChainWindow m_quotes | OptionsChainWindow.h:319; push_back 698; entries with subscribed=false kept (686-691) | **grows within one expiry** (all strikes scrolled to), <=2 x strikes | CancelAll: expiry switch (593-598), symbol change (125), window close (3169) |
| OptionsChainWindow m_reqIdToQuote | :320 | live subs (erased on cancel) | CancelAll |
| OptionsChainWindow m_deadContracts | :330; insert 308 | contracts IB rejected (code 200) | chain load (778) |
| OptionsChainWindow m_expiryStrikes / m_expiryClass | :269, :276 | expirations | chain load (779-780) |
| OptionsChainWindow m_positions | :245 | held option legs | full replace (267) |
| OptionsChainWindow m_ivHistory | :308 | 252 daily values | chain load (136, 807, 840) |
| OptionsChainWindow m_legs, m_pendingRoll, m_pendingChildren | :359, :360, :372 | user cart | user |
| ScannerWindow m_results | ScannerWindow.h:143; replaced per scan (116); remove_if (1094-1117); cleared 1067 | one scan (IB caps ~50) | each scan |
| ScannerWindow sparkline per row | ScannerWindow.cpp:1192-1196 (erase begin) | 40 floats | row replaced |
| ScannerWindow m_companyNames / m_techCache / m_fundCache | :144, :151, :155; insert 164, 175, 203 | distinct symbols, ~250 B each for techCache | **never** while window open |
| TradingWindow m_ticks | TradingWindow.h:273; push_front 324, 338; pop_back at kMaxTicks | 2000 (TradingWindow.h:272) | DOM symbol change (374) |
| TradingWindow m_fills | :268; insert 244 | 200 (resize 245) | symbol change |
| TradingWindow m_openOrders | :246; push 230, 648, 2242 | 30 terminal + live (PruneFinishedOrders 2571-2586, called 512) | ClearOpenOrders |
| TradingWindow m_domOrders | :211; push 634; expired 519 | 3 s after fill / cancelled | timer |
| TradingWindow m_asks / m_bids / buckets / m_volAtPrice | :167-182 | depth rows <=300 per side; buckets per exchange | symbol change (364-377, 747-750, 1549-1553) |
| WatchlistWindow m_watchlists / m_cdQueue / s_presets | WatchlistWindow.h:112, :125, :156 | user (100 symbols per tab) | user / preset switch (229) |
| WshCalendarWindow m_events | WshCalendarWindow.h:78; push 136 | **unbounded per window lifetime**, dedupe by conId+type+date; ~300 B each; only events of subscribed conIds | window destroyed (disconnect) |
| WshCalendarWindow m_subs / m_pendingSubs | :67, :73 | subscribed conIds (slot-limited to 130) | CancelAll (107) |
| NotificationsWindow / NotificationService m_history, m_queue, m_pendingPlays | NotificationService.h:180-182 | history 500 (cap at cpp:237), queue 64 (242); m_pendingPlays drained per tick (301-303) | ring |

### 1c. `src/core/services/IBKRClient.h` (per connection; the object is deleted on disconnect)

| Container | File:line | Max / bound | Cleared when |
|---|---|---|---|
| m_queue (IBMessage) | IBKRClient.h:540 | WP-2 territory | per frame |
| m_sendQueue | :562 | WP-2 territory | drained by send thread |
| m_pendingFills (execId keyed) | :572 | one per execution until its commission report (erased 1245) | commission arrives |
| m_externalOrderIds (permId -> local id) | :579; insert 662 | one per outside (TWS) order in the connection, ~50 B | IBKRClient destroyed |
| m_lastOpenOrderLog / m_lastOrderStatusLog | :584-585; writes 1428, 1200 | one string per order id in the connection, ~150 B | IBKRClient destroyed |
| m_lastTickPrice | :588; write 1615 | keyed by tick-by-tick reqId in pool 16000-16999 => <=1000 keys | IBKRClient destroyed |

## 2. Lead-by-lead verdicts (plan WP-3 section)

- `g_whatIfIds` (main.cpp:335, insert 3557): **confirmed grows, trivially**. One entry per "Check margin" click, never erased. Refuted as a problem: under 1 KB per day of normal use.
- `g_seenBulletins` (3865, insert 5355): **confirmed grows**, never cleared, intentionally kept across reconnects (dedupe). Bulletins are a few per day. Refuted as a problem.
- `g_comboFilledAt` (865, emplace 5107): **confirmed grows**, one per BAG fill, never erased. About 50 B per combo; refuted as a problem.
- `g_scannerHistFetched` (211, 5484) and `g_scannerFundFetched` (219, 5510): **confirmed grows** per distinct scanned symbol, never cleared. About 80 B per symbol; a session scans a few hundred symbols. Refuted as a problem, but note it is unbounded in principle.
- `g_scannerFundSym` (218): **refuted** as a leak; erased on reply (4623, 5428) and cleared (5841).
- `g_companyNames` / `g_companyNameRequested` (3322-3323): **confirmed grows** per symbol, never cleared, about 80-150 B each. Refuted as a problem. `g_companyNameReqToSym` (3325) is **refuted**: a ring of 1000 ids, so it is bounded (its entries are overwritten on reuse).
- `g_symbolConIds` (357, 5604): **confirmed grows** per chart symbol, never cleared, about 50 B. Refuted as a problem.
- `g_smartComponents` (353, 5377): **refuted**: keyed by bboExchange, under 20 keys.
- `g_modifyInFlight` family (1710, 1719, 1722): **refuted** as unbounded. Erased on answer (1731, 5929, 5940). Stuck entries are possible only if IB never answers an order change; each is an int. Cleared per re-read (1743-1744, 5073).
- `g_liveOrders` (313): **confirmed grows for the whole connection**. Insert sites 1916, 3464, 3516, 3578, 3801, 5047, 5266, 5288. Erase sites: only external orders dropped at openOrderEnd (5075-5078). Finished orders stay. Cost about 500-600 B per order (Order has about 15 strings plus a ContractSpec with a vector); 50-200 orders per day gives about 30-120 KB per day. Also a per-event O(n) cost: `UpdateAllChartPendingOrders` (872-908), `RecomputeUnguardedPositions`, `PushWorkingComboLegs` iterate the whole map. See WP3-03 / WP3-04.
- `NewsWindow` caps: **confirmed**: `m_marketNews` capped at 100 (NewsWindow.cpp:63) and 200 (84). `m_stockNews` cleared in RefreshStock (550); `m_portfolioNews` cleared in refresh (533, 163). Article bodies are not stored (`clean.summary = ""`, lines 54, 74). The real growth is the two id maps `m_itemProviders` / `m_itemArticleIds` (WP3-06). Confidence medium: I read every insert site of these maps but did not read the whole NewsWindow.cpp body.
- `ChartWindow::m_series`: **confirmed**. See WP3-03.
- `PortfolioWindow` trade history: **confirmed** unbounded per session (WP3-05 covers the duplicate insert). Equity curve: **refuted** as a cost. The in-memory series is consolidated to one point per past day on every save (431-446), intraday density is 1/min, and the 20000 cap (371) is never reached in practice. `erase(begin())` runs only at the cap.
- `OrdersWindow::m_orders`: **refuted as unbounded**. PruneOldHistory runs once a minute (OrdersWindow.cpp:256-261) and drops terminal orders older than 7 days. In memory it holds up to 7 days of orders (about 1-3 MB at the high end), not the 500-row file cap. Note: the file cap (500) is smaller than the in-memory set; the file just keeps the newest 500.
- `IBKRClient::LocalOrderId` map (IBKRClient.cpp:660-665): **refuted as a problem**. One entry per outside order per connection.
- `OptionsChainWindow` `m_quotes`, `m_deadContracts`, `m_expiryStrikes`: `m_quotes` **confirmed grows within an expiry** (WP3-01). `m_deadContracts`, `m_expiryStrikes`, `m_expiryClass` are cleared on chain load (778-780). `m_positions` is replaced on each snapshot (267).
- `ScannerWindow` sparklines: **refuted**. Fixed 40-sample ring, erase-begin on 40 floats is trivial. Live drift simulation (ScannerWindow.cpp:447) runs only without real data (`!m_hasRealData`).
- `ScannerWindow` `erase(begin())` at 1192-1196: refuted as a cost (see above).
- `g_liveOrders` erase sites: confirmed two (5078 for external; 3968 for destroy). See above.
- `g_pnlSingleConIds` (not in the plan's list, found while reading): **confirmed**, see WP3-02.
- `g_tickerSymbols` (not in plan): **refuted** as a leak. Every rotation erases the old key (1537, 1652), and cleanup at 3938-3979 covers the entries. Fixed-range scanner ids are erased in the loop at 3979. Remaining risk is the wrap collision in WP3-07.
- `g_portfolioSymbols` (1108): **confirmed**, never pruned on flat, only cleared at destroy (3967). Tiny (under 10 KB per session). Refuted as a problem.
- `g_pendingBracketStops` (256): erased on cancel, reject and fill; not cleared on disconnect (tiny). Refuted as a problem.

## 3. Findings

### WP3-01  Options chain: quote map grows per expiry and is scanned linearly on every option tick
Where:      src/ui/windows/OptionsChainWindow.cpp:417-421 (FindQuote, linear), 453 and 512 (RecomputeExpectedMove on every option tick), 521-575 (loop over all m_activeStrikes, two FindQuote per strike at 548-562), 686-691 (cancelled quotes stay in m_quotes), 698 (push_back)
What:       `FindQuote` is `for (auto& q : m_quotes) if (KeyEqual(q.key, k)) return &q;`. `OnOptionComputation` (line 512) and `OnOptionTick` (line 453) both call `RecomputeExpectedMove()`, which builds the strike rows with `FindQuote(k)` twice per strike across the whole `m_activeStrikes` ladder. Quotes are never removed when they leave the subscription (`q->subscribed = false`, line 690), so `m_quotes` keeps every contract the user has scrolled through in this expiry. It is cleared only at `CancelAll()` (717-723), i.e. expiry switch, symbol change or window close.
Trigger:    Open a chain, scroll the strike ladder up and down for a while, and keep the window open. Every bid/ask/greeks tick on any subscribed option then costs S x 2 x Q key comparisons (S = strikes in the expiry's active ladder, Q = contracts ever subscribed in this expiry). Also `FindQuote` is called per visible cell in the table (1613, 1967, ...) every frame.
Cost:       CPU, not memory. Example: S = 200, Q = 400 gives 160,000 KeyEqual calls per option tick. With ~60 live subscriptions firing 50-150 ticks/s, that is 8-24 M comparisons per second on the UI thread. Memory: about 200 B per OptionQuote, so Q = 1600 (an SPX expiry fully scrolled) is about 300 KB. Estimate only; WP-4 should measure the frame time with the chain open.
Severity:   waste-cpu
Confidence: medium (read the call sites and loop bounds; did not measure; the S value depends on the strike filter setting)
Fix sketch: index `m_quotes` by `DeadKey(k)` in an `unordered_map` alongside the vector (or erase `subscribed=false` entries that are far from the money on each SyncSubscriptions), and have RecomputeExpectedMove read the quotes once into a local map. Test: the existing `[options]` tests cover the math; the change is data-structure only.

### WP3-02  Per-position P&L subscriptions are never cancelled when a position goes flat, and the reqId counter never wraps
Where:      src/main.cpp:398 (g_pnlSingleNextReqId = 9001), 5017-5022 (subscribe on first sight of a conId), 5019 (`g_pnlSingleNextReqId++`), 6216-6232 (cancel only in Disconnect), 7715-7720 (account switch). No `CancelPnLSingle` call in the flat-position path (4954-4966 erase g_positions but not g_pnlSingleConIds).
What:       `if (pos.conId > 0 && g_pnlSingleConIds.find(pos.conId) == g_pnlSingleConIds.end() ...) { int rid = g_pnlSingleNextReqId++; g_pnlSingleConIds[pos.conId] = rid; ... ReqPnLSingle(rid, ...); }`. The entry is kept for the life of the connection. Position flat: `g_positions.erase(pos.symbol)` and `g_optionPositions.erase(pos.conId)` run, but the P&L subscription and its map entries stay. The counter has no upper bound: after 999 distinct conIds the next rid is 10000, which is the start of the chart market-data pool (`AllocChartMktId`, 10000-10999, main.cpp:1134-1140).
Trigger:    Trade option contracts through one connection (each new contract, including each expired one, is a new conId). Nothing else is needed. Ids are reset only by a manual Disconnect (6232).
Cost:       Each leaked subscription is an IB-side P&L subscription until disconnect: IB keeps streaming daily P&L for contracts the user no longer holds. Map cost about 150 B per conId (three map entries). Per day: a few to a few dozen new conIds, so about 1 KB per day of memory; the IB-side cost is the subscription count. After 999 conIds: reqId 10000+ overlaps the chart market-data pool in IB's id space. `onPnLSingle` maps that id to a symbol (4869-4870), so a chart reqId and a P&L reqId can share an id.
Severity:   waste-ib (and an id-collision risk after ~999 contracts in one connection)
Confidence: high (all insert and erase sites read; the no-wrap is visible at 5019)
Fix sketch: in the flat path (around 4954-4966) call `CancelPnLSingle(rid)` and erase the three map entries; wrap `g_pnlSingleNextReqId` inside 9001-9999 (or reuse from a free list). No pure logic change; a test would need the IB wrapper, so none.

### WP3-03  Chart bar history grows for the life of a symbol: pan-left, live bars, and an O(n) recompute on every tick
Where:      src/ui/windows/ChartWindow.cpp:410 (live bars pushed in pending series), 448-497 (PrependHistoricalData: copies whole series into `merged`, 493-496), 524-557 (UpdateLiveBar appends, 549-551), 580-587 (OnLastPrice: ComputeIndicators() per tick), 4321-4330 (pan trigger), main.cpp:1848-1869 (OnExtendHistory; duration `TimeframeIBDuration(tf)`, core/models/MarketData.h:80-93), main.cpp:4773 (OnLastPrice per tick). ChartWindow.h:352-387 (24 parallel vectors).
What:       Nothing trims `m_series.bars` or the flat arrays. Each live bar appends to 24 vectors; each pan-left extension prepends a whole duration chunk: M1 requests "1 D", M5 "5 D", M15 "10 D", H1 "30 D". Only `RequestNewData` (739-745, symbol or TF change) empties them. `OnLastPrice` (every tick for an intraday chart) runs `ComputeIndicators()` over all bars, which also runs `DetectStructure()` at its tail.
Trigger:    Open an M1 chart on a liquid stock and leave it for a day; pan left a few times to load history; the chart keeps every bar. Every tick that changes the last price recomputes all indicators over the full series.
Cost:       About 240 B per bar: `Bar` 48 B + 7 flat vectors (56 B) + 17 indicator vectors (136 B), plus vector capacity slack up to 2x. An M1 day is 390 bars (RTH) to about 960 bars (with extended hours): about 95-230 KB per day per live M1 chart. Each M1 pan extension adds up to one trading day: about 95-230 KB, and `PrependHistoricalData` copies the whole series once more (transient). Compute cost: ComputeIndicators is linear in n, so with 5000 bars and ~10 ticks/s, a chart does roughly 50,000 bar-updates per second. Estimates; WP-0 should measure heap growth and ComputeIndicators time.
Severity:   leak-unbounded (per symbol, for the life of the chart) plus waste-cpu
Confidence: high for growth (all insert sites read); medium for the cost numbers
Fix sketch: cap `m_series` to the last N bars in UpdateLiveBar / PrependHistoricalData (N chosen per TF, e.g. 2 days' worth), and throttle ComputeIndicators on ticks (for example to once per second, or only when a bar closes). Pure helper candidate: a `TrimToLast(n)` helper in ChartAnalysis.h with a test.

### WP3-04  Finished orders stay in the in-memory order map for the whole connection
Where:      src/main.cpp:313 (g_liveOrders), inserts 1916, 3464, 3516, 3578, 3801, 5047, 5266, 5288; erases only 5078 (external orders dropped at openOrderEnd) and clear at 3968. Read path: 872-908, 1527, 5075.
What:       `g_liveOrders[order.orderId] = order;` (5047) and `g_liveOrders[id] = order;` (1916) add orders; `orderStatusChanged` (5098-5110) changes status to Filled / Cancelled / Rejected but never removes the entry. The name "live" is misleading: the map holds every order of the connection.
Trigger:    Trade through the day. Each order, child leg and bracket child adds one entry. The per-event loops (UpdateAllChartPendingOrders 872-908, RecomputeUnguardedPositions 908-918, PushWorkingComboLegs 340-352 and 866-890) iterate all of them on every order event.
Cost:       About 500-600 B per Order (about 15 strings plus a ContractSpec that holds a vector). 50-200 orders a day: about 30-120 KB per day, and O(n) per order event. Estimate.
Severity:   leak-bounded (per connection; cleared on disconnect)
Confidence: high (all insert and erase sites read)
Fix sketch: in the status handler, erase terminal orders older than the history window once they have been mirrored to OrdersWindow (which already keeps them 7 days). Check first that no chart pending overlay needs terminal rows (UpdateAllChartPendingOrders filters by status, so probably not).

### WP3-05  Trade history duplicates on every execution replay (also double-counts commissions)
Where:      src/core/services/IBKRClient.cpp:1231 (`bool fromQuery = (m_filterReqId >= 0 && reqId == m_filterReqId);`), IBKRClient.h / .cpp: m_filterReqId is initialised to -1 (IBKRClient.h:687) and never assigned anywhere (grep shows no other write); main.cpp:5152-5221 (onFillReceived: `m_symbolCommissions[...] +=`, `OnTradeExecuted`, `OrdersWindow::OnFill`, `TradingWindow::OnFill`); main.cpp:3812 (filtered "Load"), 4177 and 4231 (ReqExecutions(8001) on connect and reconnect).
What:       `fromQuery` is always false, so every reqExecutions reply goes through `onFillReceived`, not `onQueriedFill`. `onFillReceived` has no dedupe by execId: `g_PortfolioWindow->OnTradeExecuted(tr)` inserts unconditionally (PortfolioWindow.cpp:286-290), `g_symbolCommissions` accumulates, and `OrdersWindow::OnFill` adds `fill.commission` again (OrdersWindow.cpp:232-236). `OrdersWindow::OnQueriedFill` does dedupe, but that is the other path.
Trigger:    Auto-reconnect while the windows stay open (FinishConnect re-sends ReqExecutions(8001)), or pressing "Load" in the Orders history filter, replays today's fills with the same execIds. The Portfolio trade list gets each fill again; the P&L strip commission and the Orders row commission are increased again.
Cost:       Trade list: about 200 B per duplicated fill; a 100-fill day duplicated on each reconnect is about 20 KB per reconnect, plus the commission totals. Estimate. The commission double-count is the more important effect (a P&L number is wrong, not just memory).
Severity:   cosmetic (correctness; outside a pure resource finding; flagged because it grows a container)
Confidence: high on the code path (read IBKRClient.cpp 1222-1250, 792-796 and the m_filterReqId grep); not reproduced live.
Fix sketch: keep a set of seen execIds in main.cpp (or in PortfolioWindow) and skip the trade-record and commission updates for an execId already seen; or set `m_filterReqId` in `ReqExecutions` so queried fills take the `onQueriedFill` path. The second is the smaller change but needs the Load path re-checked. Test: a `[fill][dedupe]` case if the dedupe goes in a pure helper.

### WP3-06  News item id maps grow for the whole News window lifetime
Where:      src/ui/windows/NewsWindow.cpp:56-57 and 68-69 (inserts into m_itemProviders / m_itemArticleIds, never erased; `grep` shows only find() reads elsewhere: 49-50, 419-421); src/main.cpp:5527 (`item.id = ++g_newsItemId;`, a process-wide counter), 5694 (`++g_histNewsId`).
What:       Every headline that reaches `OnMarketNewsItem` or `OnHistoricalNewsItem` adds one entry to each of two maps keyed by item id. The header keeps them forever. The window is recreated on connect (CreateTradingWindows), so the growth is per connection.
Trigger:    Subscribed symbols and the market feeds deliver headlines all day (tickNews, 201 and 210-249). The same article can arrive twice (the code notes that at NewsWindow.cpp:46-47), so the map gets two entries for one article.
Cost:       About 150-200 B per headline across both maps (provider code ~5 chars, article id ~30-40 chars, node overhead). 100-1000 headlines a day: about 15-200 KB per day. Estimate; the volume is the main uncertainty.
Severity:   leak-bounded (per connection)
Confidence: medium (all insert sites in NewsWindow.cpp read; I did not read main.cpp's news routing end to end, so the headline volume is an estimate)
Fix sketch: drop the map entry when a row leaves m_marketNews (the cap at 63/84 already removes rows), or key the duplicate check on (provider, article id) stored in the row itself and remove the two maps. Smallest: erase the two map entries where `resize()` drops rows at NewsWindow.cpp:63 and 84.

### WP3-07  Rotating reqId pools can hand out an id that a long-lived subscription still holds
Where:      src/main.cpp:1134-1200 (pools: chart mkt 10000-10999; hist 12000-12999; ext 13000-13999; trading mkt 14000-14999; depth 15000-15999; tick 16000-16999; option mkt 22000-22999; combo-leg lookup 21100-21199), 1646-1656 (ReqChartData allocates a new mkt id on each symbol change), 4572-4590 (tick dispatch validates the symbol via g_tickerSymbols[tickerId]); src/ui/windows/OptionsChainWindow.cpp:685-707 (every subscribe takes `OnAllocOptionReqId()`), 303-310 (`m_reqIdToQuote`), 3704 in main.cpp.
What:       Each pool is a process-wide static counter that wraps after its size, so after N allocations the same id comes back. Nothing checks that the id is free. A subscription that is still live when its id comes back is a duplicate for IB, and the dispatcher routes by id. For charts the dispatcher compares `g_tickerSymbols[tickerId]` (the new symbol) with the window symbol; a tick from the old, still-live IB subscription on that id is then delivered as the new symbol. For the option chain, `m_reqIdToQuote[reqId]` is overwritten by the new quote, so ticks of the still-live old contract go to the new one.
Trigger:    Long session with many re-subscribes: about 1000 symbol changes across all charts (a group broadcast re-subscribes every chart in the group), or about 1000 option (re)subscribes (scrolling a chain in a long session). The chart or quote must stay on its symbol or strike during that time (for example a pinned chart in a group that others move, or an ATM strike that stays in view).
Cost:       Wrong prices on a chart or chain quote, and IB duplicate-id errors (error 322 class). No memory cost. Not measured; the 1000 count is from the pool sizes, and the exact count depends on how often the user changes symbols.
Severity:   waste-ib (wrong-data risk; not a crash)
Confidence: medium (the wrap and the id reuse are read in code; the IB duplicate behaviour is inferred, not observed)
Fix sketch: before handing out an id, skip ids still in use (`g_tickerSymbols.count(id)` for the chart/trading pools; `m_reqIdToQuote.count(id)` for the chain). That is a small loop in each Alloc*. Also P&L single ids overflow into the chart pool (WP3-02); fixing WP3-02 removes that overlap.

### WP3-08  The scanner, company-name and WSH caches keep one entry per symbol for the window's life
Where:      src/ui/windows/ScannerWindow.cpp:164, 175, 203 (inserts into m_companyNames, m_techCache, m_fundCache; no erase, no clear); src/main.cpp:211, 219 (g_scannerHistFetched, g_scannerFundFetched; no erase), 3322-3323 (g_companyNames, g_companyNameRequested), 357 and 5604 (g_symbolConIds); src/ui/windows/WshCalendarWindow.cpp:136 (m_events).
What:       Each of these is keyed by symbol (or conId) and never removed while the process or connection lives. Each entry is small: 50-250 B.
Trigger:    Scan many symbols over a session (scanner rescans, watchlist and chart symbol changes). Each new symbol adds one entry per map.
Cost:       A few hundred symbols a day: about 50-100 KB per day across all of these, and the WSH list adds about 300 B per calendar event. Estimate.
Severity:   leak-bounded (tiny)
Confidence: high on the insert and no-erase reading
Fix sketch: none needed now. If wanted later: clear the scanner caches in the scan-start path where m_results is replaced (ScannerWindow.cpp:116), since the cache is only read for symbols in m_results.

## 4. Ideas (not findings)

- Replace the linear `FindQuote` with a keyed lookup (WP3-01) at the same time as the subscription diff, so the chain's per-tick work does not grow with history.
- A single "order history" owner: `g_liveOrders` (main.cpp) and `OrdersWindow::m_orders` hold the same orders twice. One of them could hold the data and the other a view. Larger change; not for Phase 2 unless the user wants it.

## 5. Could not decide (what is missing)

- Actual headline volume (WP3-06) and fill and order volume per day: needs WP-0 soak logs or the user's own counts.
- Whether IB's duplicate-id reply on a still-live reqId (WP3-07) is error 322 or silent: needs an IB Gateway API log during a forced collision; not reproducible without a long session.
- Compute cost of `ComputeIndicators` per tick on a grown series (WP3-03): needs a timed run of a chart with 5000 bars.
- `m_pendingFills` (IBKRClient.h:572): entries stay until a commission report arrives. Whether IB ever omits it for an execution is unknown; it is bounded by the connection and by the number of such executions.

## 6. Notes outside WP-3 scope (for the triage)

- WP3-05 is a correctness bug (commission double-count), not only a container issue. It may be worth a fix ahead of the resource work.
- `ChartWindow::PrependHistoricalData` copies the whole series (`merged`) on every pan extension; WP-4 should time it.
- `pendingBars` / `pendingExtBars` in `ChartEntry` keep their vector capacity after `clear()` (main.cpp:4407, 4429, 4449). That leaves about the peak load size per chart (roughly 50-100 KB per chart). Tiny; not a finding.
