# IBKR Trading App

A C++20 desktop trading terminal for Interactive Brokers, built with Dear ImGui (Vulkan backend) and the official IBKR TWS API.

Designed as a high-performance alternative front-end for IB Gateway / TWS, focusing on execution speed, multi-window workflows, and real-time market visualization.

Distributed for research and personal trading use via IBKR accounts. No guarantees of correctness, uptime, or suitability for financial decision-making.

The “Interactive Brokers API Usage Notice” section must be reviewed prior to use. By using this software, users acknowledge and agree to its terms.

## Recommended IBKR market subscriptions

- CME, in real time (no professional, level 2)
- NASDAQ (Network C/UTP)(NP,L1)
- NASDAQ TotalView-OpenView (NP,N2)
- NASDAQ TotalView-OpenView EDS (NP,N2)
- NYSE (Network A/CTA) (NP,L1)
- NYSE American, BATS, ARCA, IEX and regional markets (Network B) (NP,L1)
- NYSE ArcaBook (NP,N2)
- NYSE OpenBook (NP,N2)
- Other default within your account

## Demonstrated Capabilities

- **Candlestick Charts** — Multi-timeframe OHLCV with SMA, EMA, Bollinger Bands, VWAP (with optional ±1σ / ±2σ volume-weighted bands), RSI, and volume. Optional **Volume Profile** overlay renders a horizontal volume-by-price histogram on the right edge of the chart, highlighting the Point of Control (the price where the most volume traded) and the ~70% Value Area — re-buckets to the visible Y-range as you pan/zoom
- **Trading Style Modes** — Four curated chart presets (Scalping = 1m / 2 D, Day Trading = 15m / 20 D, Swing = 1D / 1 Y, Investment = 1W / 5 Y) plus a **Free** mode that unlocks the timeframe and keeps your own settings. Each curated mode hard-binds its timeframe, history horizon, and the analysis params used by the auto S/R, breakout signal, setup overlay, and unguarded-stop suggestion — so the chart's recommendations stay coherent within a session instead of drifting as you pan or change windows. Per-chart, persisted to `~/.config/ibkr-trading-app/chart-modes.cfg` and restored automatically on reconnect
- **Auto Technical Analysis** — Toggleable, automatically detected support and resistance levels (clustered swing highs / lows, ranked by touch count), drawn as colour-coded dashed lines with touch-count labels; tunable swing window, touch threshold, and scan depth via the chart's Auto... settings popup. A **linear-regression trend line** (with optional ±2σ channel and L/4-bar forward projection) shows the prevailing direction colour-coded by slope. Optional **supply/demand zones** render as translucent rectangles whose thickness reflects the spread of constituent swings; an **imminent-breakout signal** ( ▲ LONG SETUP / ▼ SHORT SETUP ) appears when price sits inside a zone with Bollinger-Band compression and directional momentum. Five further toggleable overlays: **Donchian channels** (rolling N-bar high/low envelope), **Keltner channels** (EMA20 ± 2·ATR14), **auto-Fibonacci levels** anchored to the largest recent swing span, **classic daily pivot points** (P, R1-R3, S1-S3 from the prior trading day's OHLC; intraday only), and **breakout markers** (▲/▼ on bars that closed through detected S/R)
- **Setup Suggestions** — When the imminent-breakout signal fires, an optional structure-based **reference plan** overlays the chart with three dashed lines — entry (cyan), protective stop (red, padded past the longest-wick anchor with round-number avoidance), and target (green, anchored at the nearest opposing level) — plus an R:R tag and a suggested share count derived from the active account's NetLiquidation × configurable risk-per-trade. A `[Use suggestion]` button in the Trade panel stages a Limit entry into the existing confirmation modal — never auto-fires. R:R minimum, ATR padding, round-number pad, stop-limit offset, and risk-per-trade are all tunable; defaults reject any plan with R:R < 2.0
- **Unguarded-Position Guard** — A non-blocking yellow strip appears in both the Chart window and the Order Book window whenever a held position has no protective Stop / Stop-Limit / Trail / Trail-Limit on the same symbol. One-click `Place stop` builds a Stop-Limit on the opposite side of the position (full quantity, DAY/RTH-only) and routes through the existing confirmation modal — never bypasses confirmation. The suggested stop level is derived from the chart's auto-detected support/resistance; a `Dismiss` button hides the warning until the position quantity changes
- **Order Impact Preview** — Before submitting any order, both the Chart window's Trade panel and the Order Book's order entry form display a colour-coded badge previewing what the order will do to the current position — `OPEN LONG / SHORT`, `ADD TO`, `REDUCE`, `CLOSE`, or `FLIP` — together with the projected closing-leg P&L in dollars (and percent) at the prices currently entered, the post-fill new average cost (for opens / adds), and a two-leg breakdown for flips. Blue for open / add, green for reduce / close at profit, red for reduce / close at loss, orange for flip. When the Setup Suggestions overlay is active, a second line shows target / stop / R:R derived from the same fill-price math so risk and reward are visible side-by-side with the order ticket. Recomputes live as you type quantity or price
- **Options Chain** — Expirations × strikes for a stock, ETF or cash-settled index (SPX, NDX, RUT, VIX, …) in a mirrored Calls | Strike | Puts table with greeks, IV, open interest, expected move and IVx. Only the strikes in view are streamed, so a wide chain stays within IB's market-data line limit. Held positions show as signed quantity pills on their strike
- **Option Strategy Tickets** — Click bid / ask cells to build an order of up to 6 legs, or pick a template: verticals, straddle, strangle, risk reversal, synthetic, butterfly, iron condor, calendar, diagonal, buy-write (covered call), collar, conversion, reversal. The underlying shares can be a leg too. Multi-leg orders go to IB as one combo at a net debit / credit. Each leg has strike, expiry, side and ratio controls and is tagged open / add / close / flip against what you hold. The ticket shows max profit / loss, net delta and theta, snaps prices to the contract's tick, warns on a marketable or mistyped limit, and can ask IB for the margin impact before sending
- **Option Brackets** — Take-profit and stop-loss on an option or combo order, set in $ or % of the premium: with the entry (IB holds the exits until it fills), attached to a working order, or placed to protect a position you already hold
- **Strategy Analysis** — P&L graph for the ticket or a held position: payoff at expiry, a theoretical "today" curve you can slide forward in time, break-evens, a probability cone, and estimated probability of profit
- **Index Support** — Cash-settled indexes work in charts, the Watchlist, Order Book quotes and the Options Chain
- **DOM / Level II** — Live order book ladder, two-sided click-to-trade (buy or sell at any price row), with your position and working orders marked on the ladder
- **Order Management** — Place, track, modify and cancel Market, Limit, Stop, Stop-Limit, Trailing, MOC/LOC, MTL, MIT/LIT, Midprice, and Relative orders; full order status lifecycle including CANCELLING state. Working orders are edited in place in the blotter (quantity, price, TIF), with a clickable price ladder showing live bid / mid / ask
- **Market Scanner** — Scan for Top Gainers/Losers, Volume Leaders, 52W Highs/Lows, RSI extremes, and more
- **News Feed** — Real-time and historical news across Market, Portfolio, and per-Stock tabs with sentiment indicators
- **Portfolio Dashboard** — Real-time account-level P&L (daily, unrealized, realized), positions, account value curve, allocation donut, and performance metrics (Sharpe, Max Drawdown, Alpha, Beta, Win Rate). Option legs are grouped into named strategies (vertical, calendar, iron condor, collar, …) with net cost and P&L per strategy; right-click a strategy to roll it, protect it with a take-profit / stop-loss, or open it in Strategy Analysis
- **Orders Blotter** — Live open orders and full execution history with commissions and realized P&L. Combo orders are named by strategy, a bracket's entry and exits are grouped under one row with Cancel all, and history is kept across restarts
- **Notifications** — Toasts, alert tones and spoken alerts for fills, rejections, cancels, working / held orders and IB warnings, with a history window
- **Symbol Autocomplete** — IB-validated symbol search with 300 ms debounce across all windows; invalid symbols automatically revert to the last confirmed ticker
- **WSH Corporate Event Markers** — Upcoming earnings, dividends, and splits shown as colour-coded vertical markers on the price chart (yellow = Earnings, cyan = Dividend, purple = Split) with hover tooltips; sourced live from Wall Street Horizon via the IB API
- **WSH Calendar** — Cross-symbol aggregate view of all upcoming corporate events for held positions and open chart symbols; filterable by symbol, date range, type, and importance; sortable table with colour-coded event types
- **Multi-Account Support** — On live sessions with multiple accounts, a selector modal appears at connect time; active account shown in the menu bar and stamped on every order
- **Paper & Live accounts** — Toggle between paper and live trading from the login screen
- **Watchlist** — Multi-tab symbol watchlist for stocks, ETFs and indexes with 22 configurable columns (right-click a header to show, hide or reorder); Mag 7 default preset; live bid/ask/last/size/52W/spread ticks; saved presets; layout and symbols persisted to `~/.config/ibkr-trading-app/watchlists.cfg` and restored automatically on reconnect
- **Replay Window** — Pre-market, intraday, and post-market playback of historical trading days fetched from IB. Play through a day bar-by-bar (1m to 1D timeframes) with progressive candle reveal, adjustable speed (0.25x to MAX), manual step forward/back, and scrubber. Two modes: **Analysis** (read-only review with real fill markers overlaid on the chart) and **Operate** — a full ChartWindow-style sandbox: BUY/SELL trade panel with all 13 order types (Limit/Stop/StopLimit/Trail/Trail Limit/MIT/LIT/etc.), chart-click order arming with dashed price-bubble overlay (single-click for Limit/Stop/MIT, two-click trigger+limit for StopLimit/LIT), Transmit-Instantly toggle + per-window confirmation popup (Ctrl+click always shows the popup), live position strip (qty/entry/last/unreal P&L from the simulated account), Order-Impact badge that classifies each staged order as OPEN/ADD/REDUCE/CLOSE/FLIP with projected P&L, and dashed working-order lines on the chart (separate STP/LMT legs for StopLimit and LIT). All paper orders go through the `ReplayEngine` — no live-market exposure. Group time-cursor sync keeps multiple replay windows in lockstep. State persisted to `~/.config/ibkr-trading-app/replay-windows.cfg`
- **Multi-Instance Windows** — Open up to 10 simultaneous Chart, Order Book, Scanner, News, Watchlist, and Replay windows to monitor multiple assets at once
- **Window Groups** — Link any windows into a group (G1–G10); changing the asset in one window instantly syncs all others in the same group
- **Layout Presets** — One-click workspace layouts: Trading Focus, Research, Full Desk, Options
- **Saved Workspace** — Window layout, which windows are open, per-window settings, watchlists, chart symbols and order history are saved and restored on the next launch
- **Responsive UI** — All toolbars and info bars wrap gracefully when windows are resized small; font size adjustable (Small / Medium / Large) via the Settings menu
- **Resizable Panels** — Drag the splitter bars inside the Order Book window to resize the DOM ladder, order entry form, and bottom tabs independently

