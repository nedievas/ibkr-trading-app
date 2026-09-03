# Plan: Options Chain Window

**Status:** Planning complete — decisions locked, no code written yet.
**Branch:** `feature/options-chain` (branched from `fix/stability-review` @ `cb09e33`, pushed to `fork`). Kept isolated from the stability-review work so the two don't get mixed in one PR.
**Owner:** Jose (nedievas)
**Goal:** Add an Options Chain window — pick an underlying, see expirations × strikes with bid/ask/last/volume/OI/IV/greeks for calls and puts, and place single-leg **and vertical-spread** option orders from it.

This file is the source of truth across sessions — update **Status** and the per-task checkboxes as work progresses. A new session picking this up should read this file plus `.claude/rules/*.md` (already loaded via `CLAUDE.md`) and nothing else.

---

## 0. Locked decisions (2026-09-03)

The four open questions from the first draft have been answered by the user. These are decisions, not proposals — do not re-litigate them:

| # | Question | **Decision** | Consequence |
|---|---|---|---|
| 1 | Underlying asset classes | **Stocks/ETFs only** (`secType=OPT`, SMART) | No FOP/index work. `MakeContractFromSpec`'s existing STK branch is the model; index/FOP deferred to v2. |
| 2 | Multi-leg spreads | **Include vertical spreads** | Needs IB `ComboLeg` / `secType="BAG"` support in `PlaceOrder` — a real addition, see §6. Iron condors / calendars still out of scope. |
| 3 | Quote feed strategy | **Stream only visible rows** | Persistent `reqMktData` for strikes in view ± buffer, subscribe/unsubscribe on scroll. Needs a real subscription manager (§5) — this is the single largest source of risk in the feature. |
| 4 | Instance count | **Singleton (1 window)** | No per-instance reqId pooling, no multi-instance spawn/persistence. Settings ride the existing `singleton-settings.cfg` `WINDOW:` block pattern. Materially cheaper than the multi-instance draft assumed. |

Net effect vs. the first draft: **cheaper** on windowing/persistence (singleton), **more expensive** on data feed (streaming) and orders (combos).

---

## 1. Why a separate branch

`fix/stability-review` (v1.2.0, commit `cb09e33`) is a long chain of live-testing bug fixes and persistence work. Options chain is a large, self-contained new feature with its own IB API surface, new data models, and a new window — it doesn't belong in that stream and would make the eventual PR hard to review. `feature/options-chain` branches from `cb09e33` so it inherits all the stability fixes but stays isolated for its own PR into `main`.

Before merging, rebase or merge `main` in if `fix/stability-review` has landed there and diverged further.

## 2. Ground truth — verified against the code (2026-09-03)

The first draft made three assumptions that are **wrong**. Corrections, all verified by inspection:

1. **`ContractSpec` already exists** — `src/core/models/ScannerData.h:68`, with `conId / symbol / secType / exchange / primaryExchange / currency / lastTradeDateOrContractMonth / multiplier`. `IBKRClient::MakeContractFromSpec` (`IBKRClient.cpp:292`) already branches STK-vs-native-exchange and already fills expiry + multiplier. **Options need only three new fields** (`strike`, `right`, `tradingClass`) and one `secType == "OPT"` branch — not a new contract-construction path. `ReqMarketDataSpec` / `ReqHistoricalDataSpec` then work on option contracts for free.
2. **There is no `tickGeneric` override.** The draft claimed one exists to extend. `grep tickGeneric src/core/services/IBKRClient.{h,cpp}` returns nothing. Open interest (generic tick 101) needs a **brand-new** EWrapper override + `IBMessage` variant, same as `tickOptionComputation`.
3. **The proposed reqId block 20000–20999 is already taken** — company-name enrichment (`g_nextCompanyNameReqId`, `main.cpp:2448`, wraps at 20999). The draft's comment there ("20000–20999 is unused elsewhere") was true when written and is now stale. See §7 for the corrected allocation.

One further blocker the draft did not flag:

