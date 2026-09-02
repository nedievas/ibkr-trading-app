# Plan: Options Chain Window

**Status:** Not started — planning/handoff only. No code written yet.
**Branch:** `feature/options-chain` (branched from `fix/stability-review` @ `cb09e33`, pushed to `fork`). Kept isolated from the stability-review work so the two don't get mixed in one PR.
**Owner:** Jose (nedievas)
**Goal:** Add an Options Chain window — pick an underlying, see expirations × strikes with bid/ask/last/volume/OI/IV/greeks for calls and puts, and place option orders from it. This is a **from-scratch feature**: grepping the codebase for `OPT`/`reqSecDefOptParams`/`greeks`/`strike` turns up nothing — there is no existing options infrastructure to extend, only patterns to imitate (multi-instance window lifecycle, reqId pooling, state persistence).

This file is the source of truth across sessions — update **Status** and the per-task checkboxes as work progresses. A new session picking this up should read this file plus `.claude/rules/*.md` (already loaded via `CLAUDE.md`) and nothing else.

---

## 1. Why a separate branch

`fix/stability-review` (now merged to `main`-bound history as of v1.2.0, commit `cb09e33`) is a long chain of live-testing bug fixes and persistence work. Options chain is a large, self-contained new feature with its own IB API surface, new data models, and a new window — it doesn't belong in that stream and would make the eventual PR hard to review. `feature/options-chain` branches from `cb09e33` (tip of `fix/stability-review` at hand-off time) so it inherits all the stability fixes but stays isolated for its own PR into `main`.

Before merging, rebase or merge `main` in if `fix/stability-review` has landed there and diverged further.

## 2. What already exists to imitate

No options code exists, but these patterns are established and should be reused rather than reinvented:

