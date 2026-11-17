# WP-5 findings: IB subscriptions and request budget

Audited at 1.6.14 (`86a9b4b`). Line numbers are from this checkout. Read-only audit, no builds.
"Assumption" marks IB behaviour that is not shown by the code or its comments.

## 1. Stream table

Cancel columns: (a) symbol change, (b) window closed with x, (c) account switch, (d) disconnect / exit, (e) reconnect.
`-` = not cancelled / not handled (confirmed by reading, see findings).

| Stream | Opener | reqId | (a) symbol change | (b) x close | (c) account switch | (d) disconnect | (e) reconnect |
|---|---|---|---|---|---|---|---|
| Chart quote | `ReqChartData` main.cpp:1655 | 100-109 initial, pool 10000-10999 | cancel + rotate 1651-1654 | **-** (WP5-01) | n/a | `CancelAllSubscriptions` 3878 | re-opened once, same id, no `open()` check 4258 |
| Chart history (keepUpToDate if intraday) | `ReqChartData` 1643 | 1-19 initial, pool 12000-12999 | cancel + rotate 1630-1631 | **-** | n/a | 3879 | re-opened once 4253 |
| Chart extend (pan-left, no keepUpToDate) | `OnExtendHistory` 1864 | pool 13000-13999 | cancel 1633 | - | n/a | 3881 if active | cancelled 4250 |
| Chart WSH events | `onContractDetails` -> `ReqWshEventData` (conId) | `ce.wshId` | cancel 1843 | - | n/a | not cancelled (socket closes) | not re-issued |
| Order Book quote | `ApplyTradingSymbol` 1542 | pool 14000-14999 | cancel + rotate 1534-1541 | **-** | n/a | 3885 | re-opened via `ApplyTradingSymbol` 4274, no `open()` check |
| Order Book depth | 1545 | pool 15000-15999 | 1535 | **-** | n/a | 3886 | same |
| Order Book tick-by-tick | 1547 | pool 16000-16999 | 1536 | **-** | n/a | 3888 | same |
| Depth mode / row change | 2016, 2027 | rotate depth pool | cancel old, rotate | - | n/a | - | - |
| Scanner scan | `OnScanRequest` 2080 | `scanBase`..+99 per instance | cancel 2066 on next scan | **-** (WP5-02) | n/a | not cancelled (socket closes) | `CancelScannerData` 4185/4239 (new client) |
| Scanner quotes (25/instance) | `onScanEnd` 5459 | `mktBase`+0..24 | cancelled at next scan 2073, 5421 | **-** | n/a | not in `CancelAllSubscriptions`; map erased 3978 | not re-opened (dead) |
| Scanner fundamentals (258, transient, only if MktCap/P/E column on) | 5511 | `fundBase`+0..24 | cancelled 5427 / on ratios 5839 | **-** | n/a | - | - |
| Scanner history (1 Y daily, one-shot) | 5485 | `histBase`+slot | no cancel needed | - | n/a | - | - |
| Watchlist quotes (165,233) | `SubscribeItem` WatchlistWindow.cpp:520 -> main.cpp:3202 | 7000-7999 | `UnsubscribeItem` / `LoadWatchlists` `CancelAll` | `PruneClosedWatchlists` main.cpp:3250 destroys + `CancelAll` | n/a | `CancelAll` 3893-3896 | **not re-opened** (WP5-05) |
| Options chain quotes (cap 72) | `SyncSubscriptions` OptionsChainWindow.cpp:713 | pool 22000-22999 | `SetSymbol`/`RequestChain` -> `CancelAll` 121, 780 | `Render` !m_open -> `CancelAll` 3166 | n/a | `CancelAll` 3896 | **not re-opened** |
| Chain underlying | `OnRequestUnderlying` main.cpp:3637/3641 | 21002 (fixed) | cancel + re-request 3622 | **-** (WP5-06) | n/a | **-** (not in `CancelAllSubscriptions`) | not re-opened |
| Chain IV history (daily, 1 per load) | `RequestIvHistory` 805 | 21004-21009 rotating | cancel previous 801 | n/a (load only while open) | n/a | - | one-shot, not re-issued |
| Orders price ladder quote (single / combo legs) | 3819 / 3828 | 8002; 8003-8008 | cancel + re-request | `StopLadder` only on edit end/Esc; **not on window close** (WP5-09) | n/a | - | - |
| Analysis pin underlying | `PinAnalysis` 645/647 | 21200 (fixed) | re-pin cancels 596 | closing analysis window -> `UnpinAnalysis` 7897 | pin dropped when legs vanish 7912 | 3862 | not re-opened (WP5-05) |
| Alert feeds | `SyncAlertFeeds` 707 | pool 23000-23999 | n/a | n/a | n/a | 3873 | `RestartAlertFeeds` 4300: cancel + re-open once |
| News RT (generic tick 292 on AAPL) | `SubscribeToNews` 4206/4266 | 201 | never changes | n/a | n/a | not cancelled (socket closes) | re-opened once |
| News topic feeds (`mdoff,292`) | `SubscribeNewsTopic` 2345 | 210-249 | n/a | not tied to the News window | n/a | 3896 | `g_newsTopicFeeds.clear()` 4294 then `onNewsProviders` re-syncs once |
| Futures health /ES /NQ (+Dec) | `ReqFuturesMarketData` 4212-4220 | 140-143 | n/a | n/a | n/a | 3892 | re-opened once 4272-4280 |
| Account updates | 4169/4223 | - | n/a | n/a | off/on 7706-7707 | 6213 | re-opened once |
| Positions (all accounts) | `ReqPositions` 4170/4224 | - | n/a | n/a | not re-sent | no cancel method exists | re-sent once on new client |
| Account summary ("All") | 4171/4225 | `ACCT_SUMMARY_REQID` | n/a | n/a | not re-sent | not cancelled (socket closes) | re-sent |
| PnL account | `onAccountValue` 4843 | 9000 | n/a | n/a | cancel + flag reset 7708-7710 | 6214 | **not re-opened** (WP5-03) |
| PnL single | `onPortfolioUpdate` 5023 | 9001-9999 | n/a | n/a | cancel + maps cleared 7715-7720 | 6216 | **not re-opened** (WP5-03/04) |
| Display group events (G1-G4) | 4286, 7117 | 8061-8064 | n/a | n/a | n/a | 7120 on toggle; socket otherwise | re-sent if sync on |
| News bulletins | 4299 | - | n/a | n/a | n/a | 3872 | re-sent once |
| WSH calendar (per conId) | `Subscribe` (deferred while closed) | 8070-8199 | n/a | not cancelled on close, but none issued while closed | n/a | `CancelAll` 3897 | not re-opened |