---

## Requirements

### System

| Dependency | Version | Notes |
|---|---|---|
| C++ Compiler | C++20 | GCC 11+, Clang 13+, MSVC 2022+ |
| CMake | 3.20+ | |
| Vulkan SDK | 1.3+ | Must include validation layers and ICD loaders |
| GLFW3 | 3.3+ | System-installed |
| Protobuf | 3.21.x | System-installed (`libprotobuf-dev`) |

### Linux (Debian/Ubuntu)

```bash
sudo apt install libvulkan-dev vulkan-validationlayers \
                 libglfw3-dev libprotobuf-dev protobuf-compiler \
                 cmake build-essential
```

### macOS (Homebrew)

```bash
brew install vulkan-headers molten-vk glfw protobuf cmake
```

### Windows

Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) and ensure CMake and a C++20 compiler (MSVC or MinGW) are on your PATH. GLFW and Protobuf can be installed via vcpkg.

---

## Install Interactive Brokers API

The IB TWS API (GPLv3 as of 10.4x) is **vendored in-tree** at `vendor/twsapi/`,
so no download is needed to build — a plain checkout compiles. The committed
version is recorded in [`vendor/TWSAPI_VERSION`](vendor/TWSAPI_VERSION).

Only the protoc-generated protobuf (`.../client/protobufUnix/*.pb.*`) is left out
of git and regenerated at build time so it matches your local `libprotobuf`. The
CMake build (and each CI job) runs `protoc` over `IBJts/source/proto` automatically.

