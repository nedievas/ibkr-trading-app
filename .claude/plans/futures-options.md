# Futures and futures options — plan

**Status:** planned 2026-11-25 (options), extended 2026-11-27 with trading the
futures themselves (Part F), which comes first. Branch `futures-and-options`
(from `main` at #7, 1.6.46; this file was left out of #7's squash merge and
restored from the `futures-options` branch). Version **1.7.0** with the first
code change, 1.7.x after.

## Part F — trading and charting futures (first)

Found 2026-11-27 from a `/SIL` chart that showed the wrong price. Charts and
Order Book quotes work only for the CME equity-index futures (`/ES`, `/NQ`,
`/RTY`), front quarterly month or `/ES 202612`. Otherwise:

1. **The symbol search drops the `/`.** For a futures pattern it offers one
   result, `{base, "FUT", "CME"}`, and Enter takes its symbol: `/SIL` becomes
   `SIL`. `IsFuturesSymbol("SIL")` is false (SIL isn't a known root), so the
   chart loaded **SIL the stock** (Global X Silver Miners ETF), seen in the
   1.6.44 log. `/ES` survives only because `ES` is a known root.
2. **Every future is built on CME** (`MakeFuturesContract`): YM, treasuries
   and grains are CBOT, gold and silver COMEX, crude / gas / platinum NYMEX,
   and get IB error 200.
3. **The default month is the equity-index rule** (`FuturesFrontMonth`: Mar /
   Jun / Sep / Dec, expired from the 22nd). Metals, energy and grains list
   other months, and their volume moves on before first notice day (silver's
   Dec moves to Mar in late November: the user expected `SILH7`).
4. **Local symbols (`SILH7`, `ESZ6`) aren't understood**; only `/SIL 202703`.
5. **Orders go out as stock.** Chart and Order Book orders carry an empty
   `spec`, so `PlaceOrder` uses `MakeStockContract("/ES 202612")` and IB
   rejects them. Nothing trades by mistake, but nothing can be traded.
6. **Tick and contract size are stock values.** The Order Book ladder steps
   $0.01 (`LadderStep`); order prices, brackets and stop suggestions round to
   the cent (IB error 110 on ES's 0.25); the position strip, order impact and
   Portfolio values ignore the multiplier (ES $50 / point, SIL 1,000 oz).

Tasks:

- **F-1 — symbols and contracts.** 1.7.0: the search keeps the `/`, the
  exchange table, local symbols, a bare root is a stock (done). 1.7.1: listed
  months, default by volume, the month picker (done, 1.7.2). The search keeps the `/` and any
  typed month. Pure `FuturesProduct(root)` table (`IBKRUtils.h`: exchange per
  root; equity index, micros, metals, energy, treasuries, grains, FX) used by
  `MakeFuturesContract`. Pure `ParseFuturesLocalSymbol` (`SILH7`, `ESZ6`,
  `/SIL H7` -> root + YYYYMM; month codes F G H J K M N Q U V X Z, year digit
  to the nearest year not in the past). Listed months from IB:
  `reqContractDetails` on the root (FUT, exchange, no month) returns each
  month with its last trade date and local symbol; `MsgContractConId` gains
  `localSymbol`, `exchange`, `minTick`, `marketRuleIds`. Default month by
  volume: a snapshot quote of the first two or three months, pick the one with
  the most volume today (pure `PickActiveMonth`); with no volume yet, the
  nearest one not expiring within a few days. A month picker beside the symbol
  on Chart and Order Book showing local symbols (`SILZ6 / SILH7 / SILK7`).
- **F-2 — tick rules (done, 1.7.3; setup suggestions not yet).** `reqMarketRule` + the `marketRule` EWrapper override
  -> `MsgMarketRule` -> `onMarketRule`; main.cpp caches rules by id. Pure
  `TickForPrice(rule, price)` / `SnapToRule(rule, price)` (new
  `MarketRule.h`). Used by the Order Book ladder step, chart order lines and
  chart-click prices, order entry, brackets and the stop / setup suggestions.
  Stocks keep today's grid where IB gives no rule. Dispatch test.
- **F-3 — orders.** Chart and Order Book orders on a future carry a FUT spec
  (root, month, exchange, multiplier). Order impact, position strip, P&L
  previews and bracket P&L multiply by the contract size.
- **F-4 — Portfolio.** FUT positions: value and P&L with the multiplier, live
  price from `pnlSingle` (stocks and options only today), avg cost per unit.

Live checks for Part F: `/SIL`, `SILH7`, `/GC`, `/CL`, `/ZN`, `/YM` chart and
quote; the default month is the active one and the picker switches; an `/ES`
limit order lands on the 0.25 grid, a bracket is accepted, and the Portfolio
value is points x 50.

## Part FO — options on futures (after Part F)

The exchange table (old FO-1) and the market rules (old FO-2) move into F-1 /
F-2; the rest stays as planned below.

## Goal

Open the Options Chain on a futures contract (`/ES 202612`, `/NQ 202612`, ...)
and trade its options like stock and index options: chain, quotes, single-leg
and combo orders, brackets, the analysis graph, and the Portfolio (grouping,
Protect / Close / Roll / Analyze).

## How futures options differ (what the code has to handle)

1. **The underlying is one futures month.** IB's `reqSecDefOptParams` takes the
   future's conId and returns only options whose underlying is that future:
   `/ES 202612` lists the weeklies and the quarterly up to its December
   expiry; January-March options sit on `/ES 202703`. The chain is opened per
   futures month, and the toolbar offers the front and next month.
2. **Request shape.** `reqSecDefOptParams(symbol="ES", futFopExchange="CME",
   underlyingSecType="FUT", conId)`. Option contracts are `secType="FOP"`, on
   the product's exchange (CME, CBOT, ...; no SMART), with the trading class
   and the multiplier (ES 50, NQ 20, RTY 50, YM 5; micros MES 5, MNQ 2).
3. **Several classes per date.** ES lists the quarterly (`ES`, AM), the
   end-of-month (`EW`), weekly (`EW1`-`EW4`) and daily (`E1A`...) classes. The
   index logic already keeps every class for display and picks one class per
   expiry (`PreferOptionClass`, the class that isn't the root symbol, i.e. the
   PM one); futures reuse it.
4. **Price grid isn't the US equity one.** The $3 penny / nickel step-up
   (`OptionTickAt`) is an equity-option rule. ES options trade in 0.05 below
   5.00 and 0.25 from 5.00; other products differ. IB publishes these as
   *market rules* (`contractDetails.marketRuleIds` → `reqMarketRule` →
   `marketRule(id, priceIncrements)`): a list of (low edge, increment). Using
   them avoids IB error 110 and a hand-kept table per product.
5. **Pricing model.** Options on futures are priced with Black-76 (the forward
   is the future, no carry): `e^{-rT} [F N(d1) - K N(d2)]`,
   `d1 = (ln(F/K) + sigma^2 T / 2) / (sigma sqrt T)`. The analysis graph's
   theoretical curve and the IV backed out of a held leg's mark must use it.
   (ES / NQ options are American-style; Black-76 is the usual approximation.)
6. **No stock leg.** Like an index, no `+Buy 100` / stock templates.
7. **Positions are `FOP`.** The Portfolio, the chain's held pills and the
   strategy grouping currently look for `OPT` only.

Found while planning: `IBKRClient::MakeFuturesContract` builds every future on
CME, which is wrong for YM / treasuries / grains (CBOT), GC / SI (COMEX) and
CL / NG / RB / HO / PL / PA (NYMEX). FO-1 fixes it with an exchange table.

## Scope

Products: the equity-index futures `ES`, `NQ`, `RTY`, `YM` and the micros
`MES`, `MNQ` (the micros need adding to `IsFuturesBaseSymbol`). Others
(energy, metals, treasuries) get the exchange fix but are not tested for
options in this phase.

Out of scope: options on other futures products, futures legs inside a combo
(a covered-call-style future + option), early-exercise modelling.

## Tasks

- **FO-1 — pure helpers + tests (1.7.0).** `IBKRUtils.h`:
  `FuturesExchange(base)` (CME / CBOT / COMEX / NYMEX) used by
  `MakeFuturesContract`; micros added to the futures list. `OptionPricing.h`:
  `Black76Price`, `Black76Greeks`, `ImpliedVolFromPrice` with a model switch.
  `ChartAnalysis.h` / new `MarketRule.h`: `PriceIncrement` list +
  `TickForPrice(rule, price)` + `SnapToRule(rule, price, side)`.
  Tests `[futures]`, `[options][pricing][black76]`, `[market-rule]`.
- **FO-2 — IB plumbing.** `contractDetails` carries `marketRuleIds` (per
  exchange; take the one for the order's exchange); `IBKRClient::ReqMarketRule`
  + `marketRule` EWrapper override → `MsgMarketRule` → `onMarketRule`;
  main.cpp caches rules by id. Dispatch test in `test_ibkr_queue.cpp`.
- **FO-3 — chain on a future.** `OptionsChainWindow`: `m_underlyingSecType`
  gains `"FUT"` (detected from `/ES` or a known futures root; saved as
  `OPT_UNDERLYING_SECTYPE`), with the root (`ES`) and month (`202612`) kept
  apart from the displayed symbol. main.cpp: resolve the future's conId and
  quote; `reqSecDefOptParams` with the FUT shape; strike enumeration, leg
  conIds and quote subscriptions as FOP specs on the product exchange with the
  per-expiry class; multiplier from secDef. Toolbar: `FUT` tag and front /
  next month buttons. No stock legs (the index gate becomes "no stock leg for
  IND or FUT").
- **FO-4 — orders.** Single FOP and FOP BAG (`exchange` = product exchange,
  legs on it too). Ticket, buttons, steppers and the confirm popup snap to the
  leg's market rule (the equity $3 rule stays for OPT); a combo uses the
  coarsest leg increment at the net price. Brackets: `BracketTickAt` takes the
  rule for FOP. What-if works unchanged.
- **FO-5 — analysis + Portfolio.** Analysis graph and `BuildPositionAnalysis`
  use Black-76 for FOP legs. Portfolio: `FOP` counted as an option in
  `PositionMultiplier`, `AvgCostPerUnit`, `OnPnLSingle`, strategy grouping,
  labels, Protect / Close / Roll / Analyze; the chain's held pills include FOP.
- **FO-6 — docs** (architecture, testing, task history) and live tests.

## Live checks (paper, market hours)

1. `/ES` front month: chain loads, weeklies and the quarterly appear, quotes
   stream, greeks show.
2. Single option order at a price above 5.00 lands on the 0.25 grid; below
   5.00 on 0.05; no error 110.
3. A vertical (BAG on CME) fills; Portfolio groups it.
4. TP / SL bracket on it is accepted.
5. Analysis graph: the theoretical curve meets the expiry line at expiry.

## Open questions for the user

0. Default futures month by volume (recommended) or by a date rule?

1. Products: ES / NQ / RTY / YM plus MES / MNQ — right set?
2. Does the paper account have CME options market data? (Charts on `/ES` load,
   so CME futures data is there; options data is a separate permission.)