| Pattern | Reference |
|---|---|
| Multi-instance window lifecycle (`Entry` struct, `SpawnXWindow(idx)`, reqId pooling, rotation-on-cancel) | `ChartEntry` / `SpawnChartWindow` in `main.cpp`; see `architecture.md` "Multi-Instance Windows" |
| Pure-logic extraction for testability | `core/services/ChartAnalysis.h` — no IB/ImGui deps, full Catch2 coverage |
| Symbol-search autocomplete | `src/ui/SymbolSearch.h` — reuse directly for the underlying-symbol field |
| Group sync (click symbol → broadcast to chart/DOM/replay) | `WindowGroup.h` + `BroadcastGroupSymbol`; Portfolio's `OnBroadcastSymbol` (just landed in v1.2.0) is the freshest example |
| Per-window settings persistence (hash-diff file) | `ChartWindow::SerializeSettings/ApplySettings` + `chart-settings.cfg` pattern in `main.cpp` |
| EWrapper bridge → UI thread via `std::variant` queue | `IBKRClient.{h,cpp}` `IBMessage` variant + `ProcessMessages()` |
| Order submission via `core::Order` struct | `OrderData.h` — options orders reuse this; only the **contract** construction differs (secType=OPT + expiry/strike/right/multiplier vs the stock's plain symbol) |

## 3. IB API surface needed (new)

None of this exists in `IBKRClient` yet. Reference: `vendor/twsapi/IBJts/source/cppclient/client/`.

1. **`reqSecDefOptParams(reqId, underlyingSymbol, futFopExchange="", underlyingSecType, underlyingConId)`** → EWrapper callback `securityDefinitionOptionalParameter(reqId, exchange, underlyingConId, tradingClass, multiplier, expirations, strikes)` fired once per exchange, then `securityDefinitionOptionalParameterEnd(reqId)`. This is how you discover the available expirations + strikes for a symbol — needs the underlying's `conId` first (already obtainable via the existing `reqContractDetails` flow used by Chart/Watchlist).
2. **Option contract construction**: `Contract{symbol, secType="OPT", exchange="SMART", currency="USD", lastTradeDateOrContractMonth=<YYYYMMDD>, strike=<double>, right="C"|"P", multiplier=<from step 1, usually "100">, tradingClass=<from step 1>}`.
3. **`reqMktData(reqId, optionContract, genericTicks="100,101,104,106", snapshot=false, ...)`** → ticks:
   - `tickPrice`/`tickSize` for bid/ask/last/volume (existing overrides, already wired for stocks — same path works, just routed by reqId into an option-chain-owned map instead of a chart/trading map).
   - `tickOptionComputation(reqId, tickType, tickAttrib, impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, undPrice)` — **new EWrapper override needed**. `tickType` distinguishes bid-computation (10), ask-computation (11), last-computation (12), model-computation (13, generic tick 106) — model is usually what you want for the chain display (IV/greeks that don't jitter with every bid/ask flicker).
   - `tickGeneric(reqId, tickType, value)` for genericTick 101 (option open interest) — already have a generic `tickGeneric` override for other features; extend its routing.
4. **Volume of subscriptions is the real design constraint.** A single expiration with 40 strikes × 2 (calls+puts) = 80 live `reqMktData` subscriptions. IB cabs concurrent market data lines (default 100 for most accounts, `reqMarketDataType` unaffected). Two viable strategies:
   - **(a) Snapshot-refresh**: use `snapshot=true` on a timer (e.g. every 2–3s) instead of a persistent stream per strike — trades live-ness for subscription-count safety. Simpler, avoids exhausting the account's market data line limit if the user opens multiple chains.
   - **(b) Streaming with a visible-rows cap**: only stream strikes currently scrolled into view ± N rows, matching how e.g. TWS's own chain window throttles. More live-feeling, much more bookkeeping (subscribe/unsubscribe on scroll).
   - **Recommendation: start with (a) snapshot-refresh**, default OFF (user must click "Load Chain" and optionally toggle "Live Refresh"), matching the account's own market-data budget rather than assuming it. Revisit (b) as a v2 if users want tighter live-ness once the plumbing exists.
5. **Order placement**: reuses `IBKRClient::PlaceOrder(const core::Order&)` unchanged — the option contract fields (`secType`, `strike`, `right`, `lastTradeDateOrContractMonth`, `multiplier`, `tradingClass`) need to be added to `core::Order` (currently stock/futures-shaped: `symbol`, `secType` already exists per the futures work, but strike/right/multiplier/tradingClass don't). Check `OrderData.h` current fields before adding — the futures /ES /NQ work may have already added a subset (e.g. `tradingClass` for the Dec contract) that can be reused rather than duplicated.

## 4. New data model

`src/core/models/OptionData.h` (new, POD-only per `cpp-style.md`):

```cpp
struct OptionContractKey { std::string symbol; std::string expiry; double strike; char right; }; // 'C'/'P'
struct OptionQuote {
    OptionContractKey key;
    int conId = 0;
    double bid=0, ask=0, last=0, volume=0, openInterest=0;
    double impliedVol=0, delta=0, gamma=0, theta=0, vega=0, undPrice=0;
    int reqId = 0;
    bool subscribed = false;
};
struct OptionExpiry { std::string date; std::vector<double> strikes; };
struct OptionChainMeta { std::string symbol; int underlyingConId=0; std::string tradingClass; std::string multiplier;
                          std::vector<OptionExpiry> expirations; };
```

Keep this POD-first — no IB calls, no ImGui, matches `PortfolioData.h`/`ScannerData.h` conventions.

## 5. New service surface

`IBKRClient.h`/`.cpp` additions:
- `MsgSecDefOptParams { int reqId; std::string exchange, tradingClass, multiplier; std::vector<std::string> expirations; std::vector<double> strikes; }` + `MsgSecDefOptParamsEnd { int reqId; }` variants on `IBMessage`.
- `MsgTickOptionComputation { int reqId, tickType; double impliedVol, delta, optPrice, pvDividend, gamma, vega, theta, undPrice; }` variant.
- `reqSecDefOptParams(reqId, symbol, futFopExchange, secType, conId)`, `securityDefinitionOptionalParameter(...)` override, `securityDefinitionOptionalParameterEnd(...)` override.
- `tickOptionComputation(...)` override → pushes `MsgTickOptionComputation`.
- Existing `reqMktData`/`cancelMarketData`/`tickPrice`/`tickSize`/`tickGeneric` are reused as-is — just need new call sites and routing keyed by the option window's reqId pool.

## 6. UI: `src/ui/windows/OptionsChainWindow.{h,cpp}` (new)

Multi-instance, following the `ChartEntry`/`SpawnChartWindow` pattern (likely cap at 4–10 instances — decide based on how much per-instance reqId space is available, see §7).

Layout sketch:
- **Toolbar**: group picker (leftmost, per convention) · symbol input (reuse `SymbolSearch.h`) · expiration combo (populated from `OptionChainMeta.expirations` once loaded) · "Load Chain" button · "Live Refresh" toggle (off by default per §3.4) + refresh-interval input · strike-range filter (e.g. ± N strikes from ATM, or min/max) · Greeks columns toggle.
- **Table**: two mirrored halves (Calls | Strike | Puts), one row per strike for the selected expiration. Columns per side: Bid, Ask, Last, Volume, OI, IV, Delta (Gamma/Theta/Vega behind a "more greeks" column-visibility toggle, same pattern as Scanner's `kColDefs[]`/`m_colEnabled[]`). ATM strike row highlighted (compare strike to underlying `undPrice` from the most recent tick, or the chart's last close if no option tick has arrived yet).
- **Row click** → stages an order ticket (reuses the existing `core::Order` + confirmation-popup pattern from ChartWindow/TradingWindow) for that specific contract, defaulting side to BUY on the clicked half (calls vs puts) and type to Limit @ mid.
- **Order Impact badge**: if the user already holds a position in that exact option contract, reuse `ComputeOrderImpact` (already generic over qty/avgCost/fillPrice — no option-specific change needed there).

## 7. ReqId layout (propose, avoid collisions)

Per `architecture.md`'s existing allocation table, the next free block after `9001–9999` (P&L singles) going up is open. Propose:
- `20000–20099`: option-chain `reqSecDefOptParams` (one per instance in-flight, cancel-before-reissue like Symbol Search's 8000)
- `21000–21999`: option-chain per-instance market data pool (rotating, mirrors the `AllocChartMktId`-style 1000-slot pool pattern used for chart mkt/hist/ext ids — necessary here too, since switching symbol/expiration mid-session will hit the exact same stale-tick race documented in the Phase 15 "stale-bar contamination" fixes)
- Update `architecture.md`'s reqId table once real ids are chosen.

## 8. Persistence

New `option-chain-settings.cfg` (or fold into a generalized per-instance file if one is added later) via the existing `state-io.h` hash-diff pattern: `INSTANCE:N` blocks with `SYMBOL`, `EXPIRY`, `GROUP`, `LIVE_REFRESH`, `REFRESH_SEC`, `STRIKE_RANGE`, per-column visibility toggles. Mirrors `ScannerWindow::SerializeSettings`/`ApplySettings` almost exactly — copy that shape.

Window open/closed persistence, spawn pre-pass on load, closed-entry save guard — all follow the now-established Phase 17 pattern (`state-persistence.md`) exactly; no new design needed there, just apply the template.

## 9. Testing strategy

Pure-logic pieces belong in `tests-core` (no IB/ImGui dep), same as `ChartAnalysis.h`:
- ATM-strike detection / moneyness classification given `(underlyingPrice, strikes[])`.
- Strike-range filtering (± N strikes from ATM).
- Any chain-organizing helper (grouping raw strike/expiry lists into `OptionExpiry` structs, sorting, dedup) — if this logic is nontrivial, extract it as a free function in a new `core::services::OptionChain.h` rather than inlining it in the window, so it's testable the same way `ChartAnalysis.h` is.
- `tests-ibkr`: dispatch tests for the new `MsgSecDefOptParams`/`MsgSecDefOptParamsEnd`/`MsgTickOptionComputation` variants, following `test_ibkr_queue.cpp`'s existing per-message-type pattern exactly (the `std::visit` in `ProcessMessages()` is exhaustive — untested variants silently no-op, per the existing "Adding New Tests" rule in `testing.md`).

## 10. Suggested task breakdown

- **Task A** — `OptionData.h` model + `IBKRClient` wiring (reqSecDefOptParams round-trip, tickOptionComputation) + `tests-ibkr` dispatch tests. No UI yet — verify against a live paper Gateway that expirations/strikes/greeks arrive correctly (log to stderr).
- **Task B** — `OptionsChainWindow` shell: toolbar, expiration/symbol selection, static table render of expirations+strikes (no live data yet, just structure) + multi-instance plumbing (`OptionsChainEntry`, `SpawnOptionsChainWindow`, reqId pool, Windows-menu entry).
- **Task C** — Live market data: snapshot-refresh subscriptions (§3.4a), bid/ask/last/vol/OI/greeks columns, ATM highlight.
- **Task D** — Order ticket integration: row-click → confirmation popup → `PlaceOrder`; Order Impact badge reuse.
- **Task E** — Persistence (`option-chain-settings.cfg`) + docs (`architecture.md` reqId table, `task-history.md` entry).

Land A→B→C as separate commits/PRs if the branch lives long; D and E can ride with C.

## 11. Open questions for the user (ask before/while implementing, don't guess)

1. **Underlying asset classes**: stocks only, or also futures options (FOP) and index options? IB's `reqSecDefOptParams` call signature already supports FOP via `futFopExchange`, but FOP contract construction has extra quirks (e.g. `/ES` options are on CME, expiry conventions differ from equity monthly/weekly). Recommend **stocks/ETFs only for v1**, FOP as a follow-up given the app already has /ES /NQ futures market-health wiring that would make FOP a natural v2.
2. **Multi-leg orders (spreads)?** v1 should probably be single-leg only (buy/sell one call or put) — combos (verticals, iron condors) are a materially bigger scope (IB combo `ComboLeg` contracts, multi-leg margin previews). Recommend deferring.
3. **Snapshot-refresh interval default** and whether it should be user-configurable per-instance or app-wide.
4. **Where does it live in the Windows menu / group system** — same `kMaxMultiWin=10` cap as Chart, or a smaller cap (e.g. 4) given the market-data-line cost per open chain?

Do not start writing code against these open questions without confirming with the user in the new session — the summary/instructions above are a proposal, not a decision.
