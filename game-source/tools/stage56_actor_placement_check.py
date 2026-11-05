"""Verify exact sparse-decoder body and isolated native placement measurements."""
from pathlib import Path
import argparse, hashlib, json, subprocess
from stage54_barrel_summarize import phase

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline',type=Path,help='Frozen baseline directory with src, ELF and ROM')
parser.add_argument('candidate',type=Path,help='Private actor-placement candidate directory')
parser.add_argument('baseline_report',type=Path)
parser.add_argument('candidate_report',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
def body(path):
    source=path.read_text();start=source.index('{',source.index('unsigned xor_nibbles('))
    depth=1;end=start+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
before=body(args.baseline/'src/actor_frames.c')
assert body(root/'src/actor_frames.c')==before
assert body(args.candidate/'src/actor_frames.c')==before
def location(folder):
    lines=subprocess.check_output(['readelf','-sW',str(folder/'diablo-gba-assets.elf')],text=True)
    fields=next(line.split() for line in lines.splitlines() if line.split() and line.split()[-1]=='xor_nibbles')
    return dict(address=int(fields[1],16),bytes=int(fields[2]))
old_location=location(args.baseline);new_location=location(args.candidate)
assert 0x08000000<=old_location['address']<0x0a000000
assert 0x03000000<=new_location['address']<0x03006000
old=json.loads((args.baseline_report/'report.json').read_text())
new=json.loads((args.candidate_report/'report.json').read_text())
assert old['result']==new['result']=='PASS' and old['mapSha256']==new['mapSha256']
cases=[]
for sample in new['samples']:
    prior=next(x for x in old['samples'] if x['prop']['i']==sample['prop']['i'])
    assert prior['prop']==sample['prop'] and prior['after']==sample['after']
    assert prior['beforeRenderState']==sample['beforeRenderState']
    assert prior['afterRenderState']==sample['afterRenderState']
    cases.append(dict(prop=sample['prop']['i'],kind=sample['prop']['kind'],
                      before=phase(args.baseline_report,prior,old['hardwareHz']),
                      after=phase(args.candidate_report,sample,new['hardwareHz']),
                      outcomeAndTerrainDepthEqual=True))
report=dict(result='PASS',baselineRomSha256=old['romSha256'],candidateRomSha256=new['romSha256'],
            exactFunctionBodySha256=hashlib.sha256(before.encode()).hexdigest(),
            baselineLocation=old_location,candidateLocation=new_location,cases=cases,
            scope='Only the unchanged xor_nibbles function moves from ROM to IWRAM. Native actual-A barrel inputs, same map, byte-identical settled color/depth and gameplay outcomes. These two isolated cases are not the final combined-build performance guarantee.')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
