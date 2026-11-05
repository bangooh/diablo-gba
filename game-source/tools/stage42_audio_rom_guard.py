#!/usr/bin/env python3
"""Validate the linked effects table and actual ROM guard bytes for DMA2 tails."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rom=a.base.with_suffix('.gba').read_bytes()
symbols=subprocess.check_output(['readelf','-sW',str(a.base.with_suffix('.elf'))],text=True)
rows=[r.split() for r in symbols.splitlines()]
entry=next(r for r in rows if len(r)>=8 and r[-1]=='effects' and r[3]=='OBJECT')
address=int(entry[1],16);size=int(entry[2]);assert size%8==0
at=address-0x08000000;assert 0<=at<len(rom)
checks=[]
for i in range(size//8):
    pointer,length=struct.unpack_from('<II',rom,at+i*8)
    offset=pointer-0x08000000
    guard=rom[offset+length:offset+length+1024]
    item=dict(id=i,address=f'0x{pointer:08x}',length=length,aligned4=pointer%4==0,guardBytes=len(guard),guardAllZero=len(guard)==1024 and not any(guard))
    assert 0<=offset<len(rom) and length>0,item
    checks.append(item)
report=dict(romSha256=hashlib.sha256(rom).hexdigest(),effectsTable=f'0x{address:08x}',effects=len(checks),guardBytesRequired=1024,checks=checks,result='PASS' if all(c['aligned4'] and c['guardAllZero'] for c in checks) else 'FAIL')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='checks'}));assert report['result']=='PASS'
