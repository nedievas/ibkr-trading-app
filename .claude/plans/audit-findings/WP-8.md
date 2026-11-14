# WP-8 Code health inventory (list only, no fixes)

Run against 1.6.14 (`86a9b4b`). Scratch builds in
`/tmp/claude-1000/-home-nedievas-development-ibkr-trading-app/a2603aef-0ea5-4559-a7b9-a48dac393fb6/scratchpad/wp8/{build,buildE}`.
Tests in the Release scratch build: ctest 562/562 pass; `tests-core` 530 cases / 2480 assertions; `tests-ibkr` 32 cases / 120 assertions.

## A. Compiler warnings (own code, Release, default gcc warnings)

33 warning lines at 30 distinct sites, none under `tests/`. Real bugs: 0.

| Site | Warning | Verdict |
|---|---|---|
| `src/ui/windows/PortfolioWindow.cpp:505` | unused variable `cardW` | dead code |
| `src/ui/windows/ScannerWindow.cpp:551` | unused variable `autoLabel` (the two buttons below use string literals; `m_autoRefresh` itself is live) | dead code |
| `src/ui/windows/TradingWindow.cpp:2318` | unused `kTypes[]` | dead code |
| `src/ui/windows/OptionsChainWindow.cpp:299` | unused parameter `msg` of `OnOptionError` | dead code (parameter) |
| `src/ui/windows/OptionsChainWindow.cpp:1315` | unused variable `itmBadgeW` | dead code |
| `src/core/services/IBKRClient.cpp:1446` | missing initializer for `MsgContractConId::{expiry,multiplier,right,tradingClass}` (4 lines) | harmless: assigned on the next lines (1448-1451 area), value-initialised before |
| `src/core/services/IBKRUtils.h:220,226` | `%04d` into `char buf[8]` may truncate | harmless: year is a real calendar year (4 digits) |
| `src/main.cpp:7554` | `"G%d"` into `char grp[8]` | harmless (group id 1..10) |
| `src/ui/DatePicker.h:107,110` | `%d##dp%d%d` into 16 / `%04d-%02d-%02d` into 12 | harmless (year/month/day bounded) |
| `src/ui/windows/ChartWindow.cpp:801` | `"G%d"` into `char grp[8]` | harmless |
| `ChartWindow.cpp:905` | `strncpy` copying 31 bytes, no NUL | harmless: line 906 sets the NUL by hand |
| `ChartWindow.cpp:935-938` | `"/ES %04d12"` into the static name buffers | harmless |
| `ChartWindow.cpp:2765` | `"%s  [%s]"` into `symDateBuf[64]`, symbol up to 31 | harmless (truncates a label only) |
| `ChartWindow.cpp:2833-2834` | `"ES %04d12"` | harmless |
| `ChartWindow.cpp:4538` | `barDate[11]` from `%04d-%02d-%02d` | harmless |
| `src/ui/windows/NewsWindow.cpp:176`, `ScannerWindow.cpp:457`, `TradingWindow.cpp:501` | `"G%d"` into `char grp[8]` | harmless |
| `TradingWindow.cpp:674` | `"DOM  %s"` into `domLabel[32]` | harmless unless a symbol is >= 27 chars; only truncates a label |
| `ReplayWindow.cpp:83,95,276` | `"Replay %s###replay%d"` into `m_title[48]` | harmless in practice; a symbol of ~31 chars would cut the `###replay<id>` suffix and change the window id (a futures/option symbol that long does not occur) |
| `WshCalendarWindow.cpp:278,286` | `%02d` into the date buffers | harmless (month/day bounded) |

## B. Dead code (list only)

