import json, glob, re
hits=[]; miss=[]; toc=[]
for tf in sorted(glob.glob("logs/deepseek--deepseek-v4-flash/n25_pro_v4_flash/treatment/*/*.traj.json")):
    d=json.load(open(tf)); msgs=d.get('messages',[])
    out_by_id={m.get('tool_call_id'):str(m.get('content','')) for m in msgs if m.get('role')=='tool'}
    for m in msgs:
        ex=m.get('extra')
        if not isinstance(ex,dict): continue
        for a in ex.get('actions') or []:
            cmd=(a.get('command') or '').strip()
            if not re.search(r'\bqi\b',cmd): continue
            out=out_by_id.get(a.get('tool_call_id'),'')
            is_search = not re.search(r'-e\b|--expand|--toc|--usage|-u\b',cmd)
            if 'No results' in out and len(miss)<2: miss.append((cmd,out))
            elif is_search and 'No results' not in out and 'Found' not in out and len(hits)<3: hits.append((cmd,out))
            elif re.search(r'--toc|-e\b|--expand',cmd) and len(toc)<2: toc.append((cmd,out))
    if len(hits)>=3 and len(miss)>=2 and len(toc)>=2: break
for tag,coll in [("SEARCH-HIT(-q,no 'Found')",hits),("MISS",miss),("EXPAND/TOC",toc)]:
    for cmd,out in coll:
        print(f"\n===== {tag}: {cmd[:75]} =====")
        print(out[:400])