> To upgrade the API version, see the upgrade note in
> [`vendor/TWSAPI_VERSION`](vendor/TWSAPI_VERSION).

---

## Build

```bash
# Configure (Release by default)
cmake -B build -S .

# Build (parallel)
cmake --build build -j$(nproc)

# Run
./build/ibkr-trading-app

# --- Variants ---

# Debug build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Clean build
rm -rf build && cmake -B build -S . && cmake --build build
```

### Linux headless / virtual display

If running on a machine without a physical display (e.g., a server or WSL), set a virtual display before launching:

```bash
DISPLAY=:1 ./build/ibkr-trading-app
```

### Platform notes

- **Windows** — MSVC or MinGW; CMake handles Vulkan/ImGui linking automatically
- **Linux** — Ensure ICD loaders are configured (`/etc/vulkan/icd.d/`) and `DISPLAY` is set
- **macOS** — Requires Xcode command line tools; MoltenVK provides Vulkan over Metal

---

## Testing

The test suite uses [Catch2 v3](https://github.com/catchorg/Catch2) and is fetched automatically by CMake. No extra install step needed.

### Run tests locally

```bash
# Configure with tests enabled
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DIBKR_BUILD_TESTS=ON

# Build
cmake --build build -j$(nproc)

# Run all tests
ctest --test-dir build --output-on-failure
```

### Test targets

| Target | What it covers |
|---|---|
| `tests-core` | Pure logic, no IB API or UI: timeframe / session helpers, model defaults, chart analysis (S/R, VWAP, volume profile, setups, order impact), trading styles, the replay engine, config file parsing, option chain math (expected move, IVx, payoff, pricing, ticks, brackets), and portfolio strategy grouping |
| `tests-ibkr` | IBKRClient message dispatch: inject `IBMessage` variants into the queue, assert callbacks fire correctly — no live IB connection required |

### Sanitizers (Linux)

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DIBKR_BUILD_TESTS=ON -DIBKR_SANITIZE=ON
cmake --build build --target tests-core -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  ./build/tests/tests-core
```

### CI

All three platform jobs (Linux, macOS, Windows) build and run the full test suite on every push and pull request. A dedicated `sanitize-linux` job runs `tests-core` under AddressSanitizer + UBSanitizer after the main Linux build passes.

> UI rendering (ImGui/Vulkan) and live IB Gateway connectivity are not covered by automated tests — these require a real display and a running IB session.

---

## IB Gateway / TWS Setup

The app connects to either **IB Gateway** or **Trader Workstation (TWS)**. You must enable API access before connecting.

### Enable API in TWS

1. Open TWS → **Edit → Global Configuration → API → Settings**
2. Check **Enable ActiveX and Socket Clients**
3. Set **Socket port** (see table below)
4. Uncheck **Read-Only API** if you want to place orders
5. Add `127.0.0.1` to **Trusted IP Addresses** (or your machine's IP)

### Enable API in IB Gateway

1. Open IB Gateway → **Configure → Settings → API → Settings**
2. Same steps as TWS above

### Port Reference

| Account Type | Application | Port |
|---|---|---|
| Live | TWS | 7496 |
| Paper | TWS | 7497 |
| Live | IB Gateway | 4001 |
| Paper | IB Gateway | 4002 |

### Gateway / TWS version

Use IB Gateway or TWS **10.50 or newer** for option combos. Version 10.45 silently drops collars and risk reversals sent through the API (no acknowledgement, no error); 10.50 accepts them.

### Client ID

Each connection to IB requires a unique **Client ID** (integer). If you connect multiple programs simultaneously, use different Client IDs to avoid conflicts. The app defaults to `1`.

---

## Login Window

On launch, a login dialog appears before the trading UI loads.

| Field | Description |
|---|---|
| **Host** | Hostname or IP of TWS/Gateway (default: `127.0.0.1`) |
| **Port** | Auto-populated when you toggle Account Type and API Type |
| **Client ID** | Unique integer per connection (default: `1`) |
| **API Type** | Toggle between **TWS** and **IB Gateway** (updates default port) |
| **Account** | Toggle between **Live** and **Paper** (updates default port) |

Click **Connect**. The app waits for `nextValidId` from IB (which signals the connection is ready) before showing the trading UI. If connection fails, an error message is displayed with the IB error code.

On **live sessions with multiple accounts**, an account selector modal appears after the connection handshake completes. The selected account is displayed in the menu bar and stamped on every order placed during the session. The Portfolio's account cards, positions and value-over-time curve use the selected account's figures only. The Orders window lists every account's orders with an **Account** column and, when more than one account has orders, an account filter above the tabs. **Settings → Accounts** takes a name per account; it is shown next to the account code ("Main (U1234567)") in the menu bar, the account selector, the Orders window and the Portfolio's Trade History, which has an Account column too (its filter box matches a symbol or an account).

---

## Windows

The UI uses ImGui's docking system. All windows are dockable and can be rearranged freely.

### Multi-Instance Windows

Chart, Order Book, Scanner, News, Watchlist, and Replay windows support up to **10 simultaneous instances** each. Open additional instances from the **Windows** menu (**+ New Chart**, **+ New Order Book**, and so on). Options Chain, Strategy Analysis, Portfolio, Orders, WSH Calendar and Notifications are single windows, toggled from the same menu. Each instance has an independent symbol subscription and its own IB reqId range, so they never interfere with each other.

### Window Groups & Symbol Sync

Every window has a **group button** (`G1` … `G10`, or `G-` for none) at the leftmost position of its toolbar.

- Click the button to assign the window to a group (or clear it with `G-`).
- When you change the asset in any grouped window — by typing a symbol in the chart search box, changing the symbol in the Order Book, or double-clicking a row in the Scanner — **all other windows in the same group immediately switch to that asset** and re-subscribe to live market data.
- Clicking a position in the Portfolio, or loading a symbol in the Options Chain, also switches the windows in that group.
- G1–G4 are color-coded: G1 = blue, G2 = green, G3 = orange, G4 = purple. G5–G10 are grey.
- By default, instance N starts in group N (e.g. Chart 1, Order Book 1, Scanner 1, News 1 all start in G1).
- Group choices are saved per window. Syncing with TWS's own display groups (Settings) covers G1–G4.

### Layout Presets

The **Presets** menu applies one-click workspace configurations:

| Preset | Windows shown |
|---|---|
| Trading Focus | Chart 1, Order Book 1, Orders |
| Research | Chart 1, Scanner 1, News |
| Full Desk | Chart 1, Order Book 1, News (G2), Scanner (G2), Portfolio, Orders, Watchlist, Options Chain, Strategy Analysis |
| Options | Options Chain, Strategy Analysis, Portfolio, Orders, Scanner, Watchlist |

### Chart Window

Real-time candlestick charting with technical analysis overlays.

**Timeframes:** 1m, 5m, 15m, 30m, 1h, 4h, 1D, 1W, 1M

**Auto price scale:** with **Auto** ticked (the default, next to the `[+]` / `[-]` zoom buttons) the price axis fits the bars in view as you pan and zoom. Drag or scroll on the price axis to scale it by hand (this unticks Auto); double-click the price axis to switch it back on.

**Indicators (toggleable):**
- SMA 20, SMA 50 (periods configurable)
- EMA 20 (period configurable)
- Bollinger Bands (period and sigma configurable)
- VWAP (resets intraday)
- RSI (separate subplot, period configurable)
- Volume subplot with up/down coloring

**Drawing Tools:** Horizontal lines, trendlines, Fibonacci retracements, eraser

**Trading:**
- Place orders directly from the chart (MKT, LMT, STP, STP LMT, Bracket, TRAIL, TRAIL LIMIT, MOC, LOC, MTL, MIT, LIT, MIDPRICE, REL)
- **Bracket** — three-click chart placement: click 1 sets the **LMT entry**, click 2 the **STP stop-loss**, click 3 the **TP take-profit**. Entry is placed first so the STP/TP cursor bubbles can show the projected $ loss / gain, % move, and live R:R against the locked entry as the user positions each leg. Each click is side-validated (BUY: STP < entry < TP; SELL reversed) and out-of-side clicks are silently rejected so the user can reposition. The LMT entry is submitted on the third click; the STP and TP are submitted as an OCA pair (`ocaGroup="BRK_<entryId>", ocaType=1`) only when IB reports the LMT filled (via `onFillReceived`) — when one of the closing legs fills, IB auto-cancels the other. Cancelling the LMT before fill discards the pending STP+TP.
- Working orders displayed as horizontal lines on the price axis
- Current position shown with entry price, current price, and unrealized P&L strip
- **Current price line** — dashed horizontal line tracking the latest price, with a right-aligned price tag inside the chart
- RTH toggle to include or exclude pre/post-market bars

**Corporate event markers:** Upcoming earnings, dividends, and splits from Wall Street Horizon appear as vertical dashed lines on the price chart, colour-coded by type, with a hover tooltip showing date, type, description, and importance.

**Session bands:** Chart shades premarket, regular hours, after-hours, and overnight regions.

**Symbol history:** Last 10 symbols are remembered for quick switching.

---

### Trading Window (DOM)

Professional Depth of Market ladder for market microstructure analysis and fast order entry.

**Layout** — Three resizable panels separated by draggable splitter bars:
- **Left**: DOM ladder (drag the vertical splitter to resize)
- **Right**: Order entry form
- **Bottom**: Tabbed panel (drag the horizontal splitter to resize)

**Order Book:**
- 5 to 300 bid/ask price levels (Level II, set with the Levels dropdown) with per-exchange depth when available
- L2 "All" filter merges all exchange buckets into a single view, sorted correctly (bids high→low, asks low→high)
- Cumulative size from the best price, volume-at-price overlay from executed trades
- L1/L2 toggle with exchange filter dropdown
- Your position is marked on the ladder at its entry price (size @ average price), and rows with a working order are shaded
- The ladder follows the spread; scrolling it by hand pauses the follow for a few seconds
- Right-click a header to show, hide or reorder columns

**Interactive order placement (via IBKR API):**
- With **Click-to-Trade** on, click the left (Bid) column of any price row to place a BUY limit there, or the right (Ask) column for a SELL limit, at the quantity in the order form
- Select order type: MKT, LMT, STP, STP LMT, TRAIL, TRAIL LIMIT, MOC, LOC, MTL, MIT, LIT, MIDPRICE, REL
- Select time-in-force: DAY, GTC, IOC, FOK, Overnight, OPG (on-open)
- BUY / SELL buttons confirm submission

**Tabs:**
- **Open Orders** — Every working, partially-filled, and cancelling order for the window's symbol (including ones placed from a chart or an earlier session) with cancel button; click a quantity, price or TIF cell to change the order in place; status badge covers the full lifecycle (PENDING → WORKING → PARTIAL → CANCELLING → FILLED/CANCELLED/REJECTED)
- **Execution Log** — Filled orders with commission and realized P&L
- **Time & Sales** — Live tape of last 2,000 tick-by-tick trades (IB `reqTickByTickData`); columns: Time, Price, Size, volume histogram bar, Exchange / Conditions; green/red/grey row tinting for uptick/downtick/neutral

---

### Options Chain Window

Expirations and strikes for one underlying, with an order ticket for single options and multi-leg strategies. Open it from **Windows → Options Chain**.

**Loading a chain:** type a symbol (stock, ETF, or an index such as SPX, NDX, RUT, VIX) and press **Load Chain**, or let a grouped window send the symbol. Expirations appear as tabs with days to expiry. The table mirrors calls and puts around the strike column, shades in-the-money strikes, marks the spot price and the ±1 SD expected move, and shows a signed quantity pill on any strike where you hold a position. **Cols** picks the greek and price columns.

Only the strikes on screen (and those in the ticket) hold a live quote, up to 72 at a time; scrolling moves the subscriptions. Closing the window stops the stream.

**Building an order:**
- Click an **ask** to buy that option or a **bid** to sell it. Click more cells to add legs (up to 6); click the same cell again to remove one.
- **+ Strategy** fills the ticket from a template around the money: verticals, straddle, strangle, risk reversal, synthetic, butterfly, broken-wing butterfly, iron condor, calendar, diagonal, buy-write (covered call), collar, conversion, reversal.
- **+Buy 100** / **+Sell 100** add the underlying shares as a leg, for a covered call, married put or any other stock + option order (not for an index, which has no tradeable share).
- Each leg has steppers for strike and expiry, a ratio, and click-to-flip Buy / Sell and Call / Put. A tag next to it says whether it would **open**, **add** to, **close** or **flip** what you already hold in that contract.

One leg is sent as a plain option order. Two or more go to IB as a single combo at a signed net price: positive for a debit, negative for a credit.

**Before sending,** the ticket shows:
- Bid / mid / ask for the leg, or the net bid / mid / ask of the combo, as buttons that set the limit. Prices are snapped to the contract's tick.
- Max profit, max loss, extrinsic value, net delta and theta.
- A warning if the limit would fill immediately, is far through the market (a likely typo), or is a debit on a spread that trades as a credit.

**Review & Send** opens a confirmation listing every leg; **Check margin** there asks IB what the order would do to your margin and commission without placing anything. **Transmit Instantly** skips the confirmation and is off by default.

**Take-profit / stop-loss:** tick **Close At Profit** and / or **Stop Loss** on the ticket to attach exits to the order, as a price or a percentage of the premium. IB holds them until the entry fills, then works them as a pair: when one fills the other is cancelled. They stay at IB if the app is closed.

**Analysis** opens the Strategy Analysis window for the ticket.

### Strategy Analysis Window

A P&L graph for the order in the Options Chain ticket, or for a position pinned from the Portfolio (**right-click → Analyze**).

- **At expiry** (orange) — the payoff line, with profit and loss shading, strike gridlines, the spot price and break-even points.
- **Today** (blue) — the theoretical value before expiry from Black-Scholes and each leg's implied volatility. The **Evaluate at date** slider moves it forward in time toward the expiry line.
- **Prob** — a probability cone for the underlying at expiry, with estimated **POP** (probability of any profit) and **P50** (probability of at least half the maximum profit). These are reference estimates from a simple lognormal model, not a forecast.
- Hover the graph for the P&L at any price; switch between **Total P&L** and **Per-contract P&L**; zoom with **[+]** / **[-]** / **Fit**.

For calendars and diagonals, whose legs expire on different dates, only the theoretical curve is shown.

---

### Settings

Open via **Settings** in the menu bar. A floating panel lets you change the font size:

| Option | Scale |
|---|---|
| Small | 0.85× |
| Medium | 1.0× (default) |
| Large | 1.5× |

All UI elements — text, widgets, padding, and spacing — scale uniformly. The setting takes effect immediately without restarting.

The same panel sets the **default trading style** for new charts and toggles **Sync with TWS Display Groups** (G1–G4 follow, and drive, the linked windows in TWS).

### Saved State

Everything the app remembers lives in `~/.config/ibkr-trading-app/` (on Windows, `%USERPROFILE%\.config\ibkr-trading-app\`): window layout and docking, which windows are open, per-window settings and group, watchlists and presets, chart symbols and styles, portfolio strategy groupings, the account value history, and order history. Delete the folder to start from a clean workspace.

---

### News Window

Multi-source financial news with three tabs. Supports up to **10 simultaneous windows instances**, each independently grouped.

**Market Tab** — Live headlines as they arrive. Each enabled news provider is subscribed as a market-wide feed (every headline it publishes, not one stock's), alongside a few seed symbols. Settings → News providers shows per provider whether IB delivers such a feed: **live**, **subscribed** (no headline yet) or **no live feed**.

**Portfolio Tab** — Historical news filtered to your current positions. Populated automatically when positions load after connection.

**Stock Tab** — Enter any symbol to search historical news archives. Click a headline to load the full article body on demand.

**Features:**
- Sentiment indicator per article (Positive / Negative / Neutral)
- Source attribution (Dow Jones, Briefing.com, etc.)
- Time-ago formatting ("5 min ago", "2 hrs ago")
- Filter by headline text

**Free news providers included:** `BRFUPDN`, `BRFG`, `DJ-N`, `DJNL`, `DJ-RTA`, `DJ-RTE`, `DJ-RTG`, `DJ-RTPRO`

> A market data subscription from IB may be required for some providers. Delayed/free tier still works for many sources.

---

### Scanner Window

Market scanning across stocks, indexes, ETFs, and futures.

**Preset scans:**

| Preset | Description |
|---|---|
| Top Gainers | Largest % gain today |
| Top Losers | Largest % loss today |
| Volume Leaders | Highest share volume |
| New 52W Highs | Stocks at 52-week high |
| New 52W Lows | Stocks at 52-week low |
| RSI Overbought | RSI >= 70 |
| RSI Oversold | RSI <= 30 |
| Near Earnings | Upcoming earnings reports |
| Most Active | Dollar volume leaders |
| Custom | User-defined scan code |

**Filters:** Price range, % change, volume, market cap, RSI range, sector, exchange

**Results table:** 16 sortable columns including symbol, company, price, change, volume, relative volume, market cap, P/E, RSI, MACD, ATR and 52-week high / low distance; right-click a header to show, hide or reorder them. Every row gets a live quote, and the indicators are computed from daily bars. Gainers highlighted green, losers red. Portfolio holdings are marked. A trend sparkline per row shows the last 30 daily closes.

Auto-refreshes every 30 seconds (configurable). Falls back to simulated data when IB is not connected (useful for UI testing).

---

### Portfolio Window

Full account and position dashboard.

**Real-time P&L header** — Account-level daily P&L, unrealized P&L, and realized P&L streamed live from IB and shown above the positions table.

**Summary cards (top row):**
- Net Liquidation Value
- Cash Balance
- Day P&L ($ and %)
- Total P&L (unrealized + realized)
- Buying Power

**Positions table:** Symbol, quantity, avg cost, current price, market value, cost basis, unrealized P&L ($ and %), realized P&L, day P&L, day change, portfolio weight. All columns sortable; right-click a header to show, hide or reorder them. Clicking a symbol loads it into the chart, Order Book and other windows of the Portfolio's group.

**Option strategies:** With **Group** on, option legs are shown as strategy rows (vertical, straddle, calendar, butterfly, iron condor, covered call, collar, conversion, …) with the legs nested underneath and net cost, mark and P&L on the strategy row. Combos sent from this app are grouped exactly; anything else is inferred from the legs and marked with a leading `~`, because IB reports only net positions. Right-click to:
- **Ungroup legs** / **Re-group**, or Ctrl+click several legs and **Group as strategy** to override the grouping
- **Roll…** — stage a combo in the Options Chain that closes the position and reopens it on the next expiry
- **Protect (TP / SL)…** — place a take-profit and / or stop-loss for the position
- **Analyze** — open the position in the Strategy Analysis window, measured from its real entry cost

**Charts:**
- Account value over time (built from the moment you first connect and saved per account — IB's API has no history for it)
- Portfolio allocation donut (by market value, top holdings labeled)

**Bottom tabs:**

- **Trade History** — Closed trades with side, qty, price, commission, realized P&L, and timestamp. Searchable.
- **Performance** — Key metrics: Total Return, YTD, MTD, Day, Sharpe Ratio, Max Drawdown, Win Rate, Avg Win/Loss, Profit Factor, Beta, Alpha, Volatility
- **Risk & Margin** — Advanced drawdown analysis and risk metrics

---

### Orders Window

Live order blotter with two tabs.

**Open Tab** — All submitted, working, partially-filled, and cancelling orders. Shows order type, quantity, limit/stop/aux prices, TIF, filled qty, avg fill price, commission, submission time, and a color-coded status badge. Cancel button per order.

- **Change an order in place** — click its quantity, price or TIF; **Update** sends the change, **x** discards it. Clicking the price also opens a ladder with the live bid / mid / ask (for a combo, the net of its legs) to pick a price from.
- **Combo orders** are named by strategy ("SPY Oct16 600/605 Bull Call"); hover the name for the legs.
- **Brackets** — a bracket's entry, take-profit and stop-loss sit under one collapsible row with **Cancel all**. Right-click a working option or combo order → **Attach TP / SL…** to add exits to it.
- An order IB has not answered after 5 seconds is flagged **NO REPLY**; an order IB accepted but is holding shows **HELD** with IB's reason.
- **Orders placed in TWS** (or another session) are listed too, marked **TWS** and read-only: IB lets only the session that placed an order change or cancel it. The list is re-read every 5 seconds, so they appear, update and disappear within that time.

**History Tab** — Filled, cancelled and rejected orders from the last 7 days, kept across restarts. Newest first by default; click a column header to sort by it. Rows from earlier days show their date. Orders still open, and today's fills from other sessions, are recovered from IB via `reqAllOpenOrders` and `reqExecutions`. A filter toolbar (symbol, side, date-from, Load/Clear buttons) queries IB for historical fills beyond the current session; results appear with an amber tint to distinguish them from live-session captures.

---

## Connection Resilience

If IB Gateway or TWS closes unexpectedly while the app is running:

- The trading UI **stays open** with last-known chart data and positions still visible.
- An orange **DISCONNECTED** badge appears in the menu bar next to the account selector.
- The app **automatically retries** the connection every 5 seconds in the background.
- When Gateway comes back up, the app reconnects silently and re-subscribes all open chart and order book windows to live data — no need to restart or re-enter credentials.
- **Order history is recovered** — open orders from all client sessions and today's fill history are re-fetched automatically on reconnect.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      main.cpp                           │
│  Vulkan/GLFW init · Login state machine · UI dispatch   │
│  Entry structs (ChartEntry, TradingEntry, ScannerEntry) │
│  BroadcastGroupSymbol · SpawnXxxWindow · WireIBCallbacks│
└────────────────────────┬────────────────────────────────┘
                         │
         ┌───────────────▼───────────────┐
         │         IBKRClient            │
         │  EWrapper + EClientSocket      │
         │                               │
         │  EReader thread               │
         │    └─ IB callbacks            │
         │         └─ push to queue      │
         │                               │
         │  Send thread                  │
         │    └─ PlaceOrder/CancelOrder  │
         │                               │
         │  UI thread (ProcessMessages)  │
         │    └─ drain queue (5ms budget)│
         │         └─ invoke callbacks   │
         └──┬────────────────────────────┘
            │  Callbacks routed by reqId to entry vectors
   ┌────────┼──────────────────────────────────────────────┐
   │        │                  │              │             │
   ▼        ▼         ▼        ▼           ▼             ▼
Chart×10 Trading×10 News×10 Scanner×10 Portfolio     Orders
(G1-G10) (G1-G10) (G1-G10) (G1-G10) (singleton) (singleton)
```

Watchlist and Replay windows are multi-instance in the same way; Options Chain, Strategy Analysis, WSH Calendar and Notifications are singletons like Portfolio and Orders.

**Threading model:**
- The IB EReader runs on its own thread and pushes typed messages (`std::variant`) into a lock-free queue.
- A dedicated send thread handles socket writes for order submission.
- The UI thread drains the queue during `ProcessMessages()` (called once per frame, 5ms budget) and invokes the corresponding `std::function` callbacks that update window state.
- This prevents any IB socket I/O from blocking the render loop.

---

## License

The application's own source code is licensed under the MIT License — see the [LICENSE](LICENSE) file.

### Bundled Interactive Brokers TWS API (GPLv3)

The repository includes the C++ client of the Interactive Brokers TWS API under [`vendor/twsapi/`](vendor/twsapi/) (version in [`vendor/TWSAPI_VERSION`](vendor/TWSAPI_VERSION)).

- It is © Interactive Brokers LLC and licensed under the **GNU General Public License, version 3 or later** — see [`vendor/twsapi/IBJts/LICENSE`](vendor/twsapi/IBJts/LICENSE). It is not covered by this project's MIT license.
- It carries one documented local change: a two-line compatibility shim in `CommonDefs.h` that restores the `OrderId` / `TickerId` type names. Everything else is as published by IBKR; only the C++ client and `.proto` sources are kept.
- The application links this code statically, so a compiled binary contains GPLv3 code. If you pass binaries on to others, the GPLv3 applies to that distribution: include the license text and make the complete corresponding source available. The MIT license on the application's own code is compatible with that.

Other dependencies (Dear ImGui, ImPlot, GLFW, GLM, Protocol Buffers, miniaudio, Catch2) keep their own licenses.

This is a description of what the repository contains, not legal advice.

### Interactive Brokers API Usage Notice

- The software is provided for personal, educational, and research purposes, and for use with the user’s own IBKR account.

- It is not a brokerage service, investment advisory tool, or financial institution, and does not provide investment advice or recommendations.

- Users are solely responsible for any trading activity executed through their IBKR account.

- The application requires a locally running IBKR Trader Workstation (TWS) or IB Gateway instance.

- This software is not certified for production or mission-critical trading environments. Users should evaluate suitability before live use.

- Use of IBKR accounts, market data and the TWS API remains subject to IBKR’s own agreements, license terms and policies.
