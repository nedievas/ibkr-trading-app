Full **Options Chain** for the trading terminal — chain view, option order
tickets, multi-leg strategies, **bracket orders** (Close-At-Profit / Stop-Loss),
a strategy-analysis graph, portfolio strategy grouping + a **portfolio NAV
curve**, and **cash-settled index options** — plus the DOM / order-blotter work
that went with it. Live-tested against a paper IB Gateway.

## What it does
- **Options Chain window**: expirations × strikes, visible-row streaming,
  mirrored Calls│Strike│Puts with ITM / ±SD overlays, expiry tabs, and a live
  underlying strip (last / bid / ask / vol / IVx / expected move).
- **Order tickets**: single-leg, plus an N-leg cart (verticals, straddles,
  butterflies, condors, ratios…) priced as a signed-net BAG combo; adjustable
  legs, a strategy template picker, and cross-expiry calendars / diagonals /
  rolls.
- **Bracket orders** (options-only): independently-toggled **TP** and **SL**
  attached to any option/combo entry as a **native IB attached bracket**
  (`parentId` + OCA + transmit chain — children survive restart and protect a
  resting entry). Each with a `$`/`%` toggle, 10/25/50/75 % presets, "% from
  entry" readout, per-child TIF, and sign-aware Est. P/L. Also **attach** TP/SL
  to a working order (right-click → Orders blotter) and **protect** a held
  position (right-click → Portfolio); the Orders tab groups a bracket under a
  collapsible node with Cancel-all. *Verified live: combo stops accepted, OCA
  cancel-survivor, restart survival, protect-to-flat netting.*
- **Index options** (SPX / NDX / VIX / RUT / XSP…): underlying type auto-detected
  from the search, resolved on its native exchange, with per-expiry trading-class
  handling so weeklies + 0DTE load and route to the PM contract. Stock legs
  hidden (cash-settled). *Verified live: SPX 0DTE loads, a 0DTE put vertical
  filled.*
- **Strategy Analysis graph**: expiry payoff + theoretical "P/L today" curve
  (evaluate-at-date), probability cone with POP / P50, hover readout.
- **Portfolio**: option legs grouped into strategy rows — combos placed in-app
  link at submit (certain), others inferred with a guess-marker + manual ungroup;
  grouped combo rows now show net avg cost + net mark price (signed debit+ /
  credit−). A build-forward **NAV curve** — account value over time, IB
  PortfolioAnalyst-style — sampled from net-liq, persisted per account across
  restarts (IB's socket API exposes no historical NAV, so it accumulates
  forward), alongside the existing cash-vs-positions allocation donut.
- **DOM / orders**: inline modify (Qty / Price / Aux / TIF) with a price-ladder
  box (real minTick; combo bid/ask/mid synthesized from leg quotes), two-sided
  click-to-trade, position marker, and column show/hide/reorder.

## For reviewers
- **`core::Order` now carries a `ContractSpec`** and `PlaceOrder` is on every
  order path — an **empty-secType fallback keeps stock orders byte-identical**.
  Highest regression risk; please focus there.
- Live-found IB quirks are encoded (combo `openOrder` must echo `comboLegs`;
  `NonGuaranteed=1` only on 2-leg stock combos; per-expiry class disambiguates
  the SPX AM/PM 0DTE pair; bracket child net snapped to the combo's real tick to
  avoid error 110). Metrics verified against real definitions; Black-Scholes is
  European (correct for index options).
- The portfolio NAV curve is a persisted per-account CSV under the app config
  dir; the combo-row net prices reuse the existing signed-net BAG convention
  (both UI-only, no pure-logic change).
- **Deferred, not blocking**: futures options (FOP), one-click rolling, manual
  force-merge grouping, Flex import of real historical NAV.
- Pure logic is unit-tested — **456/456 pass**; builds clean on Linux / macOS /
  Windows.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
