"""List every demo-conditional preprocessor block (D-063). Read-only.
usage: demo_blocks.py <repo>  -> markdown table on stdout, summary on stderr"""
import os, re, sys, collections
root = os.path.join(sys.argv[1], 'code')
SKIP = {'FRED2', 'Fred2', 'AC', 'nebedit', 'pofview', 'Fonttool', 'Cfilearchiver', 'Scramble', 'HelpEd', 'Network', 'Cryptstring'}
DEMO = ['FS2_DEMO', 'DEMO', 'OEM_BUILD', 'E3_BUILD', 'PRESS_TOUR_BUILD', 'PD_BUILD']
DIR = re.compile(r'^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$')
CONST = re.compile(r'^\s*#\s*define\b')

def macros_in(c):
    c = c.split('//')[0].split('/*')[0]
    return [m for m in DEMO if re.search(r'\b%s\b' % m, c)]

rows, per_sub, per_mac = [], collections.Counter(), collections.Counter()
consts = 0
for dp, ds, fs in os.walk(root):
    rel = os.path.relpath(dp, root)
    sub = rel.split(os.sep)[0]
    if sub in SKIP:
        continue
    for f in sorted(fs):
        if not f.lower().endswith(('.cpp', '.h')):
            continue
        p = os.path.join(dp, f)
        lines = open(p, encoding='latin-1').read().split('\n')
        stack = []  # [start, cond, macs, branches[[kw, n_lines, n_decl]]]
        for i, ln in enumerate(lines):
            m = DIR.match(ln)
            if m:
                kw, cond = m.group(1), m.group(2).strip()
                if kw in ('ifdef', 'ifndef', 'if'):
                    stack.append([i + 1, (kw + ' ' + cond).strip(), macros_in(cond), [[kw, 0, 0]]])
                elif kw in ('elif', 'else') and stack:
                    stack[-1][3].append([kw, 0, 0])
                    stack[-1][2] += [x for x in macros_in(cond) if x not in stack[-1][2]]
                elif kw == 'endif' and stack:
                    s, cond, macs, br = stack.pop()
                    if macs:
                        body = sum(b[1] for b in br)
                        decl = sum(b[2] for b in br)
                        rows.append((os.path.relpath(p, root).replace('\\', '/'), s, i + 1, cond, len(br) > 1, body, decl, macs))
                        per_sub[sub] += 1
                        for x in macs: per_mac[x] += 1
                        if decl: consts += 1
                # a directive line also counts toward the enclosing branch
                for e in stack[:-1] if kw in ('ifdef', 'ifndef', 'if') else stack:
                    pass
                continue
            for e in stack:
                e[3][-1][1] += 1
                if CONST.match(ln): e[3][-1][2] += 1

print('| File | Lines | Condition | Has else | Body lines | #defines |')
print('|---|---|---|---|---|---|')
for p, s, e, c, el, body, decl, macs in rows:
    print('| `%s` | %d-%d | `#%s` | %s | %d | %d |' % (p, s, e, c.replace('|', '\\|'), 'yes' if el else '', body, decl))
sys.stderr.write('blocks %d, with else %d, with #defines %d\n' % (len(rows), sum(1 for r in rows if r[4]), consts))
sys.stderr.write('by macro: %s\n' % ', '.join('%s %d' % kv for kv in per_mac.most_common()))
sys.stderr.write('by subsystem: %s\n' % ', '.join('%s %d' % kv for kv in per_sub.most_common()))
sys.stderr.write('files %d\n' % len(set(r[0] for r in rows)))
