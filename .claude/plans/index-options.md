# Plan: INDEX Option Trading (Options Chain)

Extend the Options Chain window from stocks/ETFs to **cash-settled index
options** — SPX, NDX, RUT, VIX, XSP, DJX, OEX/XEO, etc. — as underlyings.

## Why it's mostly already there

The contract layer, payoff engine, analysis graph, expected-move / IVx, and
portfolio grouping are all multiplier-aware and secType-agnostic:

- `MakeContractFromSpec` already has an **IND branch** (native exchange) and a
  **BAG branch** — no contract-layer change needed for the underlying or combos.
- `ComputeStrategyMetrics` / `PayoffAtExpiry` / `TheoreticalPnL` use the returned
  multiplier; index options are ×100 like equity options.
- `BlackScholesPrice` is **European** — which is *correct* for index options
  (SPX/NDX/RUT/VIX are European, cash-settled), and only an approximation for
  American equity options. So the theoretical curve is, if anything, more
  accurate here. No pricing change.

So this is a **plumbing + gating** job, not a new engine. Four real problems.

## The four problems

### P1 — Underlying is `IND`, not `STK`
`OnRequestUnderlying(sym)` calls `ReqContractDetails(kUnderlyingCdId, sym)` which
resolves to `MakeStockContract` (STK/SMART) for any non-futures symbol, and
`ReqMarketData(kUnderlyingMktId, sym, "")` likewise streams STK. An index
underlying is `secType="IND"` on a **native exchange** (SPX/VIX→CBOE,
NDX→NASDAQ, RUT→RUSSELL, DJX→CBOE, XSP→CBOE). SMART does not resolve the IND
underlying for contractDetails/market data.

### P2 — Telling the app a symbol is an index
The Load path takes a bare symbol string with no secType, and there is no
STK-vs-IND signal. Need an explicit, low-surprise way to mark the underlying as
an index.

### P3 — AM vs PM settlement / weeklies (`SPX` vs `SPXW`) — the hard one
IB returns `secDefOptParams` as multiple `(exchange, tradingClass, multiplier,
expirations, strikes)` tuples. For SPX:
- `SPX`  = AM-settled monthlies (3rd Friday open print).
- `SPXW` = PM-settled weeklies + EOM + the 3rd-Friday **PM** expiry.

The **3rd Friday carries both** an `SPX` (AM) and an `SPXW` (PM) expiry on the
*same date* — a genuine ambiguity that only `tradingClass` resolves.
`MergeChainDefinition` currently prefers the standard class (`tradingClass ==
symbol`) and locks it, which for SPX would keep only the monthlies and hide the
far-more-liquid weeklies. The streaming/order path also flattens tradingClass
away (SMART + expiry + strike + right), which is fine for equities but
**ambiguous on the SPX 3rd Friday**.

Fix direction: carry `tradingClass` **per expiry** through the chain meta so each
expiry knows its class, and pass that class into the subscription, leg-conId,
and order specs. This resolves AM/PM and surfaces weeklies. (NDX/RUT have the
same NDX/NDXP, RUT/RUTW split.)

### P4 — No shares exist (cash-settled)
Covered call / married put / collar / conversion / reversal and the
`+Buy 100` / `+Sell 100` underlying-strip buttons assume a tradeable equity leg.
Index options are cash-settled — there is no share. All stock-leg affordances
must be gated **off** when the underlying is an index. (Cash-secured put and all
pure-option spreads are unaffected.)

## Tasks

### IO-1 — Underlying secType plumbing (P1 + P2)
- Add `m_underlyingSecType` (`"STK"` default / `"IND"`) to `OptionsChainWindow`,
  persisted in the `WINDOW:optionschain` block (`OPT_UNDERLYING_SECTYPE`).
- Toolbar: a small **STK / IND** selector next to the symbol input (default STK).
  Seed it from the symbol-search result's secType when available
  (`DrawSymbolInput` already has the `SymbolResult.secType`), so picking "SPX
  (IND, CBOE)" auto-flips it; the manual toggle is the override / fallback.
