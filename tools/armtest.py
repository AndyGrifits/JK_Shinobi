#!/usr/bin/env python3
"""JK_Shinobi: how far apart the hands get with shorter arms (glmpreview.shorten_arms), over
the frames of every animation where the palms meet (< 7 units).
    python3 tools/armtest.py _humanoid.gla animation.cfg
"""
import sys, numpy as np, collections
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file, glmpreview as gp
gla=g2file.Gla(sys.argv[1]); cfg=gp.read_cfg(sys.argv[2])
def J(m,n):
    i=gla.index[n]; b=gla.bones[i]; j=np.array([b['base'][r][3] for r in range(3)]); return m[i][:,:3]@j+m[i][:,3]
def grip(m):  # palm centre: between hand joint and the d2 knuckle
    return {s:(J(m,s+'hand')+J(m,s+'_d2_j1'))/2 for s in 'lr'}
rows=[]
for name,(first,count) in cfg.items():
    if not name.startswith('BOTH_') or count<1: continue
    for f in sorted({first, first+count//2, first+count-1}):
        m=[np.array(x) for x in gla.frame_skin(f)]
        g=grip(m); d=np.linalg.norm(g['l']-g['r'])
        if d<7.0:
            out=[d]
            for k in (0.9,0.85):
                mk=gp.shorten_arms(gla,m,k); gk=grip(mk); out.append(np.linalg.norm(gk['l']-gk['r']))
            rows.append((name,f,*out))
print("frames with the hands together (palms < 7 units apart):", len(rows), "in", len({r[0] for r in rows}), "animations")
a=np.array([r[2:] for r in rows]); d0=np.array([r[2] for r in rows])
for k,col in ((0.9,3),(0.85,4)):
    g=np.array([r[col]-r[2] for r in rows])
    print("arms x%.2f: gap grows by median %.1f, 90%% %.1f, max %.1f units" % (k, np.median(g), np.percentile(g,90), g.max()))
