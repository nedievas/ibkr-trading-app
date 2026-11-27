# Futures options (FOP) in the Options Chain — plan

**Status:** planned 2026-11-25, branch `futures-options` (from `main` at #6,
1.6.44). Version **1.7.0** with the first code change, 1.7.x after.

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

1. Products: ES / NQ / RTY / YM plus MES / MNQ — right set?
2. Does the paper account have CME options market data? (Charts on `/ES` load,
   so CME futures data is there; options data is a separate permission.)
