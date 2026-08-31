# Stability, concurrency & live-testing fixes

Two passes on top of `main`: a static review of the highest-risk subsystems, and a
live-testing round against a Paper Gateway (`172.24.0.1:4002`) that surfaced a batch
of real UI/UX and IB-integration bugs. **21 files, +1007/−375.** Build clean.

> Scope: `src/` only. Any local changes under `twsapi_macunix.1037.02/` are build
> scaffolding to compile without the encrypted TWS blob and are **not** part of this PR.

---

## 1 — Concurrency & lifetime (crash / corruption class)

- **Concurrent socket writes → data race.** `IBKRClient` wrote to the single IB
  socket from both the send thread and the UI thread (38 `Req*`/`Cancel*` methods).
  Interleaved encoding corrupted the wire message. **Fix:** `m_socketMutex` guarding
  every direct method + the `SendLoop` batch drain.
- **Use-after-free on disconnect.** `onConnectionChanged` deleted the client *inside*
  its own `ProcessMessages()` dispatch. **Fix:** deferred delete via
  `g_clientDeletePending`, performed after the dispatch returns.
- **Blocking connect froze the whole app.** `eConnect()` ran on the UI thread, so an
  unreachable/unapproved Gateway hung the window. **Fix:** connect runs on a worker
  thread; the UI shows a responsive *Connecting… → Error* state and stays interactive.

## 2 — Order correctness

- **Duplicate order id (IB 103).** `nextValidId` was regressed by open orders from
  other client-ids / prior sessions. **Fix:** monotonic `EnsureNextOrderIdAtLeast`.
- **Bracket OCA pairing** kept intact on leg-drag (modify-in-place, not cancel+replace);
  `openOrder` now mirrors OCA/parent/account fields so drags don't desync.
- **After-hours bracket stops** carry `outsideRth` correctly; IB "held" warnings surface
  as an amber `HELD` chip instead of a false rejection.

## 3 — Symbol autocomplete (Chart / Order Book / Replay)

The search field only worked for the first lookup of a session, and only on one field.
Root causes and fixes:

- **`reqMatchingSymbols` used a fixed reqId (8000).** IB silently ignores a re-issue on
  an already-used id, so only the first search ever returned results. **Fix:** rotate
  the reqId through a pool (8200–8299).
- **Single shared search state** across all fields. **Fix:** per-field
  `SymbolSearchState` (window member) with results routed to the field that searched.
- **Dropdown spawned as a focus-stealing OS window** under multi-viewport, deactivating
  the input every keystroke. **Fix:** pin the dropdown to the field's viewport.
- **Typing mutated the live symbol.** **Fix:** separate edit buffer; the symbol only
  changes on an explicit commit (Enter / row-click / click-away), with select-all-on-click
  so typing replaces. Replay's field now uses the same widget.

## 4 — UI / layout

- **Portfolio:** sanitize IB `DBL_MAX` P&L sentinel (was rendering `+$1797…e308`);
  content-driven summary cards (no clipping, no duplicate subtitles); Risk & Margin as a
  single list; trimmed footer gap.
- **Chart:** RSI sub-chart bottom margin + un-clipped current-value label; symbol change
  no longer self-clobbers via the group broadcast.
- **Replay:** reserve space so the status bar + bottom tabs aren't hidden under the volume
  plot; receives the group-1 symbol broadcast.
- **News:** window-local text wrapping + accurate expanded-row height (no more raw `<p>`
  overflow).
- **Scanner:** corrected Indexes/ETFs IB instrument codes ("No instrument specified").

---

### Testing
Static review + live paper-Gateway verification of the read path, order lifecycle,
concurrent-send stress, reconnect, and the symbol/scanner/replay flows. Pure-logic
suites (`tests-core`) remain green. GUI/live paths verified manually (headless CI can't
run them).

### Not fixable in code (IB entitlement on the test account)
News feed (`10276`), L2 depth (`10092`), certain scanner presets/futures permissions
(`492` / disabled codes), Dow Jones news provider (`321`).
