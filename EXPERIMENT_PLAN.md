`qi` Context Preservation Experiment Plan

Purpose
- Produce publication-grade empirical evidence about whether `qi` preserves LLM context window relative to standard shell-based code exploration.
- Measure token consumption directly, not by proxy.
- Ensure the result is reproducible, statistically defensible, and difficult to dispute on methodological grounds.

Important Scope Note
- No single experiment can deliver literal, philosophical "irrefutable proof."
- This plan is designed to produce the strongest practical empirical evidence: pre-registered, randomized, replicated, instrumented, blinded, and fully reproducible.

Core Claim
- Using `qi` for code exploration reduces context-window consumption during coding tasks, relative to standard shell exploration, without materially reducing task success.

Primary Research Question
- Does directing an LLM coding agent to use `qi` for exploration reduce prompt-token consumption and preserve more usable context-window headroom than a control workflow?

Secondary Research Questions
- Does `qi` reduce the amount of tool output exposed to the model?
- Does `qi` reduce the number of broad file reads and grep-style false-positive exploration steps?
- Does `qi` preserve or improve task success, time-to-completion, and patch quality?

Primary Hypothesis
- Compared with control, the `qi` treatment reduces:
  - total input tokens across the task
  - peak prompt tokens in any single turn
  - tool-output tokens shown to the model

Non-Inferiority Hypothesis
- Compared with control, the `qi` treatment does not reduce:
  - task success rate
  - test pass rate
  - blinded review acceptance rate

Main Experimental Principle
- Compare two conditions on the same feature task from the same clean repository state:
  - Control: no instruction to use `qi`
  - Treatment: explicit instruction to prefer `qi` for code exploration

- Repeat the same task many times in fresh sessions.
- Optionally add a strict control with `qi` unavailable to separate "natural baseline" from "baseline that may opportunistically use `qi`."

Study Design
- Type: randomized controlled experiment with repeated independent runs
- Unit of analysis: one complete agent session from prompt to task termination
- Study arms:
  - Arm A: naturalistic control
  - Arm B: `qi`-preferred treatment
  - Optional Arm C: strict no-`qi` control
- Replication:
  - one primary task for the main study
  - at least two additional feature tasks for external validation

Why Repeated Runs Are Necessary
- LLM behavior is stochastic, even with stable prompts and environment.
- Single-run A/B comparisons are not scientifically persuasive.
- Multiple fresh runs are required to estimate central tendency, variance, and robustness.

Pre-Registration Requirements
- Before data collection begins, write and freeze:
  - repository commit hash
  - model name and version
  - system/developer prompt stack
  - exact user task prompt for each arm
  - test command(s)
  - timeout limits
  - max turn budget
  - run inclusion/exclusion rules
  - primary and secondary metrics
  - analysis plan
  - stopping rule
  - contamination handling rules

- Store this preregistration in version control.
- Do not change prompts, task definitions, or metrics after seeing outcome data.

Task Selection Criteria
- Choose tasks that:
  - require real code exploration
  - have objectively testable acceptance criteria
  - touch multiple files
  - cannot be solved from one obvious file alone
  - are small enough to finish within one session
  - are representative of normal coding work

- Avoid tasks that:
  - are pure refactors
  - are trivial from one `rg` (ripgrep reveals solution)
  - are so large that implementation randomness dominates exploration behavior
  - depend heavily on external internet lookup

Recommended Task Profile
- Medium-complexity feature addition in an existing codebase
- Example classes of task:
  - add a new CLI flag
  - add a new query filter
  - add a new output column
  - add a new parser/indexer capability with tests

Task Replication Set
- Primary task: one medium-complexity feature on SourceMinder
- Secondary tasks:
  - another feature in the same repo, different subsystem
  - one feature in a different repo with different structure

- Publication-grade evidence is much stronger if the effect replicates across tasks and repos.

Experimental Arms

Arm A: Naturalistic Control
- Prompt contains only the feature request and acceptance criteria.
- No mention of `qi`.
- The agent may use whatever tools it chooses.

Arm B: `qi` Treatment
- Same task prompt as control.
- Add this instruction:

```text
For code exploration, prefer `qi` over broad text search.
Use file-scoped exploration first:
- `qi '*' -f file --toc`
- `qi symbol -f file`
- `qi symbol -f file -e`
Only open files or line ranges after narrowing with `qi`.
```

Optional Arm C: Strict No-`qi` Control
- Same task prompt as Arm A.
- Remove `qi` from PATH or otherwise make it unavailable.
- This isolates the effect of the tool itself from opportunistic baseline use.

Recommendation on Arms
- Minimum defensible study: Arm A vs Arm B
- Stronger study: Arm A vs Arm B plus Arm C

Randomization
- Randomly assign each run to an arm.
- Use blocked randomization to keep arm counts balanced over time.
- If using multiple tasks, randomize within task.