## 2. Findings

### WP5-01  Closed (x) Chart and Order Book windows keep streaming, and are re-subscribed on every group symbol change and on reconnect
Where:      src/ui/windows/ChartWindow.cpp:790 (`if (!m_open) return false;`), TradingWindow.cpp:495, main.cpp:1593-1597 (`BroadcastGroupSymbol`), 4245-4262, 4274 (reconnect)
What:       Nothing cancels a chart or order book stream when its window is closed; `Render()` just returns. `BroadcastGroupSymbol` has no `open()` check:
            `for (auto& e : g_chartEntries) if (e.win && e.win->groupId() == groupId) e.win->SetSymbol(sym);` and
            `for (auto& te : g_tradingEntries) if (te.win && te.win->groupId() == groupId) ApplyTradingSymbol(te, sym);`.
            `ChartWindow::SetSymbol` ends in `RequestNewData()` which calls `OnDataRequest` -> `ReqChartData` (hist + keepUpToDate stream + quote) and WSH contract lookup. `ApplyTradingSymbol` opens quote + depth + tick-by-tick. The reconnect branch (`for (auto& ce : g_chartEntries) { if (!ce.win) continue; ...}`) also ignores `open()`. The first-connect restore does check `open()` (2231, 4190, 4203), so only the closed-then-continue path leaks.
