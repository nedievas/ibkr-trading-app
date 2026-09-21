# Options Bracket Orders (entry + Close-At-Profit + Stop-Loss)

> **Status: COMPLETE — OB-1..OB-9 landed and verified live (2026-09-21).**
> Shipped 1.5.10–1.5.20. The OB-9 §6 checklist passed on a live paper Gateway:
> combo STP LMT accepted, close-net sign correct, OCA cancel-survivor observed,
> parentId-on-working-combo (Case A) + protect netting (Case B) OK, restart
> survival, entry-modify-safe. Two live-found fixes: 1.5.19 (snap child prices to
> the combo net tick — IB error 110) and 1.5.20 (sign-aware Est. P/L so a
> protective stop on a winner reads as a gain). See task-history.md for the run.


Add a **Bracket** advanced-order mode to the Options Chain order ticket, for both
single-leg options and multi-leg combos (verticals, iron condors, …). A bracket
attaches two independently-toggled protective children to the combo entry:

- **Close At Profit** (take-profit / TP) — a limit order that closes the position
  at a favorable net.
- **Stop Loss** (SL) — a stop or stop-limit that closes the position at an
  adverse net.

Reference UI = tastytrade's Bracket ticket (screenshot): the leg cart + entry
net on the left, two stacked child boxes (green TP, red SL) on the right, each
with its own **enable checkbox**, a Limit/Stop price with `$`/`%` toggle, a
"% from entry price" readout, its own TIF, and a live Est. Profit / Est. Loss.

**Attach method: native IB attached bracket** (chosen). Entry + TP + SL are
submitted together as one IB bracket — children carry `parentId = entryId`, share
an OCA group, and the transmit flag activates the chain on the last child. IB
holds the children server-side, so they **survive an app restart and protect a
resting/unfilled entry** — the right behaviour for GTC verticals, which is the
common case. (Contrast: ChartWindow's stock bracket fires the children
client-side on entry fill; that's simpler but leaves a resting entry unprotected
and is lost if the app closes before the fill. We do not reuse it here.)

Scope: Options Chain window only (stocks/ETFs/index underlyings). The stock
ChartWindow bracket is unchanged. Cross-expiry (calendar/diagonal) entries are
allowed but their $-based P/L readouts are single-expiry approximations (same
caveat the ticket already shows).

---

## 1. What the user asked for

- Bracket for **options and option combos**.
- **Separate checkbox** to enable TP and SL independently (both / either / none).
- **Remember the checkbox state across restart** — "I'll more often add TP to a
  vertical spread, so it should come back checked next launch." Persist the TP/SL
  enables plus their `$`/`%` mode, default percent, stop type, and TIFs.
- **Percent presets on the TP price**: 10 / 25 / 50 / 75 % (and the same on SL) —
  a quick-set row, with a live "% from entry price" readout, mirroring the
  screenshot's `$`/`%` toggle.

---

## 2. Price ↔ percent model (the pure, testable core)

Let `E = |entryNet|` be the magnitude of the staged entry net premium
(`m_ticketLimit`, signed debit `+` / credit `−`), `mult` the option multiplier
(usually 100), `qty` the number of combos.

For a child at percent `p` (0..1) away from entry:
- **Est. Profit (TP)** `$ = p · E · qty · mult`  → readout "`p%` from entry price".
- **Est. Loss (SL)**   `$ = p · E · qty · mult`.

Screenshot check: `E = 0.06`, `mult = 100`. TP `p = 0.1667` → `1.00 cr` (16.67%);
SL `p = 0.3333` → `2.00 db` (33.33%). ✔