Environment Controls
- Same repository commit for all runs within a task
- Same index database state for all runs
- Same model and model version
- Same tokenizer for measurement
- Same system/developer prompts
- Same sandbox/permissions
- Same temperature or sampling parameters, if configurable
- Same time budget
- Same turn budget
- Same machine and runtime environment if possible

Reset Procedure Before Each Run
1. Reset repository to the exact starting commit.
2. Restore `code-index.db` to the exact same baseline snapshot.
3. Start a fresh agent session with no prior conversation state.
4. Verify the same test suite and commands are available.
5. Begin logging before the first model turn.

Termination Conditions
- A run ends when one of the following occurs:
  - the agent declares completion
  - the predefined tests pass
  - the turn budget is exhausted
  - the time budget is exhausted
  - the agent becomes blocked and explicitly stops

Instrumentation Requirements
- Capture per-turn model usage:
  - prompt tokens
  - completion tokens
  - total tokens
  - timestamps

- Capture per-tool-call data:
  - tool name
  - command/query
  - raw output shown to the model
  - tokenized length of the output
  - exit status
  - timestamps

- Capture run-level outcomes:
  - success/failure
  - tests passed/failed
  - elapsed wall-clock time
  - number of turns
  - final patch diff size

Token Measurement
- Prefer the API/provider's official usage accounting for model turns.
- For tool outputs, tokenize the exact text injected into the model using the same tokenizer family as the target model.
- Do not estimate using character counts alone.

Required Logging Schema
- `run_id`
- `task_id`
- `arm`
- `repo_commit`
- `model_name`
- `model_version`
- `start_time`
- `end_time`
- `turn_index`
- `prompt_tokens`
- `completion_tokens`
- `tool_name`
- `tool_input`
- `tool_output_tokens`
- `tool_exit_code`
- `tests_passed`
- `task_success`
- `review_accept`
- `notes`

Primary Outcome Metrics
1. Total input tokens per run
- Sum of all prompt tokens across the run

2. Peak prompt tokens per run
- Maximum prompt tokens in any single turn
- This is the most direct proxy for context-window pressure

3. Total tool-output tokens shown to the model
- Sum of tokenized tool outputs injected into prompts

Why Peak Prompt Tokens Matter
- If model context window is `C`, then preserved headroom is:
  - `C - peak_prompt_tokens`
- This directly quantifies how much future reasoning capacity remains available.

Secondary Outcome Metrics
- total completion tokens
- total turns
- total wall-clock time
- number of broad file reads
- number of file-scoped symbol queries
- number of lines read from full-file dumps
- number of commands using `qi`
- number of commands using `grep`/`rg`/`cat`/`sed`
- number of retries after dead ends
- patch size in lines changed

Success and Quality Metrics
- automated tests pass
- acceptance criteria satisfied
- blinded human review accepts the patch
- no critical regression introduced

Blinded Review Protocol
- Reviewers must not know which arm produced the patch.
- Review packet should contain:
  - task specification
  - resulting diff
  - test results
  - optional short rationale, scrubbed of arm-specific cues

- Reviewer scores:
  - accepts / rejects
  - correctness
  - completeness
  - unnecessary changes
  - confidence score

Contamination Rules
- Naturalistic control may still use `qi`.
- Decide handling before the experiment begins.

Recommended handling:
- Primary analysis:
  - Intention-to-treat by assigned arm
- Secondary analysis:
  - Per-protocol by actual tool usage

- If using Arm C, any successful use of `qi` in Arm C is a protocol violation and the run should be flagged and rerun.

Mechanism Metrics
- To support causal interpretation, also report:
  - average number of full-file reads
  - average tokens from file dumps
  - average tokens from grep/rg outputs
  - average tokens from `qi --toc`
  - average tokens from `qi symbol`
  - average tokens from `qi -e`

- This shows not just that tokens were saved, but how they were saved.

Sample Size Strategy
- Do not guess sample size without variance estimates.
- Use a two-phase plan:

Phase 1: Pilot
- Run 10 sessions per arm on the primary task.
- Estimate:
  - variance of total input tokens
  - variance of peak prompt tokens
  - success rate
  - contamination rate in naturalistic control

Phase 2: Confirmatory Study
- Use pilot variance to compute the required sample size for 80% to 90% power.
- Primary effect target:
  - detect at least a 20% reduction in peak prompt tokens

Practical Default if No Formal Power Model Is Prepared
- Minimum:
  - 20 runs per arm on the primary task
- Better:
  - 30 runs per arm on the primary task
- Publication-grade:
  - 30 runs per arm per task across 3 tasks

Predefined Success Thresholds
- The treatment is considered successful if all are true:
  - median total input tokens are reduced by at least 20%
  - median peak prompt tokens are reduced by at least 20%
  - 95% confidence interval for the difference excludes zero in the beneficial direction
  - success rate is non-inferior within a predefined margin
  - blinded review acceptance is non-inferior within a predefined margin