Trigger:    Open a chart and an order book in G1, close both with x, then click symbols in a G1 watchlist: every click re-issues hist + quote (chart) and quote + depth(up to 300 rows) + tick-by-tick (order book) for the hidden windows, plus 2 history requests that count against pacing.
Cost:       Per closed chart: 1 market-data line + 1 history stream (and 1-2 history requests per symbol click). Per closed order book: 1 line + 1 depth + 1 tick-by-tick. Estimated from the code, not measured.
Severity:   waste-ib
Confidence: high (read every path)
Fix sketch: In `BroadcastGroupSymbol`, `ApplyTradingSymbol` callers and the reconnect branch, skip entries whose `win->open()` is false; on close (`open()` true->false edge, like the replay detector at 7886) cancel mkt/hist/depth/tick-by-tick; on reopen re-request. Smallest: guard the three call sites plus one edge-detect cancel in `RenderTradingUI`.

### WP5-02  Closed Scanner keeps its scan subscription and 25 quote lines
Where:      src/ui/windows/ScannerWindow.cpp:422 (early return), main.cpp:2063-2080, 5416-5460
What:       Quotes are cancelled only at the start of the next scan (`OnScanRequest`) or the next `onScanEnd`. Closing the scanner with x does neither; the 25 `mktBase` quotes (`kMktSlots = 25`) and the scan subscription (`se.subActive`) stay up. The auto-refresh timer is also behind the early return, so nothing ever replaces them while closed. Quotes are also in `g_tickerSymbols` so ticks keep being processed.
Trigger:    Run a scan, close the scanner window.
Cost:       25 lines (up to 25 more, transient, while 258 fundamentals subs are pending) per scanner instance until reopen + rescan or disconnect.
Severity:   waste-ib
Confidence: high
Fix sketch: On the open->closed edge cancel scan + quote slots (same loop as 2063-2077).
Note:       `onScanEnd` re-subscribes all 25 quotes every time it fires and nothing calls `CancelScannerData` after `onScanEnd`. If IB repeats scan data for an open scanner subscription (assumption, I believe it does, not shown by the code) each repeat cancels and re-opens 25 quotes.

### WP5-03  Account P&L and per-position P&L are never re-requested after a silent reconnect
Where:      src/main.cpp:4841-4843 (`g_pnlSubscribed`), 5017-5023 (`g_pnlSingleConIds`), reset only at 6228-6232 (`Disconnect()`) and 7710/7718 (account switch)
What:       `StartSilentReconnect` -> `LaunchConnectWorker` deletes the client and creates a new one, then `FinishConnect(true)` runs. The reconnect branch re-sends account updates, positions, summary, news, futures, alerts but not P&L. `g_pnlSubscribed` stays true so `ReqPnL(9000)` is never re-sent (`if (!g_pnlSubscribed && ...)`), and every conId is still in `g_pnlSingleConIds` so `onPortfolioUpdate` skips `ReqPnLSingle` (`g_pnlSingleConIds.find(pos.conId) == end()` is false). Grep shows no other reset.
Trigger:    Connection drops (Gateway restart / network) and auto-reconnect succeeds.
Cost:       Day P&L and (since 1.5.69) the per-second price / market value / unrealized P&L of every position stay frozen after reconnect until the user disconnects and reconnects or switches account. Account P&L card also stale.
Severity:   waste-ib (dead feeds, wrong data shown as live)
Confidence: high
Fix sketch: In the reconnect branch of `FinishConnect` (and `onConnectionChanged(false)`), set `g_pnlSubscribed = false` and clear `g_pnlSingleConIds` / `g_pnlReqIdToSymbol` / `g_pnlReqIdToConId`; the existing per-frame paths re-subscribe. Do not cancel (client is new).

