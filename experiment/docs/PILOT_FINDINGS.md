# Does a Code Index Make AI Agents Better at Fixing Bugs?
## Pilot Study Findings from the SourceMinder Experiment

*Exploratory — N=5 instances, results are hypothesis-generating, not confirmatory.*

---

### The Question

When an AI agent is tasked with fixing a bug in an unfamiliar codebase, how does it find the relevant code? The default answer is the same one a human developer would reach for: `grep`. Search for a string, read the file, open related files, repeat. It works, but it's expensive — each grep result gets pasted into the context window, and the model has to reason about whether each hit is relevant before deciding where to look next.

SourceMinder builds a structured code index: a symbol table that knows what functions exist, where they're defined, what calls what, and what namespace they live in. The central hypothesis is that giving an agent access to this index — via a tool called `qi` — lets it navigate to the right code faster, spending fewer tokens on irrelevant file contents and fewer turns wandering the wrong part of the codebase.

We ran a small pilot experiment to test this. The results are surprising in ways we didn't expect.

---

### Setup

We used [SWE-bench Verified](https://www.swebench.com/), a benchmark of real GitHub issues paired with gold-standard patches. Each instance presents the model with a repository at a specific commit and asks it to produce a patch that makes the failing tests pass.

**Experimental design:**
- **5 instances** drawn from the SWE-bench Verified pool, spanning 2–5 gold-patch files
- **3 models**: Anthropic Claude Haiku 4.5, DeepSeek V4 Flash, DeepSeek V4 Pro
- **5 repetitions** per (instance × model × arm) — 150 total runs
- **Control arm**: standard mini-swe-agent with grep and file tools
- **Treatment arm**: same setup, plus the `qi` tool backed by a per-instance SourceMinder index

The primary outcome is patch correctness: does the model's submitted patch pass the instance's test suite? Secondary outcomes are token efficiency metrics.

All p-values below are exploratory. At N=5 instances, a paired Wilcoxon test has a minimum achievable p-value of 0.0625 — formal significance is impossible regardless of the data. The headline number is the raw effect size with its clustered bootstrap confidence interval.

---

### Finding 1: Flash improves dramatically. Pro gets worse.

The most striking result is not that qi helps — it's that **the effect is model-dependent and not uniformly positive**.

| Model | Control | Treatment | Δ (95% CI) |
|---|---|---|---|
| Claude Haiku 4.5 | 60% | 64% | +4pp [0, +12] |
| DeepSeek V4 Flash | 52% | **72%** | **+20pp [+4, +40]** |
| DeepSeek V4 Pro | **76%** | 52% | **−24pp [−44, −4]** |

Flash's +20pp gain is the most credible signal in the pilot: the entire confidence interval is above zero, and the effect is large. Pro's −24pp loss is equally credible in the wrong direction: the CI is entirely below zero.

Haiku is a wash. The +4pp point estimate is directionally positive but the CI spans zero, and it would take a much larger sample to distinguish this from noise.

**Why does Pro get worse?** We don't know yet. Several hypotheses:

- *Over-indexing*: Pro is capable enough to navigate the codebase well with grep. Adding qi gives it one more tool to think about, and Pro over-commits to structured search even when a simpler grep would have sufficed.
- *Instance mismatch*: The pilot's 5 instances are all relatively small (2–5 files). Pro may already be near-optimal on small instances and qi only adds overhead.
- *Fidelity*: Pro used the most qi calls of any model (385 vs 259 grep), but that high usage may indicate it's adding qi queries *on top of* grep rather than *substituting* for it.

The N=18 confirmatory experiment now running will shed more light on Flash. Pro needs its own N=18 run before we draw conclusions.

---

### Finding 2: Flash's gains come from *persistence*, not *efficiency*

A natural hypothesis going in was that qi would make agents *faster* — fewer turns, fewer tokens, same or better results. Flash's numbers complicate that story.

On the two hardest instances in the pilot (django-11532 and pytest-8399, each touching 5 files), Flash treatment used dramatically *more* tokens than Flash control:

| Instance | n_files | Total input tokens Δ |
|---|---|---|
| django-11532 | 5 | +68.5% |
| pytest-8399 | 5 | +99.0% |
| sphinx-10673 | 3 | −43.4% |

More tokens, not fewer — yet Flash treatment *resolved* these instances more often.

The explanation comes from the LimitsExceeded rate: Flash control hit the turn budget on **24% of runs** (6/25). Flash treatment: **8%** (2/25). The control agent was giving up. The treatment agent, armed with qi, kept going — spending more tokens but eventually finding the fix.

On the medium-complexity instance (sphinx-10673, 3 files), Flash saves 43% of total input tokens in treatment. The pattern is: **on instances where the control agent gets lost, qi buys persistence; on instances where the control agent is competent, qi buys efficiency**.

Haiku tells a cleaner efficiency story. Its LimitsExceeded rate was near zero in both arms, and treatment reduced total input tokens on 3 of 5 instances. The largest saving was on django-11532 (5 files, −19.7% total input), exactly the opposite direction from Flash on the same instance. Different models, same tool, opposite token responses.

---

### Finding 3: The size-interaction hypothesis is model-specific

The pre-registered hypothesis was: *qi helps more on larger instances* (more files = more navigation overhead = more qi benefit). We measured this as the Spearman correlation between instance size (n_files) and per-instance token savings.

| Model | Metric | Size rho |
|---|---|---|
| Haiku | total_input_tokens | −0.78 |
| Flash | total_input_tokens | +0.63 |
| Pro | total_input_tokens | +0.34 |

A negative rho means larger instances save more tokens (consistent with the hypothesis). Haiku supports the hypothesis. Flash contradicts it. Pro is weakly in the wrong direction.

More tellingly, when we ask whether the *same* instances benefit across models, the answer is largely no. The cross-model Spearman correlation of per-instance token effects is near zero or negative (rho ≈ −0.50 to +0.20). Django-11532 (n_files=5) saves 19.7% of tokens for Haiku and *costs* 68.5% for Flash. The effect is model × instance specific, not a clean function of instance size.

This matters for the N=18 design. We selected instances with a deliberate distribution (8 at n_files=2, 10 at n_files>2) to test the size hypothesis. The pilot suggests the hypothesis may hold for some models but not others, which means the N=18 result will need to be interpreted per-model.

---

### Finding 4: Treatment fidelity varies by model

We defined *treatment fidelity* as the degree to which models actually *substituted* qi for grep in the treatment arm, rather than adding qi on top of existing grep usage.

| Model | Control (grep/run) | Treatment: qi/run | Treatment: grep/run |
|---|---|---|---|
| Haiku | 19.2 | 7.4 | 15.2 |
| Flash | 21.8 | 11.2 | 13.1 |
| Pro | 19.8 | 15.4 | 10.4 |

Flash shows the most substitution: grep usage nearly halved (21.8 → 13.1) while qi came in at 11.2. Haiku still greps heavily in treatment (15.2 vs 19.2 baseline) — it's running "qi+grep" rather than "qi instead of grep". Pro actually cut grep the most in absolute terms (19.8 → 10.4) while using the most qi (15.4), which is consistent with it *over-indexing* on qi.

High qi usage doesn't predict good outcomes (Pro: high qi, worst outcome). Low qi+high grep dilutes any qi-specific signal (Haiku). Flash's balanced substitution and strong outcomes are the most coherent pattern.

---

### What this doesn't tell us

**N=5 is very small.** Every number above is exploratory. The confidence intervals are wide. The Wilcoxon tests are underpowered by design. The Pro result is striking but a single model × 5-instance dataset is not enough to conclude that qi *reliably* hurts Pro.

**This is a pilot on small, tractable instances.** The 5 pilot instances touch 2–5 gold-patch files. Real-world bugs in large codebases may require navigating 10+ files, which is where the qi navigation benefit should be strongest. The N=18 confirmatory experiment was designed with a heavier weighting toward larger instances (n_files=3–6).

**Correct submissions ≠ good patches in general.** SWE-bench scores patches by running the test suite. A patch that passes the tests is counted as resolved regardless of code quality. We're measuring task success on a specific benchmark, not code quality in the wild.

**The Pro result is unexplained.** We have hypotheses but no mechanism. Understanding why qi hurts Pro is a first-class research question. It could be a dilution artifact, a capability mismatch, or something specific to these 5 instances.

---

### What comes next

The N=18 Flash confirmatory experiment (18 instances × 3 reps × 2 arms = 108 trajectories) is evaluating now. This is the first adequately powered test — at N=18, a paired Wilcoxon test can achieve p<0.05 if the effect is consistent, and the +20pp pilot result gives us reason to expect it might be.

If the N=18 Flash result holds, the next question is whether it generalizes:

- **Pro N=18**: Does qi reliably hurt Pro, or was the pilot result noise or instance-specific?
- **Haiku N=18**: Is the +4pp pilot estimate real, or does qi have no effect on smaller models?
- **Larger instances**: Does the size interaction (qi helps more on harder instances) appear at N=18 with a wider instance distribution?

The token efficiency picture will also sharpen. The pilot's wide confidence intervals on token effects (spanning both large savings and large costs) reflect genuine heterogeneity across instances — N=18 will estimate that heterogeneity more precisely.

---

### Tentative bottom line

At N=5, the evidence for qi is mixed but not discouraging. Flash shows a large, credible success rate improvement. The mechanism — reduced runaway sessions, better grep substitution, persistence on hard instances — is coherent with what qi is designed to do. Haiku is neutral, which is consistent with qi adding overhead on instances small enough that grep already works.

The Pro result is the puzzle. A tool that makes your best model worse is not a product you can ship. Understanding whether that result replicates, and why, is the most important open question from this pilot.

---

*Data: experiment/results/runs/pilot/ · Code: experiment/analysis/ · 2026-06-19*