Close-price magnitude for the *flipped* (closing) combo:
- Debit strategy (you own it): TP sells higher `E·(1+p)`, SL sells lower `E·(1−p)`.
- Credit strategy (you're short it): TP buys back cheaper `E·(1−p)`, SL buys back
  dearer `E·(1+p)`.

`$` mode: the user types the absolute close net; the readout computes
`p = |close − E| / E`. `%` mode: the user picks `p` (typed or a 10/25/50/75
preset) and the close price is derived. Both values are kept live and displayed,
as in the screenshot.

All close prices are snapped with `core::services::RoundToTick` (default $0.01;
respects the contract's real minTick when known) so IB doesn't reject with
error 110.

**Pure helpers** (add to `core/services/OptionChain.h`, `[options][bracket]`
tests — this is the only new pure logic):
```cpp
double BracketClosePrice(double entryNetMag, double p, bool isTakeProfit,
                         bool creditStrategy, double tick);   // magnitude
double BracketPctFromPrice(double entryNetMag, double closeMag); // inverse ($ mode)
double BracketEstPnL(double entryNetMag, double p, int qty, double mult);
```
The *signed* net actually sent to IB for the flipped combo is derived from the
magnitude + the entry's sign convention — see §4, pinned by the live test in §6.

---

## 3. UI — Options Chain ticket (`OptionsChainWindow`)

**No mode selector — the checkboxes are the mode.** Every option ticket (single
leg or combo) always shows the two child boxes; the TP/SL enable checkboxes
decide whether it sends plain or as a bracket. Neither ticked → the existing
`OnOrderSubmit(entry)` plain path (byte-identical to today); either/both ticked →
`OnBracketSubmit(entry, children)` (OB-2). To stay uncluttered for a quick
single-leg order, each box **collapses to just its header row when unchecked**
and expands when ticked. New members:

```cpp
bool  m_tpEnabled      = false;  // persisted OPT_BRK_TP_ON  (the "mode")
bool  m_slEnabled      = false;  // persisted OPT_BRK_SL_ON
bool  m_tpPctMode      = true;   // true = %, false = $        (OPT_BRK_TP_MODE)
bool  m_slPctMode      = true;   //                            (OPT_BRK_SL_MODE)
double m_tpPct         = 0.50;   // default 50%                (OPT_BRK_TP_PCT)
double m_slPct         = 0.50;   //                            (OPT_BRK_SL_PCT)
double m_tpPrice       = 0.0;    // resolved close net ($ mode / display)
double m_slTrigger     = 0.0;
double m_slLimit       = 0.0;
int    m_slStopType    = 1;      // 0 = Stop, 1 = Stop Limit   (OPT_BRK_SL_TYPE)
int    m_tpTifIdx      = 1;      // default GTC                (OPT_BRK_TP_TIF)
int    m_slTifIdx      = 1;      //                            (OPT_BRK_SL_TIF)
```

**Shared widget.** The TP + SL boxes are factored into one reusable component
(e.g. `ui::BracketChildForm` — a small header-only or `.h/.cpp` helper, like
`DrawGroupPicker` / `DatePicker`), rendering the two boxes given an *entry-net
reference* + multiplier + qty and returning the enabled/priced state. It is used
three ways with identical look + math, differing only in the entry-net source and
how children attach:
1. inline in the chain ticket's right column (entry-time bracket, transmit-chain);
2. the Portfolio **"Protect (TP / SL)…"** popup (Case B, §7b — entry-net = the
   position's net avg cost; standalone OCA closers);
3. the Orders/DOM **"Attach TP / SL…"** popup (Case A, §7b — entry-net = the
   working order's limit; children `parentId = workingId`).

The ticket's right column always shows the shared widget's two child boxes
(replacing the single stats/actions block; the stats strip moves under the entry
column). Unticked boxes render as a single header row (checkbox + dim title);
ticking one expands it:

**Close At Profit** (green header, checkbox in header = `m_tpEnabled`):
- Limit Price input + `▲`/`▼` steppers + `$`/`%` toggle button.
- Preset row: `10% 25% 50% 75%` small-buttons (set `m_tpPct`).
- Right-aligned "`NN.NN%` from entry price".
- TIF combo (Day/GTC, default GTC).
- `Est. Profit  X.XX cr (NN.NN%)`.
- Whole box `BeginDisabled` when unchecked.

**Stop Loss** (red header, checkbox = `m_slEnabled`):
- Stop Order Type combo: `Stop` / `Stop Limit`.
- Stop Trigger Price input (+ steppers, `$`/`%`).
- Limit Price input (only when `Stop Limit`).
- Preset row `10% 25% 50% 75%`.
- "`NN.NN%` from entry price", TIF combo (default GTC).
- `Est. Loss  X.XX db (NN.NN%)`.

`Review & Send` (existing button) now builds the whole bracket. The button label
stays; the disabled-gating extends: at least the entry must be priced, and if a
child is enabled its price must be > 0. If both children are unchecked, a Bracket
is submitted as a plain entry (equivalent to Single).

Changing entry legs / entry net / qty re-derives the two child prices from their
percents (in `%` mode) via `RecomputeTicketMetrics` → new `RecomputeBracketPrices()`.

---

## 4. Order construction & submit

New callback (window builds `core::Order`s; main.cpp stamps identity only, per
the architecture rule that UI never assigns orderIds):

```cpp
// entry = the combo/single entry (as built today). children = TP and/or SL,
// fully built except orderId / parentId / ocaGroup / transmit / account.
std::function<void(const core::Order& entry,
                   const std::vector<core::Order>& children)> OnBracketSubmit;
```

**Window** builds each enabled child from the entry template:
- Copy the entry order (keeps symbol / spec / multiplier / qty).
- **Flip direction**: combo → flip every `spec.comboLegs[i].action` (BUY↔SELL);
  single leg → flip `o.side`.
- **TP**: `type = Limit; limitPrice = <signed close net at m_tpPct/m_tpPrice>`.
- **SL**: `type = m_slStopType ? StopLimit : Stop; stopPrice = <signed SL
  trigger>;` and for Stop Limit `limitPrice = <signed SL limit>`.
- `tif` from the child's TIF combo; `outsideRth` from the after-hours guard (§5).

**main.cpp** `OnBracketSubmit` handler (native attach):
1. `entryId = g_nextOrderId++`; `entry.orderId = entryId`,
   `entry.transmit = children.empty()`, stamp account, register in
   `g_liveOrders` + `OrdersWindow::OnOpenOrder` + `g_pendingLocalAccept`.
2. `ocaTag = "OBR_" + entryId`. For each child `i`:
   `orderId = g_nextOrderId++`, `parentId = entryId`, `ocaGroup = ocaTag`,
   `ocaType = 1`, `transmit = (i == last)`, stamp account, register.
3. `PlaceOrder(entry)`, then each child in order (transmit=true on the last
   activates the chain). Only the last child transmits so IB receives the full
   bracket atomically.

This reuses the existing `parentId` / `ocaGroup` / `ocaType` / `transmit`
plumbing already in `core::Order`, `PlaceOrder`, and `openOrder` (verified
present). No pending-fill map is needed — IB owns the bracket after submit.

**Sign of the flipped-combo net**: the entry sends `o.side = Buy` with signed
legs and a signed net. The closing child flips the leg actions and must send the
matching signed net so IB reads it as the opposite trade at the intended price.
The exact sign (does IB expect the flipped combo's net as `+close` or `−close`?)
is pinned by the live paper test in §6 — this is the one construction detail we
verify rather than assume, consistent with the NonGuaranteed / class-disambig
findings.

---

## 5. Confirm popup + after-hours guard

- `DrawConfirmPopup` gains a bracket layout: entry legs + net, then a TP row
  (`+X.XX, R:R`) and an SL row (`−X.XX`), each with its type/price/TIF. R:R =
  `Est.Profit / Est.Loss` when both are enabled.
- Reuse the existing after-hours detection (`core::BarSession(now)`): outside RTH,
  force the SL to **Stop Limit** with a tick-rounded limit offset and
  `outsideRth = true` on the children (index options trade limited hours too);
  show the orange warning line already used by the bracket confirm.

---

## 6. Combo-stop live-test checklist (the known risk)

IB combo (BAG) **stop** orders trigger on the combo's calculated net price and
are less battle-tested than combo limits. Verify on a paper Gateway before
trusting real orders:

1. **Combo STP / STP LMT accepted** — place a vertical bracket with SL enabled;
   confirm no IB rejection (watch the `[placeOrder … secType=BAG type=STP]`
   stderr line). If IB rejects combo stops outright, fall back to **TP-only
   brackets for combos** (single-leg SL still works) + a status note — TP is the
   user's stated common case, so this degrades gracefully.
2. **Close net sign** — enter a known credit vertical, set TP 50%, confirm the TP
   child's price and the filled P/L match `0.5 · E`.
3. **Credit vs debit** — repeat for a debit vertical (buy-write not applicable to
   cash-settled index).
4. **OCA cancel-survivor** — let the TP fill in the replay/paper and confirm IB
   cancels the SL (and vice-versa).
5. **Restart survival** — submit a resting bracket, kill+relaunch the app,
   confirm the children are still live in IB (native-attach payoff).

---

## 7. Persistence (OptionsChain block, `singleton-settings.cfg`)

Extend `SerializeSettings` / `ApplySettings` with, defaulting to a plain ticket
for upgrading users (all off), but remembering the user's habit once set:

```
OPT_BRK_TP_ON     m_tpEnabled         (the sticky "add TP" habit)
OPT_BRK_SL_ON     m_slEnabled
OPT_BRK_TP_PCT    m_tpPct             (0.01 … 5.0)
OPT_BRK_SL_PCT    m_slPct
OPT_BRK_TP_MODE   m_tpPctMode
OPT_BRK_SL_MODE   m_slPctMode
OPT_BRK_SL_TYPE   m_slStopType        (0=Stop,1=StopLimit)
OPT_BRK_TP_TIF    m_tpTifIdx          (0=Day,1=GTC)
OPT_BRK_SL_TIF    m_slTifIdx
```

Only percents/modes/toggles persist — the absolute close prices are re-derived
from the entry each time (a vertical's premium differs per ticket). So "TP on at
50% GTC" comes back exactly, which is the requested behaviour.

**Stickiness.** Bracket is a ticket *preference*, not per-symbol or per-order
state: the mode + enables + percents/modes/TIFs survive both an app relaunch and
a symbol change / new chain load. What resets is only the staged cart (as today)
and the absolute child prices (re-derived from the next entry's net; the percents
re-apply). A user who wants a plain entry just leaves both child checkboxes
unchecked — no enabled children submits exactly like a plain order (the existing
`OnOrderSubmit` path), and that unticked state persists too.

---

## 7b. Attaching TP/SL to an existing order or held position

Beyond building a bracket at entry time, the user can attach TP and/or SL
(independently) to something that already exists. IB dictates two distinct
paths — the app picks the right one from the target's state:

**Case A — working (unfilled) entry** (Orders "Open" tab / DOM blotter).
The entry is not modified. The app **submits the TP/SL as new child orders
referencing the working order** (`parentId = workingId`, shared OCA,
`ocaType = 1`, `transmit = true`). IB holds them dormant and activates them when
the entry fills. Entry-net reference = the working order's own limit price. This
is the "attach while modifying an open order" the user asked for — surfaced as a
right-click **"Attach TP / SL…"** on an editable working-order row in the
OrdersWindow `##open` blotter (gated to OPT/BAG orders), opening the same TP/SL
child boxes from §3 in a small popup. (The TradingWindow blotter is skipped — it
only holds the stock DOM's own orders, so an options bracket there is outside the
options-only scope.) It is *not* wired into the inline price-edit
flow, which only re-places the entry itself — adding children is a separate
submit, per IB.)

**Case B — held position, entry already filled** (Portfolio row / strategy
group). No parent order exists, so the app places the TP/SL as **standalone
closing orders, OCA-linked to each other, `parentId = 0`** — the flipped combo at
the TP/SL net. Entry-net reference = the position's net avg cost (sum of the
legs' signed avg costs; falls back to current mark if avg cost is missing).
Surfaced as a right-click **"Protect (TP / SL)…"** on a position / strategy group
— which also satisfies the previously-deferred "right-click Portfolio → protect"
idea, and shares the qty pills so the user sees what's held.

**The form is a self-contained popup, not the chain window.** The position (or
working order) already carries the full leg set — conIds, strikes, rights,
expiries, qty, avg cost — from the portfolio / order feed, so Case A and Case B
render the shared TP/SL widget (§3) in a compact modal anchored on the invoking
window's viewport. No Options Chain load or symbol change is required, and the
chain's staged cart / subscriptions are never touched. The modal shows a one-line
position/order summary (symbol · strategy label · qty · net avg cost), the TP/SL
boxes, and Send / Cancel.

Both cases: the child close legs are the flipped position/entry combo (same flip
logic as §4), and the two children (when both enabled) are OCA'd so one filling
cancels the other. The dialog carries the same `$`/`%` toggle, 10/25/50/75
presets, per-child TIF, and Est. P/L. The window builds the `core::Order`
children; main.cpp stamps orderId / (parentId for Case A) / ocaGroup / account,
via a shared helper reused from §4's `OnBracketSubmit` (a `parentId` argument of
0 = standalone).

Live-test additions (§6): confirm IB accepts a child `parentId` referencing an
already-transmitted working combo (Case A), and that a netting close via
standalone OCA closers (Case B) reduces the position rather than opening a new
one.

## 7c. Orders-window display — bracket tree

Every bracket (entry-time §4, attached Case A, protected Case B) produces a
parent + one/two children that must read as a group in the blotters. Render them
as a **tree**, reusing the Portfolio window's `TreeNodeEx`-inside-a-table pattern:

- **Parent node** = the entry (Case A/B: a synthetic node when there's no live
  parent — Case B's two OCA closers group under a "Protect <sym> <strategy>" node
  keyed by `ocaGroup`). Collapsible; the aggregate/parent row shows entry
  price/qty/status.
- **Child rows** = TP / SL, indented under the parent, tagged `⤷ TP` / `⤷ SL`,
  tinted by OCA group. Per-child inline price-modify keeps working; a `HELD` chip
  still shows per leg.
- **Grouping key** = `parentId`, falling back to `ocaGroup` for parentless Case B.
- Cancel on the parent node cancels the whole group; cancelling one child leaves
  the sibling (IB's OCA still governs the fill-time cancel-survivor).
- Non-bracket orders stay flat rows. Within a group, rows render in fixed order
  (entry → TP → SL) and only the group sorts — the same table-sort compromise
  Portfolio already accepts.

Implemented in `OrdersWindow` (`##open`), the global blotter where option
brackets live; grouping keys `OBR_` (entry-time / attach), `BRK_` (chart stock
brackets) and `OPR_` (protect) are all handled, so chart stock brackets group
there too. The `TradingWindow` blotter is deferred — it only shows its stock
DOM's own orders (options-only scope), and the tree adds no value there.
(Alternative considered: flat rows + a `↳ TP of #123` link tag + OCA color chip —
keeps sorting pure, weaker visual grouping. Tree chosen for TWS-familiarity.)

## 8. Tasks

- **OB-1** — Pure helpers (`BracketClosePrice` / `BracketPctFromPrice` /
  `BracketEstPnL`) in `OptionChain.h` + `[options][bracket]` tests (screenshot
  values as fixtures: E=0.06 → TP@16.67%=1.00, SL@33.33%=2.00; credit vs debit;
  $↔% round-trip; tick snapping).
- **OB-2** — Data model + `OnBracketSubmit` callback + main.cpp native-attach
  chain builder (parentId / OCA / transmit). No fill-map.
- **OB-3** — Ticket UI: always-present TP/SL boxes (collapse when unticked; the
  checkboxes are the mode — no separate selector), $/% toggle, 10/25/50/75
  presets, "% from entry" readout, per-child TIF, Est. P/L,
  `RecomputeBracketPrices`; Review&Send sends plain when neither is ticked, else
  builds children and calls OnBracketSubmit.
- **OB-4** — Confirm popup 3-leg layout + after-hours guard.
- **OB-5** — Persistence (§7).
- **OB-6** — Attach to a **working order** (Case A, §7b): right-click "Attach
  TP / SL…" on OrdersWindow / TradingWindow open rows → child popup → children
  with `parentId = workingId`. Shared submit helper with §4.
- **OB-7** — Protect a **held position** (Case B, §7b): right-click
  "Protect (TP / SL)…" on Portfolio rows / strategy groups → standalone OCA
  closers.
- **OB-8** — Orders-window bracket tree (§7c) in OrdersWindow (grouped by
  ocaGroup OBR_/BRK_/OPR_, entry→TP→SL under a collapsible node, Cancel-all at
  the node). TradingWindow deferred (stock-only blotter).
- **OB-9** — Live paper test (§6, incl. the Case A / Case B additions), then docs
  (task-history, architecture.md "Bracket Orders", this plan marked LANDED).

Deferred: applying the same $/% TP presets to the stock ChartWindow bracket
(options-only per the user); trailing-stop children.

---

## 9. Files touched

- `src/core/services/OptionChain.h` — 3 pure helpers.
- `tests/test_option_chain.cpp` — `[options][bracket]` cases.
- `src/ui/BracketChildForm.{h,cpp}` (new) — shared TP/SL two-box widget used by
  the chain ticket, the Attach popup, and the Protect popup.
- `src/ui/windows/OptionsChainWindow.{h,cpp}` — ticket UI, child build,
  persistence.
- `src/main.cpp` — `OnBracketSubmit` wiring (native-attach chain), shared child
  submit helper (parentId = 0 for standalone).
- `src/ui/windows/OrdersWindow.{h,cpp}`, `src/ui/windows/TradingWindow.{h,cpp}` —
  right-click "Attach TP / SL…" on working rows (OB-6, Case A).
- `src/ui/windows/PortfolioWindow.{h,cpp}` — right-click "Protect (TP / SL)…" on
  positions / strategy groups (OB-7, Case B).
- `.claude/rules/task-history.md`, `.claude/rules/architecture.md` — docs.