4. **`PlaceOrder` hardcodes stock contracts.** `IBKRClient::PlaceOrder` (`IBKRClient.cpp`) opens with `Contract c = MakeStockContract(o.symbol);` — there is no path for an order to carry a non-stock contract. Both single-leg options *and* verticals are blocked on fixing this. This is the first thing Task A must address (§6), and it is a **shared, cross-cutting change** — every existing order path flows through this function, so it needs care and a regression pass over stock/futures orders.

## 3. What already exists to imitate

| Pattern | Reference |
|---|---|
| Singleton window lifecycle (construct in `CreateTradingWindows`, destroy in `DestroyTradingWindows`, Windows-menu toggle) | `PortfolioWindow` / `WshCalendarWindow` in `main.cpp` |
| Rotating reqId pool (stale-tick defence) | `AllocTradingTickId()` / `AllocChartMktId()` in `main.cpp:744-772` |
| Pure-logic extraction for testability | `core/services/ChartAnalysis.h` — no IB/ImGui deps, full Catch2 coverage |
| Symbol-search autocomplete | `src/ui/SymbolSearch.h` — reuse directly for the underlying field |
| Group sync (click symbol → broadcast) | `WindowGroup.h` + `BroadcastGroupSymbol`; Portfolio's `OnBroadcastSymbol` (v1.2.0) is the freshest example |
| Singleton settings persistence | `singleton-settings.cfg` `WINDOW:<name>` blocks — `PortfolioWindow::SerializeSettings/ApplySettings` |
| Column-visibility toggles | `ScannerWindow`'s `kColDefs[]` / `m_colEnabled[]` |
| EWrapper bridge → UI thread | `IBKRClient.{h,cpp}` `IBMessage` variant + `ProcessMessages()` |
| Order confirmation modal (viewport-centred) | `ChartWindow::DrawConfirmPopup` — note the `SetNextWindowPos(GetWindowViewport()->GetCenter())` requirement for multi-viewport |

## 4. Data model — `src/core/models/OptionData.h` (new, POD-only)

```cpp
struct OptionContractKey { std::string symbol, expiry; double strike; char right; }; // 'C'/'P'

struct OptionQuote {
    OptionContractKey key;
    int    conId = 0;
    double bid=0, ask=0, last=0, volume=0, openInterest=0;
    double impliedVol=0, delta=0, gamma=0, theta=0, vega=0, undPrice=0;
    int    reqId = 0;
    bool   subscribed = false;
    std::time_t lastTick = 0;      // staleness indicator in the UI
};

struct OptionExpiry { std::string date; std::vector<double> strikes; };

struct OptionChainMeta {
    std::string symbol, tradingClass, multiplier;
    int underlyingConId = 0;
    std::vector<OptionExpiry> expirations;   // sorted ascending, strikes sorted ascending
};

// Vertical spread staged from the chain (§6).
struct VerticalSpread {
    OptionContractKey longLeg, shortLeg;     // same expiry + right, different strikes
    int longConId = 0, shortConId = 0;
    double netDebit = 0;                     // +debit / -credit at current mid
};
```

`ContractSpec` (`ScannerData.h`) gains `double strike = 0.0; std::string right; std::string tradingClass;`, all defaulted so every existing brace-init call site is unaffected.

## 5. Subscription manager — the visible-row streaming design (§0 decision 3)

This is the hard part. Requirements:

- Only strikes currently rendered (± a buffer of N rows, default 5) hold a live `reqMktData`.
- Scrolling must subscribe entering rows and cancel leaving rows **without** thrashing: debounce ~250 ms after scroll settles before acting, so a fast flick doesn't fire hundreds of subscribe/cancel pairs.
- A hard ceiling on concurrent option subscriptions (`kMaxOptionSubs`, default **60**) leaves headroom under the typical 100-line account limit for the charts/DOM/watchlist already running. When the visible set would exceed the cap, subscribe from the ATM strike outward and drop the furthest.
- **Every cancel rotates the reqId** — `AllocOptionMktId()`, 1000-slot pool, mirroring `AllocChartMktId`. This is non-negotiable: the Phase 15 stale-tick contamination bugs (documented at length in `task-history.md`) are the exact same race, and a chain switching expiry mid-session hits it much harder than a chart does. Ticks arriving on a retired id match no entry in the quote map and are dropped at the dispatcher.
- Expiry change / symbol change / window close cancels **all** live option subscriptions before re-subscribing, same as the `CancelAllSubscriptions()` discipline added in v1.1.x.

