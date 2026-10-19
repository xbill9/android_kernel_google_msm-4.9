# usage: resolve.py FILE MODE   MODE: ours|theirs|union (union = ours then theirs), applied to every conflict hunk
import re,sys
f,mode=sys.argv[1],sys.argv[2]
s=open(f).read()
pat=re.compile(r'^<<<<<<< [^\n]*\n(.*?)^(?:\|\|\|\|\|\|\| [^\n]*\n.*?)?^=======\n(.*?)^>>>>>>> [^\n]*\n',re.S|re.M)
n=0
def r(m):
    global n; n+=1
    o,t=m.group(1),m.group(2)
    return {'ours':o,'theirs':t,'union':o+t}[mode]
s=pat.sub(r,s); open(f,'w').write(s); print(f,mode,n,'hunks')