### WP5-04  pnlSingle ids are never recycled; 999 slots overflow into the chart quote pool
Where:      src/main.cpp:399 (`g_pnlSingleNextReqId = 9001`), 5017-5023
What:       `int rid = g_pnlSingleNextReqId++;` No upper bound, no free-list, no cancel when a position goes flat (`onPortfolioUpdate` with quantity 0 only erases the window row / `g_positions`). The counter resets only in `Disconnect()` (6232). Account switch clears the maps (7718-7720) but not the counter. After 999 distinct conIds in one session, `rid` becomes 10000+, i.e. the start of the chart quote pool (`AllocChartMktId`, 10000-10999) and then 11000+ (replay range 11000-11999).
Trigger:    One session with 20 positions plus 50 account switches, or a day of option trading with many distinct legs (each new conId, including legs that later close, takes a slot; IB also sends flat rows for positions closed today).
Cost:       Dead P&L subscriptions accumulate (IB pnlSingle limits unknown; assumption they are not market-data lines). Past 9999 the id can equal a live chart quote id; whether IB treats a pnlSingle and a mktData with the same id as a conflict is an assumption. Local routing is by separate maps so the app side is fine, `g_pnlReqIdToSymbol/ConId` also grow to 999 entries.
Severity:   leak-bounded (id exhaustion) / crash-risk low
Confidence: medium (read all increment/reset sites; IB side unverified)
Fix sketch: Cancel `ReqPnLSingle` when a conId's quantity reaches 0 and erase its map entries; reset `g_pnlSingleNextReqId` to 9001 on account switch (the maps are cleared there anyway); skip subscribing when `rid > 9999`.

### WP5-05  After a reconnect, watchlist, chain, scanner quotes and analysis pin stay "subscribed" but are dead
Where:      main.cpp:4245-4285 (reconnect branch re-opens only charts and order books); WatchlistWindow.cpp:520 (`item.subscribed = true`), OptionsChainWindow.cpp:688-713 (`q.subscribed`), main.cpp:3622 (21002), 645 (21200)
What:       The new client has none of the old subscriptions. Watchlist items keep `subscribed = true`, chain quotes keep `subscribed = true` with live-looking reqIds (so `DiffSubscriptions` sees nothing to do), the chain underlying (21002), analysis pin spot (21200) and scanner quotes are not re-sent. Grep for a reconnect hook in the windows (`Resubscribe`, `OnConnected`) found none.
Trigger:    Auto-reconnect while a watchlist / chain / pin is open.
Cost:       Frozen prices until the user reloads (chain: next expiry change or reload; watchlist: restart or preset switch). Not a line leak.
Severity:   waste-ib (stale data)
Confidence: high for watchlist and chain (read the subscribe flag paths), medium for pin
Fix sketch: In the reconnect branch call a `CancelAll()`-style reset that clears the flags without sending cancels, then let the existing paths re-subscribe (chain: `CancelAll` + `m_lastVisLo = -1` already forces a resync; watchlist needs a `ResubscribeAll`).

