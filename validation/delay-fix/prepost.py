import sys, numpy as np
a=np.fromfile(sys.argv[1],dtype=np.float32); b=np.fromfile(sys.argv[2],dtype=np.float32)
n=len(a)//2; assert len(a)==len(b)
name=sys.argv[3]
out=[]
for ch,nm in ((0,'L'),(1,'R')):
    x=a[ch*n:(ch+1)*n].astype(np.float64); y=b[ch*n:(ch+1)*n].astype(np.float64)
    d=np.abs(x-y); idx=np.nonzero(x.view(np.int64)!=y.view(np.int64))[0] if False else np.nonzero(a[ch*n:(ch+1)*n].view(np.uint32)!=b[ch*n:(ch+1)*n].view(np.uint32))[0]
    if len(idx)==0: out.append(f'{nm}: identical'); continue
    f=idx[0]; w=slice(f,f+256); w2=slice(f,f+4800)
    out.append(f'{nm}: first_diff={f} old={x[f]:.9g} new={y[f]:.9g} |d|={d[f]:.3g}; max|d| in [first,first+256)={d[w].max():.3g}; in [first,first+4800)={d[w2].max():.3g}; overall max|d|={d.max():.3g} (at {int(d.argmax())}); n_diff={len(idx)}/{n}; peak(old)={np.abs(x).max():.4g}; rms|d| first 4800 = {np.sqrt((d[w2]**2).mean()):.3g}; rms|d| whole = {np.sqrt((d**2).mean()):.3g}')
print(name+' | '+' | '.join(out))