Suggested Non-Inferiority Margins
- success rate drop no worse than 5 percentage points
- review acceptance drop no worse than 5 percentage points

Statistical Analysis Plan

Primary analyses
- Compare arm distributions for:
  - total input tokens
  - peak prompt tokens
  - total tool-output tokens

Recommended methods
- bootstrap confidence intervals for median differences
- Mann-Whitney U for unpaired arm comparison
- Wilcoxon signed-rank if the design is paired by task instance

Non-inferiority analyses
- two-proportion confidence intervals for:
  - success rate
  - review acceptance rate

Mixed-effects extension for publication
- If running across multiple tasks/repos, fit a mixed-effects model:
  - outcome ~ arm + (1 | task) + (1 | repo)
- This improves generalization and separates arm effects from task effects.

Secondary descriptive analyses
- median and IQR for all token metrics
- survival-style completion curves over time or turns
- distribution of tool types used per arm

Visualization Plan
- Boxplots or violin plots for:
  - total input tokens
  - peak prompt tokens
  - tool-output tokens

- Bar charts for:
  - success rate
  - blinded acceptance rate

- Sankey or stacked bars for exploration behavior:
  - `qi`
  - grep/rg
  - file dumps
  - tests

- Headroom chart:
  - `context_window - peak_prompt_tokens`

Example Result Framing
- "Relative to naturalistic control, the `qi` treatment reduced median peak prompt tokens by 31%, preserving an additional 24k tokens of context headroom, while maintaining non-inferior task success and patch acceptance."

Handling Failures
- Failed runs must remain in the dataset unless excluded by preregistered protocol rules.
- Do not silently drop difficult runs.
- Report:
  - total failures
  - causes of failure
  - whether failure patterns differ by arm

Threats to Validity and Mitigations

1. Model nondeterminism
- Mitigation:
  - many repeated fresh runs
  - fixed model version
  - blocked randomization

2. Control arm opportunistically uses `qi`
- Mitigation:
  - log actual `qi` usage
  - perform both intention-to-treat and per-protocol analyses
  - optional strict no-`qi` arm

3. Task-specific overfitting
- Mitigation:
  - replicate on multiple tasks
  - include a second repo if possible

4. Success differences inflate token usage
- Mitigation:
  - report success-adjusted analyses
  - compare successful runs separately as a secondary analysis

5. Tool-output token accounting mismatch
- Mitigation:
  - tokenize exact prompt-injected tool text using the correct tokenizer
  - validate accounting on sample runs

6. Human review bias
- Mitigation:
  - blinded review
  - predefined rubric

7. Learning effects over time
- Mitigation:
  - randomized run ordering
  - no carryover session memory

Artifact Package for Reproducibility
- Publish:
  - preregistration document
  - prompts for each arm
  - repository commit hashes
  - task specs
  - run logs
  - token accounting scripts
  - statistical analysis scripts
  - anonymized review data
  - final report with raw tables

Ideal Deliverables
- `PREREGISTRATION.md`
- `TASK_SPECS/`
- `RUN_LOGS/`
- `ANALYSIS_NOTEBOOK/` or scripts
- `RESULTS.md`
- `FIGURES/`

Recommended Experimental Script Outline
1. Reset repo and DB state
2. Launch fresh agent session
3. Inject arm-specific prompt
4. Capture every turn and tool result
5. Stop at completion or budget
6. Run tests
7. Save patch
8. Send diff to blinded reviewer
9. Aggregate metrics
10. Run preregistered analysis

Minimum Publishable Version
- 20 to 30 runs per arm
- one primary task
- token accounting at turn level
- blinded review
- confidence intervals

Publication-Grade Version
- pilot plus powered confirmatory study
- 30 runs per arm per task
- at least 3 tasks
- at least 2 repos if possible
- intention-to-treat and per-protocol analyses
- blinded review
- full artifact release
- replication of the primary effect on multiple tasks

Interpretation Standard
- If `qi` reduces total and peak prompt tokens while preserving success and quality across replicated tasks, that is strong evidence that `qi` preserves context-window headroom in real coding workflows.
- If the effect appears only on one task or depends on protocol quirks, the claim should be narrowed accordingly.

Concrete Recommendation
- Start with a pilot on one SourceMinder feature task.
- Use the pilot to finalize power and contamination handling.
- Then run the confirmatory study with at least 30 runs per arm.
- Replicate on two more tasks before making broad claims.

Suggested Summary Statement for the Final Paper/Report
- "`qi` significantly reduced prompt-token consumption and peak context pressure during LLM-assisted coding tasks, while maintaining non-inferior task success and patch quality, indicating that structured symbol-oriented exploration preserves usable context-window capacity relative to standard shell exploration."