Extract the pure part — "given visible range, current subscriptions, and a cap, what should be subscribed and what cancelled?" — as a free function in `core::services::OptionChain.h` returning `{toSubscribe, toCancel}`. That makes the riskiest logic in the feature unit-testable without IB or ImGui, which is the whole reason `ChartAnalysis.h` exists.

## 6. Order path — single-leg and verticals

**Step 1 (blocking, cross-cutting): give `core::Order` a contract.** Add `core::ContractSpec spec;` to `core::Order` (`OrderData.h`) and change `PlaceOrder` from `MakeStockContract(o.symbol)` to:
```cpp
Contract c = o.spec.secType.empty() ? MakeStockContract(o.symbol)
                                    : MakeContractFromSpec(o.spec);
```
The empty-`secType` fallback keeps every existing stock order path byte-identical — important, because bracket/OCA/trail logic all flows through here. Regression-test stock and futures orders after this change.

**Step 2 (single-leg):** row click → build `ContractSpec{symbol, "OPT", "SMART", …, expiry, strike, right, tradingClass, multiplier}` → stage a `core::Order` → existing confirmation popup → `PlaceOrder`. No other change needed. Order Impact badge (`ComputeOrderImpact`) is already generic over qty/avgCost/fillPrice and needs nothing option-specific.

**Step 3 (verticals):** IB models a spread as a single `Contract` with `secType="BAG"`, `symbol=<underlying>`, `exchange="SMART"`, and a `comboLegs` list of two `ComboLeg{conId, ratio=1, action="BUY"|"SELL", exchange="SMART"}`. `ComboLeg` is available at `vendor/twsapi/IBJts/source/cppclient/client/Contract.h:39`; `Contract::comboLegs` is a `ComboLegListSPtr`.

Implications, all of which need explicit handling:
- **Leg conIds are mandatory.** A BAG contract references legs by `conId` only. The chain must resolve each leg's conId before an order can be placed — either from `reqContractDetails` per selected leg, or from the `tickOptionComputation`/market-data path if conIds are captured there. Plan for an explicit two-leg `reqContractDetails` round-trip on spread staging, with the Confirm button disabled until both conIds land.
- **Combos price as a net debit/credit.** The limit price on a BAG is the net, and it can legitimately be negative (a credit spread). Any validation that assumes `limitPrice > 0` must be relaxed for BAG orders — check the existing `ValidateOrder` paths.
- **Combo fills report per-leg.** `execDetails` arrives once per leg; the blotter will show two fills for one submitted order. Decide whether to group them in the UI or let them list separately (v1: let them list, note it in the UI).
- Because of the above, **verticals are their own task (Task E)** and should land after single-leg is working end-to-end. Do not interleave.

## 7. ReqId layout (corrected)

The draft's 20000–20999 collides with company-name enrichment. Current occupancy tops out at 20999. Proposed:

- **`21000`** — `reqSecDefOptParams` for the chain (singleton ⇒ a single id, cancel-before-reissue like Symbol Search's 8000).
- **`21001–21099`** — option `reqContractDetails` (leg conId resolution for spreads; transient).
- **`22000–22999`** — option market-data rotating pool (`AllocOptionMktId()`, wraps at 22999).

Update the reqId table in `.claude/rules/architecture.md` when these are implemented, and add a comment at the pool declarations noting the ceiling, so the next feature doesn't repeat the 20000 collision.

## 8. Persistence

Singleton ⇒ **no new `.cfg` file**. Add a `WINDOW:optionschain` block to the existing `singleton-settings.cfg`, alongside Portfolio/Orders/WshCalendar. Fields: `OPT_SYMBOL`, `OPT_EXPIRY`, `OPT_GROUP` (clamped `[1, kNumGroups]`), `OPT_STRIKE_RANGE`, `OPT_OPEN`, and the per-column visibility toggles. `SerializeSettings`/`ApplySettings` must be pure — no IB calls, no subscription side effects on apply (the user clicking "Load Chain" re-establishes streams). This mirrors `PortfolioWindow` exactly; copy that shape.

`OPT_OPEN` matters: per the v1.1.15/v1.2.0 fixes, a singleton that defaults `m_open = true` and is unconditionally recreated on connect will reappear after the user closes it.

## 9. Testing strategy

`tests-core` (pure, no IB/ImGui) — new `tests/test_option_chain.cpp`:
- ATM-strike detection / moneyness classification given `(underlyingPrice, strikes[])`, including exact-match and empty-strikes edges.
- Strike-range filtering (± N strikes from ATM), including ranges that clip the ends of the list.
- Chain organisation: grouping raw expiry/strike lists into `OptionExpiry`, sorting, dedup across the multiple `securityDefinitionOptionalParameter` callbacks (IB fires one per exchange, so **duplicates across callbacks are expected** — dedup is real logic, not defensive coding).
- **Subscription diffing** (§5): given visible range, current subs, and cap → `{toSubscribe, toCancel}`. Cover the cap-exceeded case (ATM-outward priority), no-op when the visible set is unchanged, and full-swap on expiry change.
- Vertical-spread net debit/credit computation from two leg mids, including the negative (credit) case.

`tests-ibkr` — dispatch tests in `test_ibkr_queue.cpp` for each new `IBMessage` variant (`MsgSecDefOptParams`, `MsgSecDefOptParamsEnd`, `MsgTickOptionComputation`, `MsgTickGeneric`), following the existing per-message pattern. The `std::visit` in `ProcessMessages()` is exhaustive, so an untested variant silently no-ops.

## 10. Task breakdown

- [ ] **Task A — Contract plumbing (blocking).** Extend `ContractSpec` with `strike`/`right`/`tradingClass`; add the `secType=="OPT"` branch to `MakeContractFromSpec`; add `spec` to `core::Order` and switch `PlaceOrder` to the spec-or-stock fallback. Regression-pass stock + futures orders (place/modify/bracket) — this touches every order path. No options UI yet.
- [ ] **Task B — IB service surface.** `MsgSecDefOptParams` / `MsgSecDefOptParamsEnd` / `MsgTickOptionComputation` / `MsgTickGeneric` variants; `reqSecDefOptParams` + the four EWrapper overrides (`securityDefinitionOptionalParameter`, `…End`, `tickOptionComputation`, `tickGeneric`); `tests-ibkr` dispatch tests. Verify against a live paper Gateway by logging expirations/strikes/greeks to stderr — no UI.
- [ ] **Task C — `core::services::OptionChain.h` + tests.** Pure helpers: chain organisation/dedup, ATM detection, strike-range filter, subscription diffing, spread net-price. Full `tests-core` coverage (§9) *before* the window consumes them.
- [ ] **Task D — `OptionsChainWindow` shell + live data.** Singleton window: toolbar (group picker · `SymbolSearch` underlying · expiry combo · Load Chain · strike-range · column toggles), mirrored Calls | Strike | Puts table, ATM highlight, subscription manager wired to visible rows (§5), staleness indicator. Windows-menu entry, `CreateTradingWindows`/`DestroyTradingWindows`/`CancelAllSubscriptions` wiring.
- [ ] **Task E — Single-leg orders.** Row click → order ticket → confirmation popup → `PlaceOrder`. Order Impact badge reuse.
- [ ] **Task F — Vertical spreads.** Two-leg selection UI, leg conId resolution, BAG contract construction, net debit/credit pricing, negative-limit validation relaxation, per-leg fill handling (§6 step 3).
- [ ] **Task G — Persistence + docs.** `WINDOW:optionschain` block in `singleton-settings.cfg`; update `architecture.md` (reqId table, new window, `OptionChain.h` section), `testing.md`, `task-history.md`.

**Sequencing:** A → B → C are independent of the UI and can land as three small, reviewable commits. D depends on all three. E rides with or just after D. **F should be its own PR** — it is the largest single chunk and the one most likely to need live-Gateway iteration. G rides with F.

**Risk ranking** (highest first): F (combo orders, unfamiliar IB surface) · §5 subscription manager (stale-tick + line-limit) · A (`PlaceOrder` is on every order path — a regression here breaks stock trading).

## 10b. Stats-strip metrics — where each number must come from

The sketch's stats strip (POP · EXT · P50 · Delta · Theta · Max Profit · Max Loss ·
BP Eff.) mixes three very different kinds of number. Getting this wrong silently
is the main risk in the strip, so classify before implementing:

**(a) Computable client-side, exactly.** Deterministic from the legs and quotes:
- Max Profit / Max Loss — payoff math on the leg set.
- EXT (extrinsic) — option mid minus intrinsic.
- Net Delta / Theta — sum of leg greeks x qty x multiplier, from the greeks
  already streaming (tickType 13).
- BP Usage % — but only once BP Effect is known; it is `BP Effect / Net Liq`,
  and `netLiquidation()` already exists on PortfolioWindow.

**(b) Must come from IB, not from us — BP Effect / margin.** Verified: our
`IBKRClient` has no `whatIf` support at all, while IB's `OrderState` already
exposes `initMarginChange`, `maintMarginChange`, `initMarginAfter` and
`commissionAndFees`. So the work is plumbing, not math: set `order.whatIf =
true`, submit, and read the margin deltas back from `openOrder`.

Do **not** port tastytrade's published margin rules (alternative minimum for
naked options at 0.5% / 0.25% of deliverable, the vega test with its factor of
10, EPR/PNR risk arrays and in-house +-20% floors). Three reasons: those are
tastytrade's house rules and we clear through IB, whose engine differs; the
percentages are explicitly documented as changing at any time; and several
inputs (EPR, PNR, per-underlying variable factors) are firm estimates we have
no feed for. A number that looks authoritative and is wrong about margin is
worse than no number. The reference material is still useful for *understanding*
what the column means — just not as an implementation source.

Note also that those rules only populate on portfolio-margin accounts and show
"--" on Reg-T. Whatever we display must have an equivalent "not applicable"
state rather than a plausible-looking zero.

**(c) Needs a model, and is partly proprietary.**
- POP — approximable from delta, or from the payoff under a lognormal
  assumption. Any version we ship is an approximation and must be labelled one.
- P50 — tastytrade's is a Monte Carlo over their own vol model. Not
  reproducible. Either omit it or label it clearly as our own estimate.

Recommendation: ship (a) first, add (b) as a `whatIf` round-trip behind the
order builder, and treat (c) as opt-in with explicit "estimate" labelling.

## 10c. Derived-metric definitions already corrected

Two metrics were implemented wrongly in D2 and fixed after checking the real
definitions. Both are now pure tested helpers in `OptionChain.h`:

- **Expected move** is tastytrade's straddle weighting
  (`0.60*straddle + 0.30*strangle1 + 0.10*strangle2`, falling back to
  `0.85*straddle`), **not** `spot * IV * sqrt(DTE/365)`. The annualised-IV form
  smears event premium across a year and understates the range around earnings.
- **IVx** is Cboe's VIX-style model-free variance-swap integral over the OTM
  wings for the expiry, **not** the ATM implied vol. ATM IV samples one point on
  the smile; the two diverge under skew.

The lesson for the remaining metrics: check the definition against the source
before implementing. Two of the first three derived numbers were wrong.

## 11. Remaining open questions

None blocking. Two low-stakes defaults chosen here; flag them to the user only if they turn out to matter in live testing:
- Visible-row subscription buffer = 5 rows; scroll debounce = 250 ms; `kMaxOptionSubs` = 60.
- Greeks source = model computation (tick type 13 / generic tick 106), not bid/ask computations — steadier for a table display.
