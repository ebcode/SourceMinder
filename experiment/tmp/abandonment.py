import csv, re
from collections import defaultdict, Counter

# Narrowing filters the agent can stack to over-exclude. -x noise is the
# recommended default, so a retry that only keeps -x noise still counts as
# relaxed if it dropped a -f/-i/--within/-w that the missed query carried.
_FILTER_RE = re.compile(r"(?:^|\s)(-f|-i|-w|--within|--filter|--include|--exclude)(?:\s|=|$)")

def _nfilters(cmd):
    return len(_FILTER_RE.findall(cmd))

def analyze(path, label):
    rows=[r for r in csv.DictReader(open(path)) if r['arm']=='treatment']
    by_run=defaultdict(list)
    for r in rows:
        # run_id is just the rep ("1"/"2"/"3"); the unique run is (instance, rep)
        by_run[(r['instance'], r['run_id'])].append(r)
    # outcome class for a qi row
    def cls(r):
        if r['tool']!='qi' or r['qi_results'] in ('',None): return None
        n=int(r['qi_results'])
        if n>0: return 'hit'
        return f"miss:{r['qi_miss_kind'] or 'other'}"
    # stats
    nextsearch=defaultdict(Counter)   # outcome -> Counter(next qi/grep/none)
    terminal=defaultdict(lambda:[0,0]) # outcome -> [no_more_qi, total]
    last_qi_classes=Counter()
    overall=Counter()
    retry_relax=defaultdict(Counter)  # outcome -> Counter(relaxed/same_or_more) for next=qi
    for run,cmds in by_run.items():
        cmds.sort(key=lambda r:(int(r['turn_idx']), int(r['cmd_idx'])))
        qi_positions=[i for i,r in enumerate(cmds) if r['tool']=='qi' and cls(r)]
        for k,i in enumerate(qi_positions):
            c=cls(cmds[i])
            if not c: continue
            overall[c]+=1
            # next exploration command of type qi or grep (skip read/other)
            nxt='none'; nxt_row=None
            for j in range(i+1,len(cmds)):
                if cmds[j]['tool'] in ('qi','grep'):
                    nxt=cmds[j]['tool']; nxt_row=cmds[j]; break
            nextsearch[c][nxt]+=1
            # for a qi-retry, did it drop a filter vs the missed query?
            if nxt=='qi' and c.startswith('miss'):
                relaxed = _nfilters(nxt_row['command']) < _nfilters(cmds[i]['command'])
                retry_relax[c]['relaxed' if relaxed else 'same_or_more']+=1
            # terminal: any qi after this one in the run?
            more_qi = any(cmds[j]['tool']=='qi' and cls(cmds[j]) for j in range(i+1,len(cmds)))
            terminal[c][1]+=1
            if not more_qi: terminal[c][0]+=1
        # the very last qi-with-outcome in the run
        if qi_positions:
            lc=cls(cmds[qi_positions[-1]])
            if lc: last_qi_classes[lc]+=1

    print(f"\n################## {label} ##################")
    print(f"{'qi outcome':16}{'n':>6}{'next=grep':>12}{'next=qi':>10}{'next=none':>11}{'P(no more qi)':>15}")
    order=['hit','miss:filtered','miss:absent','miss:other']
    for c in order:
        n=overall[c]
        if not n: continue
        ns=nextsearch[c]; g=ns['grep']; q=ns['qi']; no=ns['none']
        t0,t1=terminal[c]
        print(f"{c:16}{n:>6}{g:>5} ({g/n:>3.0%}){q:>4} ({q/n:>3.0%}){no:>4} ({no/n:>3.0%}){t0/t1:>13.0%}")
    # of the qi-retries after a miss, how many relaxed (dropped a filter)?
    print("  -- after a MISS, when next call is qi (a retry): did it drop a filter? --")
    for c in ['miss:filtered','miss:absent']:
        rr=retry_relax[c]; tot=rr['relaxed']+rr['same_or_more']
        if not tot: continue
        print(f"     {c:16} qi-retries={tot:>3}  relaxed={rr['relaxed']:>3} ({rr['relaxed']/tot:>3.0%})"
              f"  same/more={rr['same_or_more']:>3} ({rr['same_or_more']/tot:>3.0%})")
    # enrichment: miss-cause among LAST-qi (abandonment point) vs base rate
    print("  -- miss-cause mix: at the LAST qi call vs base rate among all misses --")
    base_miss=sum(v for k,v in overall.items() if k.startswith('miss'))
    last_miss=sum(v for k,v in last_qi_classes.items() if k.startswith('miss'))
    for mc in ['miss:filtered','miss:absent']:
        br = overall[mc]/base_miss if base_miss else 0
        lr = last_qi_classes[mc]/last_miss if last_miss else 0
        print(f"     {mc:16} base={br:.0%}   at-last-qi={lr:.0%}")
    print(f"     (last-qi calls that were a MISS: {last_miss}/{sum(last_qi_classes.values())} = "
          f"{last_miss/sum(last_qi_classes.values()):.0%}  vs base miss rate "
          f"{base_miss/sum(overall.values()):.0%})")

analyze('results/runs/n25_pro_v4_flash/qi_commands.csv','FLASH')
analyze('results/runs/n25_v4_pro/qi_commands.csv','PRO')