- `OnRequestUnderlying` gains a secType arg (or reads the window's). main.cpp:
  - New `IBKRClient::ReqContractDetailsIndex(reqId, sym)` (or reuse
    `ReqContractDetailsSpec` with a `{secType="IND", exchange=""}` spec —
    `MakeContractFromSpec`'s else-branch already routes IND to its native
    exchange via `primaryExchange`/`exchange`; for a bare index symbol leave
    exchange empty so IB resolves, or seed a small symbol→exchange map).
  - `ReqMarketData` for the underlying must use the IND contract, not STK — add
    a spec-based underlying market-data request (`ReqMarketDataSpec`) instead of
    the bare-symbol `ReqMarketData`.
- **Verify:** loading SPX (IND) resolves a conId, streams the index level in the
  underlying strip, and does not error 200.

### IO-2 — reqSecDefOptParams underlyingSecType (P1)
- `OnReqSecDefOptParams` currently hardcodes `"STK"`. Thread the window's
  `m_underlyingSecType` through the callback so index chains request with
  `"IND"`; `futFopExchange` stays `""` (SMART chain lookup).
- **Verify:** SPX secDefOptParams returns expirations/strikes for SPX + SPXW.

### IO-3 — Per-expiry tradingClass (P3)
- Extend `OptionChainMeta` so each expiry carries its `tradingClass` (weeklies
  vs monthlies), instead of one locked class for the whole chain. Populate it in
  `MergeChainDefinition` from the per-tuple `(tradingClass, expirations)` mapping
  IB delivers.
- `OnReqOptionStrikes`, `OnSubscribeOption`, `OnReqOptionLegConId`, and the order
  build all pass the **expiry's** tradingClass into the `ContractSpec` (they
  currently omit it). `MakeContractFromSpec`'s OPT branch already forwards
  `tradingClass` when set.
- Expiry-tab UI: on a date that has both an AM and PM class (SPX 3rd Friday),
  either show two tabs (`SPX` / `SPXW`) or tag the tab AM/PM. v1 acceptable: show
  both as separate expiry entries labelled by class.
- Pure `MergeChainDefinition` change → **add `[options][chain]` tests** for the
  per-expiry class mapping (SPX+SPXW union, 3rd-Friday dual class, both callback
  orders — mirrors the 1.3.30 adjusted-class regression test).
- **Verify:** SPX weeklies appear; selecting an SPXW expiry streams quotes and a
  vertical on it resolves leg conIds + places.

### IO-4 — Gate off stock legs for index (P4)
- When `m_underlyingSecType == "IND"`: hide/disable the `+Buy 100` / `+Sell 100`
  underlying-strip buttons, and grey the stock-inclusive templates
  (covered call / married put / collar / conversion / reversal / buy-write) in
  the strategy picker with a tooltip ("cash-settled index — no share leg").
  Reuse the existing grey-out mechanism from the >2-leg stock-combo gate.
- **Verify:** on SPX, no stock-leg buttons; the six stock templates are greyed;
  pure-option templates (verticals, condor, butterfly, straddle, strangle,
  calendar, diagonal) all build.

### IO-5 — Docs
- `architecture.md` Options Chain section: index underlying (IND, native
  exchange), per-expiry tradingClass, no stock legs, European pricing note.
- `task-history.md` entry.

## Out of scope (v1)
- **Futures options (FOP)** — /ES, /NQ options. Different again: underlying is a
  FUT, `futFopExchange` must be set, multiplier varies (/ES 50). Separate phase.
- **AM/PM UX polish** beyond correct disambiguation (e.g. a settlement badge).
- **Combo routing exchange precision** — index option combos route SMART; if a
  specific index requires a native combo exchange, handle reactively (surface the
  IB error on the status line, as the chain already does).
- **Entitlements** — index + OPRA market-data permissions are an account/runtime
  concern, not code; a rejected subscription already surfaces on the status line.

## Risks / unknowns to confirm live
- Whether SMART resolves index-option **leg conIds** on the SPX 3rd Friday
  without an explicit exchange once tradingClass is supplied (expected: yes).
- Whether the paper account is entitled to index-option data at all (the live
  log already shows limited permissions) — if not, IO-1..IO-4 are still correct
  but can only be smoke-tested on an entitled account.
- Exact native exchange per index for the IND underlying resolution (a tiny
  symbol→exchange seed map may be simpler than relying on IB's default).
