# Portfolio Strategy Grouping — Authoritative Combo Linkage

Status: planned (2026-09-15). Branch: `feature/options-chain` (PR #1 line).

## 0. Locked decisions (2026-09-15)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Scope this round | **Authoritative links only** — manual "merge arbitrary legs" deferred to a follow-up. |
| 2 | When to record a link | **At submit** — classify verifies the legs are actually held, so an unfilled / rejected / netted-away combo simply never matches and is pruned. |
| 3 | Stock-leg combos | **Group + label generically** — a link may include the underlying stock conId (covered call / collar group as one row); the classifier labels them Custom / covered-call-ish since its namers are option-only today. Proper stock+option naming is a small follow-up. |

## 1. Problem

`core::services::ClassifyStrategies` (`src/core/services/OptionStrategy.h`) works
purely from **net positions** — IB drops the original combo linkage, so every
multi-leg grouping is a heuristic guess (`GroupSource::Inferred`, rendered with a
leading `~`). Six naked puts are indistinguishable from three put spreads. When
the app *itself* submits a combo we know the exact legs, so we can record that and
make those positions group with certainty.

Out of scope this round: manual merge (the missing half of ungroup/regroup) and
rolling (cross-expiry close+open as one BAG). Both remain future work.

## 2. Ground truth (verified in code)

- `ClassifyStrategies(positions, ungroupedConIds)` buckets OPT legs by underlying,
  names by shape (`twoLegGroup` / `tryNamedMulti` / `decompose`), and stamps
  `source = Inferred` for anything multi-leg. `ungroupedConIds` pins legs flat
  (`Manual` singles). `PortfolioWindow` calls it at `PortfolioWindow.cpp:588`.
- Ungroup state persists as `PORT_UNGROUP` (`conId-conId|…`) in the Portfolio
  block of `singleton-settings.cfg`, pruned on save when legs are no longer held.
- Combo submit: `g_OptionsChainWindow->OnOrderSubmit` (`main.cpp:2707`) sends a
  `core::Order` whose `spec.comboLegs` (`ComboLegSpec{conId, ratio, action,
  exchange}`) carries each leg's **conId** — resolved before Send is enabled.
  This is the authoritative linkage source.
- `ComboLegSpec` has no strike/expiry/right, so a recorded link is a **partition**
  (a set of conIds). Labeling stays with the classifier, which reads
  strike/expiry/right off the matched `core::Position`s.

## 3. Design — a link registry that overrides only the *partition*

A link says only "these conIds belong together." The existing namers still produce
the label, so there is one naming code path.

### 3a. Model (`OptionStrategy.h`, pure)
```cpp
struct ComboLink {
    std::vector<long> conIds;          // the leg set (option conIds; may include the stock conId)
    GroupSource       source = GroupSource::Actual;  // Actual = recorded at submit
};
```

### 3b. Classify precedence
Extend the signature (new arg defaulted, so existing callers/tests are untouched):
```cpp
std::vector<StrategyGroup>
ClassifyStrategies(const std::vector<core::Position>& positions,
                   const std::unordered_set<long>& ungroupedConIds = {},
                   const std::vector<ComboLink>& links = {});
```
Before the heuristic bucketing pass:
1. For each link, resolve its conIds to present, non-flat, non-ungrouped position
   indices. If **all** legs resolve, emit one group from those legs, named by the
   existing shape logic, `source = link.source` (no `~`), and remove those indices
   from the pool.
   - Option-only leg set → reuse `twoLegGroup` / `tryNamedMulti` / `finalize` for
     the label (Vertical / Calendar / Iron Condor / …).
   - Link that includes the stock conId → group all legs, label generically
     (`"<SYM> Covered Call"` when it is long stock + short call, else `"<SYM>
     Combo (N legs)"`); a small dedicated namer, decision #3.
2. Remaining legs → today's heuristic (`Inferred`), unchanged.

**Verification is the safety net:** a link groups only when its legs are actually
held. A rejected/never-filled combo matches nothing; a partially-closed combo
matches with a smaller `comboQty` and still groups; a fully-closed combo matches
nothing and is pruned.

### 3c. Dedupe / netting
- Two links with the same conId set (e.g. the user opened the same spread twice →
  IB nets into one bigger position) collapse to one match; `finalize`'s
  `comboQty = gcd(|leg qty|)` reports the combined size. Drop duplicate links on
  record (identical sorted conId set).
- A link whose legs partly overlap another link's legs: match the more-specific /
  earliest link first; a leg already claimed by a matched link is removed from the
  pool so it can't be double-counted.

## 4. Recording at submit (`main.cpp`)

In the combo branch of `OnOrderSubmit` (`o.spec.comboLegs.size() >= 2`), collect
the leg conIds and record the link. Ownership: **PortfolioWindow owns all grouping
state** (it already owns `PORT_UNGROUP`), so main.cpp calls a new hook:
```cpp
if (g_PortfolioWindow && o.spec.comboLegs.size() >= 2) {
    std::vector<long> ids;
    for (const auto& cl : o.spec.comboLegs) if (cl.conId) ids.push_back(cl.conId);
    if (ids.size() >= 2) g_PortfolioWindow->RecordComboLink(ids);
}
```
Recorded at submit; verification (3b) makes an unfilled link harmless.

## 5. Persistence (`PortfolioWindow`, singleton-settings.cfg Portfolio block)

- New member `std::vector<std::vector<long>> m_comboLinks;` (Actual source).
- `RecordComboLink(ids)` — sort, dedupe against existing, append.
- Serialize as `PORT_LINK:c1-c2|c3-c4-c5|…` alongside `PORT_UNGROUP`.
- Apply parses it back on load.
- Prune on save: drop any link whose conIds are no longer all present in
  `m_positions` (same pattern as `PORT_UNGROUP` pruning) so the file self-cleans as
  positions close/expire.
- Feed into classify: build the `std::vector<ComboLink>` from `m_comboLinks`
  (source `Actual`) and pass as the third arg at `PortfolioWindow.cpp:588`.

## 6. Edge cases

- **External combos** (placed in TWS) have no link → stay heuristic (`~`). Exactly
  the intended behavior: in-app combos become certain, external stay inferred.
- **Ungroup wins over a link**: if the user ungroups a set that a link also
  claims, the ungrouped conIds are excluded first, so the link fails the
  "all legs present & not-ungrouped" test and falls back — the user's explicit
  action takes precedence.
- **Leg conId reuse across two combos** (same strike/expiry opened as two separate
  spreads) nets to one position pair in IB; both links point at the same conIds →
  dedupe to one group with summed qty (3c).
- **Stock leg** conId is the underlying's; a covered-call link groups stock+option.
  The generic namer covers labeling (decision #3).

## 7. Tasks

1. **Pure**: `ComboLink` + `ClassifyStrategies` link-precedence + generic
   stock-combo namer. `[strategy]` tests: link wins over heuristic; partial-qty
   match still groups; missing-leg → fallback to heuristic; duplicate conId sets
   dedupe; link vs ungroup precedence; covered-call link labels generically.
2. **PortfolioWindow**: `m_comboLinks`, `RecordComboLink`, pass links into
   `ClassifyStrategies`, `PORT_LINK` serialize/apply + prune.
3. **main.cpp**: record link in the combo submit path (`OnOrderSubmit`).
4. **Docs**: `architecture.md` (Portfolio grouping section), `task-history.md`,
   this plan marked landed.

## 8. Deferred (not this round)

- **Manual merge** — user-selected "group these legs" (`Manual` `ComboLink`,
  `PORT_MERGE`), completing the ungroup/regroup pair.
- **Rolling** — close a vertical + open another as one N-leg cross-expiry BAG.
- **Dedicated stock+option strategy names** beyond covered call (protective put /
  collar labeling).
