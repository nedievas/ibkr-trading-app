# US stocks 23x5 hours (IB, 6 December 2026) — plan

**Status:** planned 2026-11-28, not started. Must ship before **Sunday 6 December
2026**, when IB moves US stocks to 23x5. Small release on the
`futures-and-options` branch (or its own branch from `main` if the futures work
isn't ready to merge by then).

## What IB changes (from the client notice)

| Session (ET) | Hours |
|---|---|
| RTH | 9:30 am - 4:00 pm |
| Pre-market + RTH | 9:00 pm (day before) - 4:00 pm |
| Full extended (Outside RTH) | 9:00 pm - 8:00 pm (next day), 23 h |
| EARLY (overnight ATS only: IBEOS, Blue Ocean) | 8:00 pm - 9:00 pm; Sundays from 6:00 pm |

- Exchanges pause 8:00 - 9:00 pm ET on weekdays.
- A trade after 8:00 pm ET counts toward the **next trade date**.
- Outside RTH orders execute 9:00 pm - 8:00 pm ET.
- SMART orders placed overnight roll into pre-market and RTH.
- The **OVERNIGHT venue** is discontinued: an order sent directly to it is
  rejected. SMART covers overnight liquidity.
- The **'overnight' time-in-force** is retired; a new **'early'** TIF exists on
  the new platforms. Old platforms keep 23x5 through DAY + Outside RTH.
- The user's IB Gateway needs updating around 1 December (outside the app).

## What the app has to change

1. **Remove the OVERNIGHT TIF.** It is offered in the Chart trade panel
   (`ChartWindow.cpp` kTIFs), the Order Book entry form and its blotter edit
   (`TradingWindow.cpp`), the Orders inline edit (`OrdersWindow.cpp`) and the
   IB mapping (`IBKRClient.cpp` kTIFs / the string-to-enum parse;
   `TimeInForce::Overnight` in `OrderData.h`). Take it out of the lists; a saved
   default or an order read back with it becomes DAY + Outside RTH. Keep
   reading IB's "OVERNIGHT" from an existing order so it still shows.
2. **Hide the OVERNIGHT venue** in the Order Book's exchange list (built from
   IB's smart components; OVERNIGHT appears there today). An order can't be
   sent to it after 6 December.
3. **VWAP / CVD day boundary at 8:00 pm ET.** `ChartWindow::ComputeIndicators`
   starts a session at each UTC date change (7 pm ET in winter, 8 pm in
   summer). Use 8:00 pm ET (DST-aware, like `BarSession`), which is IB's new
   trade-date boundary. Pure helper + test (`[vwap]`).

## Not changed (decided)

- **Outside RTH** and the chart bracket's after-hours guard: still correct,
  IB only widens the window.
- **Chart session shading / "Overnight" hide toggle** (`BarSession`: Overnight
  8 pm - 4 am): labels differ from IB's new names (EARLY 8-9 pm, pre-market
  from 9 pm), behaviour is fine. Revisit only if the user asks.
- **'early' TIF:** not added until it's confirmed the TWS API version in use
  (10.49) accepts it.

## Live checks (after 6 December, paper)

1. Chart / Order Book TIF lists have no OVERNIGHT; a DAY + Outside RTH order
   placed at 10 pm ET is accepted and works overnight.
2. Order Book exchange list has no OVERNIGHT.
3. VWAP restarts at 8:00 pm ET on an intraday chart with extended hours shown.