Leftovers of removed features: grep found **none** for leg-in (`NeedsLegIn`, `MarketableStockLimit`, `g_pendingStockLegs`, `SendLegInStock`), account aliases, `m_tickFills`, the chain `Auto` toggle (`OPT_AUTO`; `m_autoRefresh` hits are the Scanner's own), `g_userCancelled`, `m_sessionIdx`, `DrawColumnChooserPopup`, `m_colEnabled`, `m_show*` column bools, `COL_*` keys, `OnLegInSubmit`, `StageSpreadLeg`, `PriceClickCell`, `IsApiDroppedRiskReversal`, `g_chartModesDirty`, `CalcVWAP`. Only mention: `src/main.cpp:3054` comment about a legacy `TICKFILLS:` line (intentional).

Callbacks declared and assigned but **never invoked**:
- `ReplayWindow::OnPaperOrderCancel` (`ReplayWindow.h:127`; assigned `main.cpp:3431` as a no-op lambda with `(void)idx; (void)localId;`) - no call site in `ReplayWindow.cpp`.
- `OptionsChainWindow::OnReqMatchingSymbols` (`OptionsChainWindow.h:167`; assigned `main.cpp:3537`) - the chain's symbol box uses `ui::g_symbolSearchFn` (`OptionsChainWindow.cpp:861`), so this one is unused.
All other `std::function` members in window headers / IBKRClient.h are both assigned and invoked.

Methods with no caller (grep of the whole identifier across `src/` + `tests/` gives only declaration/definition):
- `IBKRClient::CancelAccountSummary` (.h:341, .cpp:482)
- `IBKRClient::ReqPositionsMulti` / `CancelPositionsMulti` / `ReqAccountUpdatesMulti` / `CancelAccountUpdatesMulti` (.h:323-329) - the multi-account request set; the matching `MsgPositionMulti` / `MsgAccountUpdateMulti` dispatch is therefore never fed.
- `IBKRClient::CancelFuturesMarketData` (.h:302, inline, zero uses at all)
- `IBKRClient::CancelWshMetaData` (.h:366, .cpp:450)
- `IBKRClient::QueryDisplayGroups` (.h:392, .cpp:1633) (and `MsgDisplayGroupList` / `onDisplayGroupList` handler that only it feeds)
- `IBKRClient::ReqHistoricalTicks` (.h:226, .cpp:250) - kept on purpose per task history (tick-fill revisit), plus `MsgHistoricalTick` dispatch.
- `ReplayWindow::DrawCursorLine` (.h:236, .cpp:790)
- `ScannerWindow::FmtChange`, `FmtPrice` (.h:202-203), `PushRowColor` / `PopRowColor` (.h:209-210; both bodies are empty `{}`, .cpp:1504-1505)
- `PortfolioWindow::FmtDate`, `FmtShares` (.h:266-267), `PortfolioWindow::SetBaseCurrency` (.h:89; a comment at `PortfolioWindow.cpp:145` mentions a caller that does not exist)
- `WatchlistWindow::PopRowTint` (.h:173; empty body, .cpp:567 - `PushRowTint` is called at .cpp:1056 but is the only half used), `WatchlistWindow::deserialize` (.h:89, .cpp:635)
- `NotificationService::assetDir()` (.h:163) and `schedulePlays` (.h:177, declared, no definition at all)
- Data members with a single mention: `ChartWindow::m_chartHeightRatio` (.h:403), `ScannerWindow::m_uniform` (.h:176).
- Doc/comment only: `ScannerWindow.h:35` mentions `reqScannerParameters()`, which does not exist in `IBKRClient`.
- Duplicate call: `PushWorkingComboLegs()` is called twice in `onOpenOrder` (`main.cpp` ~5058 and ~5063) - harmless.

## C. Doc drift

Verified against the code; "now" = what the code does.

### architecture.md
1. L569 (and the same paragraph "all same-expiry"): "the ticket accumulates up to `kMaxLegs` (6) legs - all sharing one expiry". Now: each cart leg carries its own expiry (`AdjustLegExpiry`, cross-expiry templates, calendars/diagonals); `AddOrToggleLeg` has no same-expiry guard. Fix: "up to `kMaxLegs` (6) legs, each with its own expiry".
2. L577-588: "exempt from the same-expiry guard" and "Cross-expiry (calendar/diagonal) and templates are later phases". Now: both landed (1.4.15-1.4.24). Fix: delete both phrases.
3. L766: "capped at `kMaxOptionSubs = 60`". Now `OptionsChainWindow.h:345` = 72. Fix: "= 72".
4. L67 (reqId list): "see \"Chart mktId/histId/extId rotation\" below" - no such section exists. Fix: "see the rotating-pool helpers `AllocChartMktId` / `AllocChartHistId` / `AllocChartExtId` in main.cpp".
5. L66-72 reqId list: "Scanner scan: 1000,1100,...,1900 (+99 each) · mkt: 800,812,...,908 (+12 each)". Now `ScannerMktBase = 17000 + idx*25` (main.cpp:1204), plus `ScannerHistBase = 18000 + idx*25` (:1208) and `ScannerFundBase = 19000 + idx*25` (:1214), none of which the doc lists. Fix: "mkt: 17000+idx*25 (25 slots), history 18000+idx*25, fundamentals 19000+idx*25".
6. reqId list omits live ranges: `8002` + `8003-8008` (Orders price-ladder quotes, `OrdersWindow.h:67-68`), `8200-8299` (pool at main.cpp:4892), `11000-11999` is listed only in the Replay section. Fix: add the first two.
7. L135-145 (scanner block), "14 column-visibility toggles (`COL_COMPANY` ... `COL_SPARKLINE`)". Now removed (`ScannerWindow.cpp:354` comment: column state is in imgui.ini). Also the `FLT_BUF_*` list: code writes `FLT_BUF_MIN_PRICE/MAX_PRICE/MIN_CHG/MAX_CHG/MIN_VOL/SECTOR` and also `SCAN_GROUP`. Fix: drop the COL_ sentence; add `SCAN_GROUP`; list the six FLT_BUF keys.
8. Singleton-settings paragraph: "7 `PORT_COL_*` column-visibility toggles" - removed (`PortfolioWindow.cpp:70`). Now also saved: `PORT_GROUP`, `PORT_GROUP_STRATEGIES`, `PORT_UNGROUP`, `PORT_MERGE`, `PORT_LINK`. "`ORD_FILTER_DATE` (history filter state)" - not serialised (`OrdersWindow.cpp:34-40`); now `ORD_FILTER_ACCOUNT` and `ORD_GROUP`. WshCalendar also saves `WSH_OPEN`. Fix accordingly.
9. Trading-settings paragraph: keys listed omit `TW_GROUP` and `AUTO_FOLLOW` (`TradingWindow.cpp:415-430`).
10. L1049 (TradingWindow DOM section): "Clicking an ask price places a BUY limit, clicking a bid price places a SELL limit" on an `InvisibleButton` "in column 2". Now two-sided: left (Bid) column = BUY, right (Ask) column = SELL at any row (task-history 1.3.36; README already says so). Fix: "Click-to-Trade: the left (Bid) column of any row places a BUY limit at that price, the right (Ask) column a SELL limit."
11. L3-26 directory tree: lists 5 test files (real: 15), `services/` as 4 files (real: 19 headers + 3 .cpp), `ui/` as `UiScale.h` + `windows/` (real: AppIcon, BracketChildForm, ChartRender, DatePicker, MessagePopup, NotificationOverlay, RecentSymbols, SymbolSearch, TradingShortcuts, WshData...), models list omits ContractSpec/OptionData/ReplayData/SmartRouting/WatchlistData; `platform/` (MacAppIcon.mm), `tools/`, `resources/`, `assets/`, `third_party/`, `.github/` absent. Fix: regenerate from `find src tests -maxdepth 3`.
12. L45-47: "Chart, Order Book, Scanner, and News support up to 10 instances ... Singleton windows (Portfolio, Orders)". Now Watchlist and Replay are multi-instance too; singletons also include Options Chain, Strategy Analysis, WSH Calendar, Notifications, Price Alerts.
13. `[setup]` "(17 cases / 80 assertions)" in "Setup Suggestions" section: now 30 cases / 107 assertions (`tests-core "[setup]"`).
14. Main.cpp comment (not doc, same drift): `main.cpp:1109-1116` comment block still says "Scanner mkt: 800,812..." and "News: 400-420(conId), 500-520(hist), 600-699(art), 700-759(mkt)"; real News layout is the 2000-3699 block below it (main.cpp:1222-1229). `main.cpp:3321` says "chart pools stop at 16999" (scanner pools 17000-19999 and option pool 22000 exist).

### testing.md
15. L95: "`assets/sounds/{tones,voice}/*.wav` (26 WAVs)". Now 14 tones + 14 voice = 28.
16. `[replay]` "207 cases / 987 assertions": now 76 cases / 192 assertions.
17. `[state-io]` "19 cases / 77 assertions": now 23 / 101 (`[state-io]`).
18. `[strategy]` "55 cases": now 73 cases / 305 assertions.
19. tests-core row says "Links: Catch2 only"; `tests/CMakeLists.txt` also compiles `src/core/services/NotificationService.cpp` with `IBKR_NOTIFICATIONS_NO_AUDIO` into it, and `test_notification_service.cpp` has no entry in "What Is Tested". Fix: add a bullet for it; note the extra source.
20. Verified OK: file list of `tests-core` (14 files) and `tests-ibkr` matches `tests/CMakeLists.txt`; `[vwap]` 6/84, `[volume-profile]` 12/72, `[order-impact]` 17/74 match.

### build.md
21. L55 and L60: "Generate the 11 alert tones" / "11 voice phrases". Now 14 each (`gen_tones.cpp` has 14 builders, `voice_phrases.txt` 14 entries, 14 WAVs per dir). Also L56-58 "Force-overwrite all 11". Same "11" in `tools/README.md:16,35`.
22. L13 `DISPLAY=:0` vs L108 (":0 is the one available... `:1` does not exist") are consistent with each other and with the machine (`/tmp/.X11-unix/X0`); the contradiction is in the other two files (see 24).

### ibkr-api.md
23. L5 "Version: 1037.02": `vendor/TWSAPI_VERSION` says `TWSAPI_VERSION=10.49.01`.
24. Connection ports table: "Live | Gateway 4001 | TWS 7497; Paper | 4002 | 7496". Code (`main.cpp:1266-1271`) and README: TWS Live = 7496, TWS Paper = 7497. Fix: Live: Gateway 4001, TWS 7496; Paper: Gateway 4002, TWS 7497.
25. L11: "`IBKRClient.h` forward-declares `::Bar`": the header includes the IB API headers (`DefaultEWrapper.h`, `EClientSocket.h`, lines 17-20) and uses `::Bar` directly; there is no forward declaration. Fix: "`IBKRClient.h` includes the IB headers, so `::Bar` is visible inside `core::services`".
26. Protobuf bullets: "`protobufUnix/*.pb.*` were regenerated with protoc 3.21.12 (originals were 3.12)" and "protoc binary was downloaded to /tmp/protoc/": `CMakeLists.txt:188-191` says the protobuf output is git-ignored and regenerated at build time (by CI, `.github/workflows/build.yml:44,88`); locally the files are present from an earlier run. Fix: describe that.

### vulkan-imgui.md
27. L37 "`DISPLAY=:1` on this Linux machine": only `:0` exists (`ls /tmp/.X11-unix` = `X0`); build.md says `:1` fails.

### README.md
28. L133 "`DISPLAY=:1 ./build/ibkr-trading-app`" for headless/WSL: same as 27 (on this machine `:0`). Left as is if the virtual-display example is generic, but it contradicts build.md.
29. L193-199 "CMake build (and each CI job) runs `protoc` over `IBJts/source/proto` automatically": `CMakeLists.txt` contains no protoc step (only `find_package(protobuf)` and a comment at 188-191); only the CI workflow runs protoc. A fresh local checkout without generated `protobufUnix/*.pb.*` will not build. Fix: "CI runs protoc; locally run it by hand (see ...)".
30. Verified OK (no change): ports table, presets table (matches `kBuiltinPresets`), 22 watchlist columns claim, "10 to 24 px", keyboard shortcut defaults B/S/C/U.

### Header comments
31. `OptionsChainWindow.h:26-34` reqId layout: "21001-21099 option reqContractDetails (leg conId resolution)": now 21001 underlying details, 21002 underlying quote, 21003 strike enum, 21004-21009 IV history, 21010-21015 leg conIds, 21100-21199 combo-leg lookup, 21200 analysis pin underlying. Fix: copy the line from architecture.md L81.
32. `OptionsChainWindow.h:347` "N-leg cart (Phase A: all legs share one expiry)" and `OptionsChainWindow.cpp:32` "Same-expiry only" - stale (cross-expiry legs exist).
33. `ScannerWindow.h:35` mentions `reqScannerParameters()` which no longer exists.
34. `PortfolioWindow.cpp:145` mentions `SetBaseCurrency()` "(called from reqAccountSummary)": nothing calls it.

Total verified mismatches: 31 (architecture.md 14 incl. main.cpp comment, testing.md 5, build.md/tools README 1, ibkr-api.md 4, vulkan-imgui.md 1, README 2, header comments 4). Items 20, 22 and 30 are confirmations, not mismatches.

## D. Riskiest untested decision logic in `src/main.cpp` (top 10)

Pattern note: existing pure helpers are in `src/core/services/*.h` with tests; none of the logic below has one.

1. **`onError` order-error path, `main.cpp:5762-6073`.** One 300-line lambda: what-if routing, news-topic routing, hold warnings (399/404/2109/2148/10311), 2000-2999 warnings, 135/10147 cancel-not-found, refused modify, reject vs reject-after-cancel, toast allowlist. Can go wrong: ordering of branches (a 10147 on a modify is taken by the cancel-not-found branch before the refused-modify branch; a reject code on an id that is also a chart/news reqId, since order ids come from `nextValidId` and can be low after a Gateway reinstall, would flip an unrelated order to Rejected via `g_liveOrders.find(reqId)`). Pure helper: yes - `ClassifyOrderError(code, status, rejectReason.empty(), inModifyFlight, isHoldWarning...) -> enum {Hold, Warn, NotFound, ModifyRefused, Reject, BackfillReason, Ignore}` is a table of ints and enums, no IB/UI needed; the side effects stay in main.cpp.
2. **Bracket entry/child submission, `main.cpp:3566-3620` (`OnBracketSubmit`), `3779-` (`OnAttachBracket`), `3497-` (`OnProtectPosition`), `5225-5295` (stock bracket on first fill).** Parent transmit=false + last child transmit=true: if the connection drops between `PlaceOrder` calls the parent is left untransmitted with no children and nothing notices. The stock bracket handler places STP+TP for the full `p.qty` on the **first** fill callback, so a partially filled entry gets full-size exits. Child `transmit`/`parentId`/OCA stamping is duplicated across 4 sites. Pure helper: yes for the stamping (`StampBracketChain(entryId, children, firstChildId) -> vector<Order>` returning ids/parent/oca/transmit; partial-fill sizing as `ExitQtyForFill(p.qty, filledSoFar)`); the `PlaceOrder` calls stay.
3. **Combo-link keep set, `main.cpp:868-884` (`PushWorkingComboLegs`) with `g_comboFilledAt`/`kComboFillGraceSec`, `340-348` (`ComboLinkLegs`), and the 3 `RecordComboLink` call sites.** Decides which saved links survive a prune. Wrong: a combo filled while the app is closed has no `g_comboFilledAt`, so after the next restart it is treated as expired once `g_openOrdersLoaded` is true (the live order isn't in `g_liveOrders` any more) - the link is pruned as soon as positions load, before they might all be present; grace window is wall-clock based. Pure helper: yes - `WorkingComboLegs(orders, filledAt, now, grace)` over a plain struct (secType, status, legs, id); `FormatConIdSets` already takes the `keep` set.
4. **Open-order refresh reconciliation, `main.cpp:1734-1750` + `onOpenOrder` 5037-5064 + `onOpenOrderEnd` 5066-5091** (`g_openOrderRefresh`, `g_refreshSeenExternal`, `g_modifiedDuringRefresh`, `ModifyAnswered`). Can go wrong: an external order seen in the 30 s window is dropped if the reply is split across the 30 s give-up; an orderStatus-only update for an external order during the re-read does not mark it seen; an error mid-refresh leaves `g_openOrderRefresh` true until the 30 s timeout. Pure helper: yes - `ExternalOrdersGone(liveExternalIds, seenIds)` is trivial, but the state machine (`RefreshState{inFlight, sentAt}` + `ShouldSendRefresh(now)` + `OnReply`) is testable as a small struct.
5. **Account filtering, `main.cpp:4849-4860` (`onAccountSummary`), `4830-` (`onAccountValue`), `4939-5036` (`onPositionData`/`onPortfolioUpdate`).** The filter is `account != g_selectedAccount`; an empty `pos.account` passes (`!pos.account.empty() &&`), an empty `g_selectedAccount` (before selection) drops every summary row. Orders are **not** filtered at all: `RecomputeUnguardedPositions` (`886-935`) matches live orders to positions by symbol only, so another account's working stop counts as protection for the selected account's position and silences the unguarded-position warning. Pure helper: yes - `AccountMatches(rowAccount, selected)` plus an account argument on the unguarded check (`ComputeUnguarded(positions, orders, account)`); both are plain-data functions.
6. **`RecomputeUnguardedPositions`, `main.cpp:886-956`.** Edge-triggered notification on the set difference, symbol-only match, partial-coverage treated as guarded, option legs excluded via `IsSymbolLevelPosition`. Pure helper: yes - input `vector<Position>`, `vector<Order>`, previous set; output unguarded list; the notify stays.
7. **Price-alert checking, `main.cpp:691-790` (`SyncAlertFeeds`, `CheckPriceAlerts`).** `AlertTriggered` is pure and tested, but the loop erasing while iterating, once-only firing, per-symbol feed diffing (`SyncAlertFeeds` cancel/subscribe set) and the `g_alertLast` seed are not. Wrong: duplicate ticks for two feeds on one symbol fire twice (erase happens after the notify, so only if two callbacks interleave - they do not, single UI thread); an alert added while feeds are mid-rotation might get no feed. Pure helper: yes - `FireAlerts(alerts, symbol, last) -> {fired, remaining}` and `DiffAlertFeeds(alerts, liveFeeds)` (same shape as the already-tested `DiffSubscriptions`).
8. **`ApplyOrderModification` and chart drag-modify, `main.cpp:1793-1815`, `OnModifyOrder` (~1940-1990), `g_modifyInFlight`/`MarkModifyInFlight`/`ModifyAnswered`.** Copies 5 fields from the edit onto `g_liveOrders` and re-sends the whole order; re-places a Filled/Cancelled order if the blotter shows it (no status check here, only in the UI); an in-flight mark that never gets an answer turns the next unrelated error into "Change not accepted". Pure helper: partly - `MergeEdit(live, edited) -> Order` yes (and `OrderEdit.h` is the natural home); the in-flight state machine yes.
9. **`MaybeNotifyOrderAccepted` / `g_pendingLocalAccept` / `CheckUnacknowledgedOrders`, `main.cpp:460-500, 1770-1792`, set touched at 16 sites.** Dedupe by set membership across onOpenOrder / onOrderStatus / onError(135, 10147) / terminal states: a path that forgets to erase leaves an id forever (bounded by order count, so small) and `CheckUnacknowledgedOrders` iterates it every frame; `s_warned` static set is never cleared across reconnects. Pure helper: yes - `AcceptTracker{add, onAck, onTerminal, pendingOlderThan(now, 5s)}` testable without IB.
10. **`FinishConnect` reconnect re-subscribe and `DrainStyleSwitchQueue`, `main.cpp:4002-4330`, `1665-1733`.** Decides which subscriptions and which pending bars to clear on reconnect, plus latest-wins queue with 1 s throttle; a missed reset leaves stale `pendingBars`/`extStreamActive` (the cause of past contamination bugs). Pure helper: partly - the queue (`StyleSwitchQueue::push(chartIdx,...)` latest-wins, `popIfDue(now)`) yes; the reconnect re-subscribe loop no, it is IB calls on live window objects (it would need an interface to fake).

## E. ASan/UBSan `tests-ibkr` (Debug, `-fsanitize=address,undefined`, `detect_leaks=1`)

Clean: exit 0, "All tests passed (120 assertions in 32 test cases)", 0 AddressSanitizer / UBSan / LeakSanitizer reports (grep count 0 of `ERROR: AddressSanitizer|runtime error|LeakSanitizer` in `E.out`). Output saved at `/tmp/claude-1000/-home-nedievas-development-ibkr-trading-app/a2603aef-0ea5-4559-a7b9-a48dac393fb6/scratchpad/wp8/E.out`. No vendor frames reported, no suppression file needed.