### WP5-06  Lines that are always on regardless of which windows are open
Where:      main.cpp:4206 (news RT 201), 4212-4220 (futures 140-143), 2345 (topic feeds), 3637 (chain underlying 21002)
What:       (a) `SubscribeToNews(201)` opens an AAPL market-data line with tick 292 for the whole session, whether or not a News window exists. (b) Four futures lines (/ES, /NQ front month + Dec) are opened unconditionally; there is no check that any chart enables market health. (c) One news topic feed per enabled entitled provider (user's log: BRFG, BRFUPDN, DJNL live) feeds only the News Market tab, stays on with the News window closed. (d) Closing the options chain cancels option quotes (`CancelAll`) but not the underlying 21002, and it is not in `CancelAllSubscriptions`. Unlike (a)-(c) this one holds the line only until next load.
Trigger:    Connect.
Cost:       1 + 4 + N(providers enabled, 3 for this user, up to 8 refused/failed which are not open) + 1 = about 9 lines baseline. Whether `mdoff` news feeds count as market-data lines is an assumption (they use `reqMktData`).
Severity:   waste-ib
Confidence: high (read every opener)
Fix sketch: Gate futures on "any chart has market health on" (ChartWindow already knows); cancel 21002 in the chain close edge; leave news as is unless lines run short (a Settings switch exists per provider).

### WP5-07  IV history adds one history request per chain load, with no pacing guard
Where:      OptionsChainWindow.cpp:784 (`RequestChain` -> `RequestIvHistory`), 805-818; main.cpp:3656-3660
What:       Every `RequestChain` (typed symbol, Load Chain, and every group broadcast while the chain is open via `SetGroupSymbol`) cancels the previous IV request and sends a new 1 Y / 1 day `OPTION_IMPLIED_VOLATILITY` request. It is not keepUpToDate (daily bar size, `IBKRClient.cpp:223`), so it needs no cancel after completion (and `OnIvHistoryEnd` zeroes `m_ivHistReqId`, correct). Together with each chart's `ReqChartData` (hist per chart) a symbol click with 3 charts + chain open issues about 4 history requests plus quote / contract-detail / secdef requests. The only pacing code is the chart style-switch queue (main.cpp:1081, `kStyleSwitchThrottleSec`); the symbol path has none, and error 162 is only handled for scanner cancels (5784).
Trigger:    Click through a watchlist quickly with the chain open.
Cost:       4+ history requests per click. IB's rule of 60 history requests per 10 minutes (assumption from main.cpp:1081's comment, not verified) is reached after about 15 clicks; "identical request within 15 s" pacing (assumption) applies if the same symbol is clicked twice.
Severity:   waste-ib
Confidence: medium (request count read, limits assumed)
Fix sketch: Debounce `SetGroupSymbol` -> `RequestChain` (e.g. 400 ms) so only the final symbol of a quick sequence loads. A closed chain is not affected (`m_loadOnOpen` defers, verified).

