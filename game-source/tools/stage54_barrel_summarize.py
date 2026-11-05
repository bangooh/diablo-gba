#!/usr/bin/env python3
"""Compare measured break-animation presentation windows in native traces."""
from pathlib import Path
import argparse,json,statistics

def phase(folder, sample, hz):
    index=sample['prop']['i']
    trace=json.loads((folder/f'barrel-{index}-trace.json').read_text())
    lo=next(x['f'] for x in trace if dict(x['states'])[index]>=2)
    hi=next(x['f'] for x in trace if x['f']>lo and dict(x['states'])[index]==1)
    commits=[b for a,b in zip(trace,trace[1:]) if b['renders']!=a['renders']]
    start=max((x for x in commits if x['f']<lo),key=lambda x:x['f'])
    end=next(x for x in commits if x['f']>=hi)
    frames=end['f']-start['f'];count=end['renders']-start['renders']
    inside=[start]+[x for x in commits if start['f']<x['f']<=end['f']]
    gaps=[b['f']-a['f'] for a,b in zip(inside,inside[1:])]
    return dict(hardwareFrames=frames,renderCommits=count,fps=count*hz/frames,
                gapMedian=statistics.median(gaps),gapMaximum=max(gaps),gaps=gaps,
                worstGapMs=max(gaps)*1000/hz,minimumPerFrameCadenceFps=hz/max(gaps),
                sourceStateStartsAt=lo,sourceStateFinishesAt=hi)

def main():
    p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    old=json.loads((a.before/'report.json').read_text());new=json.loads((a.after/'report.json').read_text())
    assert old['result']==new['result']=='PASS'
    assert old['mapSha256']==new['mapSha256'] and old['props']==new['props']
    assert len(old['samples'])==len(new['samples'])
    report={'result':'PASS','beforeRom':old['romSha256'],'afterRom':new['romSha256'],'sameMapAndNativeProps':True,
            'phase':'Last completed draw before native break state2 through first completed draw at settled state1. Includes impact redraw and final debris redraw.',
            'scope':'Emulated hardware time, peaceful native seed12345 L1 fixtures; not physical GBA or campaign-wide FPS.', 'cases':[]}
    for x,y in zip(old['samples'],new['samples']):
        assert x['prop']==y['prop'] and x['after']==y['after']
        before=phase(a.before,x,old['hardwareHz']);after=phase(a.after,y,new['hardwareHz'])
        for sample,value in [(x,before),(y,after)]:
            value['wholeAttackWindowFps']=sample['action']['fps']
            value['wholeAttackWindowMinimumRollingSecondFps']=sample['action'].get('minimumRollingSecondFps')
            value['wholeAttackWindowSeconds']=sample['action']['hardwareFrames']/old['hardwareHz']
        report['cases'].append(dict(prop=x['prop']['i'],kind=x['prop']['kind'],before=before,after=after,
            fpsImprovementPercent=100*(after['fps']/before['fps']-1),
            beforePropDecodes=x['action'].get('propDecodes'),afterPropDecodes=y['action'].get('propDecodes'),nativeOutcomeUnchanged=True))
        if all(key in sample for sample in [x,y] for key in ['beforeRenderState','afterRenderState']):
            report['cases'][-1]['sameSettledTerrainAndDepth']=all(x[key]==y[key] for key in ['beforeRenderState','afterRenderState'])
    for kind,name in [(3,'ordinary'),(52,'explosive')]:
        rows=[x for x in report['cases'] if x['kind']==kind];values={}
        for version in ['before','after']:
            frames=sum(x[version]['hardwareFrames'] for x in rows);renders=sum(x[version]['renderCommits'] for x in rows)
            values[version]={'aggregateFps':renders*old['hardwareHz']/frames,'renderCommits':renders,'hardwareFrames':frames,
                             'minimumCasePhaseFps':min(x[version]['fps'] for x in rows),
                             'minimumPerFrameCadenceFps':min(x[version]['minimumPerFrameCadenceFps'] for x in rows),
                             'medianGap':statistics.median([g for x in rows for g in x[version]['gaps']]),'maxGap':max(x[version]['gapMaximum'] for x in rows)}
        values['improvementPercent']=100*(values['after']['aggregateFps']/values['before']['aggregateFps']-1);report[name]=values
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['result','ordinary','explosive']},indent=2))

if __name__=='__main__':main()
