# Monitoring — Watching Runs in Progress

A short guide on how to watch a run in progress and interpret what you see.

## Starting a Monitored Run

```bash
python3 experiment/run_experiment.py \
    --instances-file experiment/verified_instance_ids.txt \
    --reps 10 --quiet
```

`--quiet` mode does two things:
1. Prints the log path for each run before starting it
2. Redirects all subprocess output into that log file in real-time

Example output:
```
[control] django__django-10554 run=1
  trajectory: experiment/logs/control/django__django-10554/1.traj.json
  log:        experiment/logs/control/django__django-10554/1.log
  follow:     tail -f experiment/logs/control/django__django-10554/1.log
```

## Following a Run

Copy the `tail -f` line and run it:
```bash
tail -f experiment/logs/control/django__django-10554/1.log
```

## Interpreting the Output

### Normal operation

The log shows mini-swe-agent's rich console output — one turn per step:

```
Step 1
  Model: deepseek/deepseek-v4-flash
  Cost: $0.0234
  Response: I'll start by exploring the repository structure...
  Command: ls /testbed

Step 2
  Model: deepseek/deepseek-v4-flash
  Cost: $0.0312
  Response: Let me look at the relevant files...
  Command: qi '*' -f django/contrib/contenttypes/models.py --toc
```

Each turn takes 30–120 seconds (API latency + model thinking time). Cost
increment typically $0.02–$0.05 per turn for DeepSeek V4 Flash.

### What "thinking" looks like

When the API is slow, you'll see the step header appear then a long pause
before the Response line. This is normal — the model is processing the
full context (200K–400K tokens) and generating a response.

### Signs of a stalled run

A run has likely stalled if the log file shows **no new output for >2 minutes**
and no error message. Possible causes:

- Network stall (`litellm` waiting on a hanged connection)
- Docker OOM (container killed silently)
- mini-swe-agent stuck in a retry loop

Check process liveness:
```bash
ps aux --forest | grep run_one
ps aux | grep mini-extra
```

Check container liveness:
```bash
docker ps --filter name=mini
```

### Normal vs. abnormal patterns

| Indicator | Normal | Abnormal |
|-----------|--------|----------|
| Log growth | Steady every 30–120s | No growth >2 min |
| Turn count | Incrementing step numbers | Same step for >3 min |
| Cost increments | $0.02–$0.05 per turn | No cost change for >2 min |
| API errors | Rare, retried automatically | Repeated same error |
| qi commands | Appear in treatment runs | Missing in treatment (agent ignoring qi) |
| grep commands | Common in control | Dominance may indicate fallback from qi |

## Monitoring a Batch

### Quick status check

```bash
# Count completed trajectories
find experiment/logs -name '*.traj.json' | wc -l

# Count manifest states
find experiment/logs -name '*.manifest.json' \
  -exec sh -c 'jq -r ".status" "$1"' _ {} \; | sort | uniq -c

# Check ledger for the latest attempts
tail -5 experiment/logs/run_ledger.jsonl | jq '{arm, instance_id, rep, ok}'
```

### Per-arm progress

```bash
# Trajectories by arm
find experiment/logs/control -name '*.traj.json' | wc -l
find experiment/logs/treatment -name '*.traj.json' | wc -l
```

### Check running processes

```bash
# See the process tree
ps aux --forest | grep -E 'run_experiment|mini-extra|run_one'

# Docker containers
docker ps --filter name=mini --format 'table {{.Names}}\t{{.RunningFor}}\t{{.Status}}'
```

## Diagnosing a Specific Run

```bash
# Check if the manifest says it completed
cat experiment/logs/control/django__django-10554/1.manifest.json | jq .

# Check exit_status (Submitted, LimitsExceeded, ValueError, etc.)
jq '.info.exit_status' experiment/logs/control/django__django-10554/1.traj.json

# Count turns completed
jq '[.messages[] | select(.role=="assistant")] | length' \
  experiment/logs/control/django__django-10554/1.traj.json

# Check if a patch was submitted
jq '.info.submission' experiment/logs/control/django__django-10554/1.traj.json
```

## When to Worry

- **Log hasn't grown in >2 minutes** — likely stalled; check process liveness
- **Container running for >30 minutes** — should be done by now (100 turns max,
  ~30–60s/turn = 50–100 min max). Likely stalled.
- **exit_status is `ValueError`** — the interactive turn-limit prompt crashed.
  These runs need `--retry-failed` to re-run.
- **No `.traj.json` file exists but manifest says `completed`/`failed`** — the
  subprocess exited without writing a trajectory. A framework error.
- **Manifest says `started` with no later status** — the orchestrator was
  killed mid-run; this run will be re-run automatically on resume.
