# Summary Addendum — Statistical Power / "How big must N be?"

**Addendum to:** `work_summary_20260702_171225.md`
**Topic:** After the main summary was written, we worked through the recurring question — *what does N have to be to move the qi results from "directional" to "proof-positive"?* This addendum captures that analysis (computed from the current 7-instance data, not rules of thumb).

---

## Key insight: "N" is two different things

- **Reps per arm** (within one instance) → proves "qi helped *on this instance*."
- **Number of instances** (cross-instance paired test) → proves the *general* claim. This is the better route: it's a **paired design** (each instance is its own control/treatment pair), which cancels the large between-instance variance.

## Route A — reps per arm (per instance), for 80% power (α=0.05)

From observed effect sizes (Cohen's d), `n ≈ 15.7 / d²`:

| metric | median d | reps/arm needed |
|---|---|---|
| cost | 0.86 | ~21 |
| log size | 0.80 | ~25 |
| total tokens | 0.63 | ~40 |
| turns | 0.53 | ~57 |
| patch lines | 0.38 | ~108 |

At n=5 we are 4–8× short on the strong metrics. Expensive route.

## Route B — number of instances (cross-instance paired), for 80% power

| metric | geomean | instances needed |
|---|---|---|
| cost | 0.75 | ~12 |
| log size | 0.90 | ~18 |
| total tokens | 0.82 | ~19 |
| patch lines | 0.84 | ~22 |
| turns | 0.99 | ~351 (genuinely ~flat — don't hang the claim on it) |

So **~15–20 instances** makes the general claim solid on cost/log/tokens. We have 7.

## The clean threshold: the sign test (assumption-free)

With *k* instances all moving the same direction, two-sided p = 2 / 2^k:

- k=5 all agree → p = 0.063 (just misses)
- **k=6 → p = 0.031 ✓**, k=7 → 0.016, **k=8 → 0.0078**

**Direct answer to "8 instances, 5 reps each, log size consistently 90% of control — significant?"** → **Yes, if all 8 land below 1.0:** sign test p = 0.0078; the magnitude-aware test (Wilcoxon signed-rank / paired-t on log-ratios) is far stronger (~0.00003 for ratios clustered near 0.90).

### Two traps
1. **The sign test needs unanimity at n=8** (7/8 → p = 0.070, misses). This is exactly why we're *not* significant on the current 7: log-size is **6 down / 1 up** (flipt = 1.12) → sign test **p = 0.125**, paired-t **p ≈ 0.11**. The **"qi good-fit" scoping** (dropping the non-source instances flipt/openlibrary) is what buys a unanimous set — and the magnitude-aware signed-rank test tolerates a *small* reverser where the bare sign test cannot. **Use Wilcoxon signed-rank, not the bare sign test.**
2. **"Significant" is a claim about *these* instances.** It generalizes to "qi tasks in general" only if the instances are a fair sample. Hand-picked good-fit instances → the honest claim is "significant for qi-good-fit tasks."

### The "5 reps" subtlety
The 5 reps/arm *estimate* each instance's 0.90; they don't enter the cross-instance test directly, but they determine whether that ratio reliably lands below 1.0. A genuine ~10% effect with noisy 5-rep estimates can occasionally *measure* >1 by chance and break unanimity; a large effect (qutebrowser 0.63) never will. **Smaller true effect → more reps needed to lock each instance's sign.**

---

## Recommended design (from directional → proof-positive)

1. **Pre-register one primary metric** (suggest **total_tokens** or **log_size** — the context-economy core). Don't test 5 metrics × N instances and report the winner (multiple-comparisons cherry-picking).
2. **~8 good-fit instances at ~8–10 reps/arm** (more reps to make each 10%-ish effect's *direction* robust).
3. **Test with Wilcoxon signed-rank on the per-instance log-ratios.** If they cluster near 0.90 and all point down, expect p ≈ 0.001–0.01 — genuinely proof-positive.

## Caveats to state alongside any p-value

- Reps are provider nondeterminism, so significance = "the qi effect exceeds run-to-run noise" — narrower than "qi helps a random task."
- The variance-collapse finding is about *spread*, not the mean, so per-instance it needs even more reps (estimating a variance ratio), though cross-instance it shows up robustly.
- Turns is ~flat (d_eff ≈ 0.15, ~351 instances) — exclude it from the significance claim.
