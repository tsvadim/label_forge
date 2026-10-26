import sys, zxingcpp
from PIL import Image
d=sys.argv[1]; bad=0
for line in open(d+"/barcodes.tsv",encoding="utf-8"):
    f,hexexp=line.rstrip("\n").split("\t"); exp=bytes.fromhex(hexexp).decode("utf-8")
    res=zxingcpp.read_barcodes(Image.open(d+"/"+f))
    got=[(r.format.name if hasattr(r.format,'name') else str(r.format), r.text) for r in res]
    ok=any(t==exp for _,t in got)
    if not ok: bad+=1
    print("OK " if ok else "BAD", f, got[:1] if ok else (got, repr(exp)))
print("failures:",bad)