### WP5-08  Rotating id pools wrap without a liveness check
Where:      main.cpp:666-671, 1134-1198 (`AllocChartMktId/HistId/ExtId`, `AllocTradingMktId/DepthId/TickId`, `AllocOptionMktId`, `AllocAlertFeedId`)
What:       `id = s_next++; if (s_next > max) s_next = base;` Nothing checks whether the wrapped id is still owned. For the chain, `m_reqIdToQuote[q->reqId] = ...` would overwrite the mapping of a still-live quote, so its ticks go to the wrong row. Pools are shared (one counter for all charts' hist ids, etc.).
Trigger:    Chain: 1000 (re)subscribes in one session while one row stays subscribed all along (same symbol + expiry, ATM core pinned). Charts: about 500 symbol / TF changes across all charts while another chart (e.g. in another group) keeps its original histId.
Cost:       Wrong prices on one row / duplicate id error. Low probability, no memory cost.
Severity:   crash-risk (data) low
Confidence: low-medium
Fix sketch: Skip ids present in the owner set (chain `m_reqIdToQuote`, entries' current ids) inside the allocators.

### WP5-09  Orders price-ladder quote survives closing the Orders window mid-edit
Where:      OrdersWindow.cpp:249 (early return), 729-736 (`StopLadder` only from edit end / Esc)
What:       Closing the window with x during an inline edit leaves `m_editOrderId` and the quote (reqId 8002, or legs 8003-8008) subscribed until the window is reopened and the edit ends.
Trigger:    Start editing a combo order price, close Orders.
Cost:       Up to 7 lines for as long as the window stays closed.
Severity:   waste-ib
Confidence: high
Fix sketch: Call `CancelEditOrder()` when `!m_open`.

### WP5-10  `CancelAllSubscriptions` is not a complete list (cosmetic)
Where:      main.cpp:3860-3898
What:       It does not cancel scanner scan / quotes / fundamentals, chart WSH ids, news RT 201, account summary, display groups, ladder quotes. All are dropped when the socket closes, so there is no line leak; the remaining effect is only the "Can't write, socket closed" log noise the helper was written to avoid.
Severity:   cosmetic
Confidence: high

## 3. Leads closed

| Lead | Result |
|---|---|
| Chain cancels on close; do Chart / Order Book / Scanner / Watchlist / News? | Chain: yes (option quotes; underlying 21002 stays, WP5-06). Watchlist: **refuted**, x destroys the window and `CancelAll` runs (3250). Chart / Order Book / Scanner: **confirmed** leak (WP5-01, 02). News window: refuted, it holds no stream of its own (RT 201 and topic feeds are session-wide, WP5-06). |
| Scanner "12 quotes per instance" | **Refuted**: `kMktSlots = 25` (main.cpp:122), plus up to 25 transient fundamentals subs. |
| pnlSingle cancelled when flat / after account switch | Flat: **confirmed never cancelled** (WP5-04). Account switch: cancelled and maps cleared (7715-7720), but the id counter is not reset. |
| Analysis pin feed 21200 | **Refuted** as a leak: cancelled by unpin, close (7897), disconnect (3862), pin loss (7912). Not re-opened on reconnect (WP5-05). Fixed id is reused across symbols without a staleness check, but `g_analysisPin = {}` on re-pin makes it cosmetic. |
| Alert feeds 23000+ | **Refuted**: `SyncAlertFeeds` cancels removed symbols, `RestartAlertFeeds` after (re)connect, cancelled on disconnect (3873). Duplicate lines for a symbol that is also in a chart are still separate requests (whether IB counts duplicates once is an assumption). |
| News topic feeds 210-249 | Per enabled provider, cancelled when disabled, cleared on connect; not tied to the News window (WP5-06). |
| Futures health 140-143 subscribed when no chart uses market health | **Confirmed** unconditional (WP5-06). |
| IV history on every chain load, pacing | WP5-07; cancel-before-reissue is correct, closed chain does not request. |
| `g_tickerSymbols` entries for rotated ids | **Refuted**: chart (1652), order book (1537) erase the old id before assigning the new one; scanner slots erased at rescan; cleared on disconnect (6239) and destroy (3938-3979). Chain 21002 is one key overwritten. No growth path found. |
| Re-opened once or twice on reconnect | **Once**, on the same ids (new client has nothing). Exception: closed windows are re-opened too (WP5-01). |

## 4. Typical layout: market-data lines

Assumptions: each `reqMktData` counts as one line, including duplicates of the same symbol (IB's rule for duplicates is not shown in the code); depth, tick-by-tick, history (incl. keepUpToDate), PnL, scanner subscriptions, contract details, secdef do **not** count as market-data lines (my recollection of IB's docs; depth and tick-by-tick have their own, smaller limits that I am not sure of).

| Item | Lines |
|---|---|
| 3 charts (quote each) | 3 |
| 1 order book (quote; depth + tick-by-tick not counted) | 1 |
| Chain, option quotes (cap `kMaxOptionSubs = 72`; a typical 30-row view = 60) | 60 to 72 |
| Chain underlying (21002) | 1 |
| Watchlist, 7 symbols | 7 |
| Scanner, 25 rows | 25 (+ up to 25 transient 258 subs if MktCap / P/E visible) |
| 20 positions with pnlSingle | 0 (not market data, assumption) |
| 3 alerts on distinct symbols | 3 |
| Futures health | 4 |
| News RT (AAPL) | 1 |
| News topic feeds (3 live for this user) | 3 |
| Analysis pin, ladder (while in use) | 0 to 8 |
| **Total** | **108 (typical chain) to 120 (chain at cap)**, up to about 145 during a fundamentals burst |

Compared with "about 100": over the limit with the scanner and chain both fully used, around 85 without the scanner. The reclaimable ones from the findings: closed scanner (25), closed charts / order books (WP5-01), futures (4), topic feeds (3), news RT (1). IB's real limit depends on the account (assumption) and the code has no line counter or handling of the refusal error for quotes beyond the limit that I found (only 354 / 10090 are special-cased for charts and order books, main.cpp:6065-6070); the chain marks a rejected contract dead (`m_deadContracts`) which would permanently hide a strike refused for line-limit reasons, not checked.
