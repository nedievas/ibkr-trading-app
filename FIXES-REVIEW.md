# Bug-fix & review pass — 16 fixes

Linux build/test bring-up + live testing against a Paper Gateway + a static review
of the highest-risk subsystems. 16 fixes across 10 files.
Build clean; **333/333 ctest pass**; `tests-core` clean under ASan+UBSan; key fixes
live-verified against IB Gateway (paper, `172.24.0.1:4002`).

> **Note:** local changes under `twsapi_macunix.1037.02/` (regenerated protobuf +
> a `CommonDefs.h` `OrderId`/`TickerId` typedef shim) are **build scaffolding only**
> to compile without the encrypted TWS blob and must **not** be committed — CI
> decrypts the real 1037.02 API. Only the `src/` changes below are the deliverable.

---

## Serious (crash / corruption / financial)

### 1. Concurrent socket writes — data race on `EClientSocket`
`IBKRClient` writes to the single IB socket from **two threads**: the send thread
(`PlaceOrder`/`CancelOrder`/display-group via `PostSend`) and the UI thread (38
`Req*`/`Cancel*` methods calling `m_client->…` directly). The socket's send buffer
is not safe for concurrent encoding — interleaved writes corrupt the outgoing wire
message (intermittent IB errors/disconnects when an order is placed while a chart
subscribes).
**Fix:** `std::mutex m_socketMutex` — a method-level `lock_guard` in all 38 direct
methods + a lock around the `SendLoop` batch drain. No nested locked calls (verified)
→ no deadlock. Live-verified: concurrent burst (6× mktData + 6× contractDetails +
hist + send-thread `QueryDisplayGroups` + scanner) → no hang, no errors.
`src/core/services/IBKRClient.h` (member), `src/core/services/IBKRClient.cpp` (SendLoop + 38 methods).

### 2. Use-after-free on disconnect
`onConnectionChanged` deleted `g_IBClient` **inside** the object's own
`ProcessMessages()` dispatch — freeing the object whose method is on the stack.
Any message batched after the disconnect, and the `m_queue` re-append, then run on
freed memory (timing-dependent crash).
**Fix:** defer the delete via `g_clientDeletePending`, performed in the main loop
after `ProcessMessages()` returns. `src/main.cpp` (flag ~L182, callback ~L2739, main-loop teardown ~L5452, `Disconnect()` reset ~L3861).

### 3. Duplicate order id (IB error 103)
`onOpenOrder` — which fires for every order `ReqAllOpenOrders` returns on connect
(other client-ids / prior sessions / TWS-placed, ids can exceed `nextValidId`) —
never advanced the id counter, and the `TradingWindow` had a *separate* counter.
So the app allocated an id IB already held. **Live-confirmed:** account had 32 open
orders up to id 31750 while `nextValidId=5013` → order 5013 collided.
**Fix:** `EnsureNextOrderIdAtLeast(int)` — monotonic bump + resync of all
TradingWindow counters; called from `onOpenOrder(id+1)` and `onNextValidId(id)`
(also fixes reconnect regressing the counter). `src/main.cpp` (~L2684, ~L2947, ~L3793).

### 4. LAST-price `-1` sentinel corrupting P&L
IB sends `tickPrice` field 4 = `-1` for "no last trade". The chart / NBBO / `m_lastPrice`
paths guard `>0`, but case 4 fed `-1` straight into position `marketPrice` (wrong P&L)
and the trading-window mid. **Fix:** `if (price <= 0.0) break;` at the top of case 4.
`src/main.cpp` (~L2947, onTickPrice case 4).

---

## Reliability / correctness

### 5. Historical extend (pan-left) — `extId` not rotated
`OnExtendHistory` cancelled + re-issued on the **same** `ce.extId`, unlike
`ReqChartData` which rotates. After a request's `done`, its trailing in-transit bars
can land on the reused id while `extStreamActive` is true again → merged into the new
prepend (stale-bar contamination). **Fix:** `ce.extId = AllocChartExtId()` before
re-issue. `src/main.cpp` (~L1189).

### 6. `PrependHistoricalData` — missing symbol backstop
Unlike `SetHistoricalData`, it didn't reject `older.symbol != m_symbol`, so a stale
extend racing a symbol switch could prepend cross-symbol bars (AAPL into /ES).
**Fix:** symbol guard at the top, leaving `m_loadingMore=false` so a fresh pan re-fires.
Live-verified extend still returns strictly-older bars (21 older, 0 overlap).
`src/ui/windows/ChartWindow.cpp` (~L455).

### 7. Replay windows never restored on restart
`LoadReplayWindowsFromFile()` was defined but never called, while all sibling loaders
run in `FinishConnect`. **Fix:** call it after `LoadSingletonSettingsFromFile()`, and
add the Task-#86 spawn pre-pass so instances >0 restore too. **Live-verified:** seeded
3-instance config → all 3 (AAPL/MSFT/TSLA) restored. `src/main.cpp` (~L2076, ~L2571).

### 8. Watchlist Bid/Ask showed `-1.00`
`WatchlistWindow::OnTickPrice` stored the raw `-1` no-data sentinel into every price
field; the cell formatter only blanks exact `0.0`, so it rendered `-1.00`. Watchlist
ticks route straight here, bypassing the guarded dispatcher. **Fix:** `if (price <= 0.0)
return;` at the top → cells show a clean `--`. `src/ui/windows/WatchlistWindow.cpp` (~L367).

### 9. Multi-account switch — stale positions/PnL (FA/institutional only)
The account dropdown re-subscribed without clearing the old account's data;
`reqAccountUpdates(true,new)` only *adds* new positions → old ones linger and mix.
**Fix:** on switch, `g_positions.clear()` + new `PortfolioWindow::ResetAccountData()`
+ `RecomputeUnguardedPositions()` + cancel old per-position PnL singles & clear the
maps. Account-level PnL self-heals via the per-frame re-subscribe.
`src/main.cpp` (~L4893), `src/ui/windows/PortfolioWindow.{h,cpp}`.
*(Cannot be exercised on a single-account login; verified by build + tests + inspection.)*

### 10. L2 depth insert on negative index
`OnDepthUpdate` guarded `pos<0` for update/delete but not the `op==0` insert, where
`levels.insert(begin()+pos)` on negative `pos` is UB. **Fix:** uniform `if (pos<0) return;`.
`src/ui/windows/TradingWindow.cpp` (~L40).

### 11. WSH date-picker button ID instability
A stable-id table (`kBtnId`) was declared but unused; the button used its changing
visible label as its ImGui id. **Fix:** append the `##id` suffix.
`src/ui/windows/WshCalendarWindow.cpp` (~L181).

---

## Noise / performance

### 12. Per-frame stderr spam
Three ungated `fprintf(stderr)+fflush` traces fired every frame (`ProcessMessages`
start/done, `RenderTradingUI enter`) — ~180 flushed writes/sec at 60fps. **Removed.**
Kept the conditional ChartWindow anomaly diagnostics. `src/core/services/IBKRClient.cpp`, `src/main.cpp`.

### 13. Empty-symbol autocomplete rows
IB `symbolSamples` returns BOND contracts with an empty symbol; selecting one fired
requests on an empty symbol. **Fix:** filter `r.symbol.empty()` at the handler choke
point (covers all 4 search widgets). `src/main.cpp` (onSymbolSamples).

### 14. `marketDataType` log flood
Printed once per subscribed ticker (dozens on a busy desk), no functional use.
**Fix:** made the callback a no-op. `src/core/services/IBKRClient.cpp`.

### 15. Multi-instance replay restore (spawn pre-pass)
Bundled with #7 — the restore now spawns replay instances beyond 0 so G2/G3 replay
windows come back. `src/main.cpp` (~L2076).

### 16. Portfolio table/cards clipped at non-default font size
The positions & trade-history tables set raw pixel column widths (`72.f`, `88.f`, …)
and the summary cards a raw `62.f` height, instead of the font-scale-aware `em()` the
rest of the app mandates. At Medium/Large font (`io.FontGlobalScale` 1.0/1.5×) the text
grows but the columns/cards don't → text overflows/clips in cells. **Fix:** wrap the 14
column widths + card height/gap in `em()`. `src/ui/windows/PortfolioWindow.cpp`.
*(Likely the padding issue reported — pending screenshot confirmation.)*

---

## Reviewed and confirmed sound (no change)
- **Bracket OCA fill handler** — `erase`s the pending entry after submitting STP+TP, so
  entry partial-fills don't double-submit; legs keyed only by entry id.
- **Window lifecycle / memory** — balanced new/delete; client torn down before windows
  on all paths; singletons delete-first-new + nulled; spawns bounded by `kMaxMultiWin`;
  post-teardown cancels null-check the client. Hide/show window model is intentional & bounded.
- **Historical live-append** (`UpdateLiveBar`) uses the sequential filtered index; reset
  flags on symbol/TF change; extend trigger guards `!m_loadingMore`.

## Verified live (paper gateway)
connect + nextValidId, contract details, historical bars + extend, market-data ticks,
positions (41), account summary, symbol search, real-time P&L, scanner, tick-by-tick,
order lifecycle (place → working → cancel, OCA pairing), concurrent-send stress.
