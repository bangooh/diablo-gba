#!/usr/bin/env python3
"""Bind Stage63 release source, ROM and completed minimap discovery evidence.

Reads existing evidence; does not rerun emulator/performance tests. Private
instrumented builds cannot pass this release gate. No unrelated old gates.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VALIDATION = ROOT/'generated/stage63-validation'
BASELINE_SHA = 'ab726f2c37097984d454a4ac3868563be84c08420c62b73d3451e2c6405210c3'
EWRAM_LIMIT, IWRAM_LIMIT = 261928, 24368
NATIVE = {'minimap-pixels-native/report.json': ('cases',7),
          'integrated-ui/report.json': ('cases',8),
          'hud-colors-native/report.json': ('checks',3),
          'npc-ui-native/report.json': ('cases',14),
          'firebolt-native/report.json': ('cases',3),
          'melee-native/report.json': ('cases',6)}
METRICS = {'render':('renders',), 'video':('videoSha256',),
           'playfieldVideo':('playfieldVideoSha256',),
           'worldBg0Scroll':('bgX','bgY'), 'cpuCamera':('camX','camY')}
SAMPLES = {'L1-native-fog-walk-off-0', 'L1-native-fog-walk-on-0',
           'L1-all-explored-walk-on-0'}

def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def passed(report, name):
    assert report.get('result', report.get('status')) == 'PASS', f'Not PASS: {name}'


def source_hashes(report, key, name):
    values = report[key]
    assert isinstance(values, dict) and values, name
    for path, digest in values.items():
        assert sha(ROOT / path) == digest, f'Stale tested source: {name}: {path}'


def load(path):
    return json.loads(path.read_text())


def trace_metrics(beginning, trace, fields, hz):
    previous = tuple(beginning[k] for k in fields)
    last, intervals = 0, []
    for row in trace:
        current = tuple(row[k] for k in fields)
        if current != previous:
            intervals.append({'startFrame':last,'endFrame':row['f'],
                              'hardwareFrames':row['f']-last})
            last, previous = row['f'], current
    gaps = sorted(v['hardwareFrames'] for v in intervals)
    assert gaps
    p99 = gaps[math.ceil(.99*len(gaps))-1]
    return {'intervals':intervals,
            'histogramHardwareFrames':{str(k):v for k,v in sorted(collections.Counter(gaps).items())},
            'intervalCount':len(gaps), 'p99HardwareFrames':p99, 'p99Ms':p99*1000/hz,
            'maxHardwareFrames':max(gaps),'maxMs':max(gaps)*1000/hz,
            'trailingUnfinishedHoldHardwareFrames':600-last}


def discovery(folder, rom_sha, hashes):
    prefix = folder.relative_to(VALIDATION).as_posix()
    report, summary = load(folder/'report.json'), load(folder/'summary.json')
    for name, value in [('report.json',report),('summary.json',summary)]:
        passed(value,prefix+'/'+name)
        assert value['romSha256'] == rom_sha
        hashes[prefix+'/'+name] = sha(folder/name)
    assert report['fixtureWorld'] == summary['fixtureWorld']
    assert report['hardwareHz'] == summary['hardwareHz']
    assert {s['name'] for s in report['samples']} == SAMPLES
    assert {s['name'] for s in summary['samples']} == SAMPLES
    summaries = {s['name']:s for s in summary['samples']}
    for sample in report['samples']:
        name, beginning = sample['name'], report['beginningStates'][sample['name']]
        assert beginning == sample['before'] == summaries[name]['beginningState']
        assert beginning['f'] == 0 and beginning['diag'] is None
        assert sample['hardwareFrames'] == 600 and sample['logicalMoves'] == 25
        assert sample['mapOverruns'] == 0
        trace_path = folder/(name+'-trace.json')
        trace = load(trace_path)
        assert [r['f'] for r in trace] == list(range(1,601))
        assert all(r['diag'] is None for r in trace), 'Instrumented trace is not release evidence'
        assert all(trace[-1][k]==v for k,v in sample['after'].items())
        assert sha(trace_path) == summaries[name]['traceSha256']
        hashes[prefix+'/'+trace_path.name] = sha(trace_path)
        assert summaries[name]['fps'] == sample['fps']
        assert summaries[name]['minRolling60HardwareFramesFps'] == sample['minRolling60HardwareFramesFps']
        assert math.isclose(sample['fps'],sample['renders']*report['hardwareHz']/600,rel_tol=1e-12)
        for metric, fields in METRICS.items():
            want = trace_metrics(beginning,trace,fields,report['hardwareHz'])
            got = summaries[name]['metrics'][metric]
            assert got.keys() == want.keys()
            for key, value in want.items():
                if isinstance(value,float): assert math.isclose(got[key],value,rel_tol=1e-12)
                else: assert got[key] == value, (prefix,name,metric,key)
        assert summaries[name]['metrics']['render']['histogramHardwareFrames'] == sample['renderGapHistogram']
        assert summaries[name]['metrics']['render']['intervalCount'] == sample['renders']
        render_counts=[beginning['renders']]+[v['renders'] for v in trace]
        minimum=min((render_counts[i]-render_counts[i-60])*report['hardwareHz']/60 for i in range(60,601))
        assert math.isclose(minimum,sample['minRolling60HardwareFramesFps'],rel_tol=1e-12)
        previous, fog, commits, anchors = beginning, [], [], []
        for row in trace:
            if [row['mapLastX'],row['mapLastY']] != [previous['mapLastX'],previous['mapLastY']]:
                anchors.append({'frame':row['f'],'before':[previous['mapLastX'],previous['mapLastY']],
                                'after':[row['mapLastX'],row['mapLastY']]})
            if row['revision'] != previous['revision']:
                event={'frame':row['f'],'oldRevision':previous['revision'],'newRevision':row['revision'],
                       'newCells':row['revealedCells']-previous['revealedCells'],'hero':[row['x'],row['y']]}
                if fog and fog[-1]['hero']==event['hero'] and row['f']<=fog[-1]['lastFrame']+1:
                    fog[-1]['lastFrame']=row['f'];fog[-1]['newRevision']=row['revision'];fog[-1]['newCells']+=event['newCells']
                else: fog.append({**event,'lastFrame':row['f']})
            if row['mapCommits'] != previous['mapCommits']:
                assert row['mapCommits'] == previous['mapCommits']+1
                commits.append(row['f'])
            previous=row
        assert len(fog)==(7 if sample['region']=='L1-native-fog' else 0)
        assert sum(f['newCells'] for f in fog)==(53 if fog else 0)
        if not sample['mapOn'] or not fog: assert len(commits)==0
        elif rom_sha==BASELINE_SHA: assert len(commits)==7
        else: assert len(commits)<=7  # A reveal may add no new contour pixels.
        events = summaries[name]['discoveryEvents']
        assert len(events)==len(fog)
        for i,(event,actual) in enumerate(zip(events,fog)):
            for key,value in actual.items(): assert event[key]==value
            end=fog[i+1]['frame'] if i+1<len(fog) else 601
            assert event['windowEndFrameExclusive']==end
            window=[f for f in commits if actual['frame']<=f<end]
            assert [v['frame'] for v in event['commitsUntilNextReveal']]==window
            assert event['noCommitObserved'] is (not window)
            if event['commit'] is not None:
                assert event['commit']['frame'] in window
                assert event['commit']['revision']>=actual['newRevision']
            snapshots=[beginning if actual['frame']==1 else trace[actual['frame']-2],
                       trace[actual['frame']-1],trace[end-2]]
            for suffix,row in zip(['BeforeReveal','AtReveal','AfterWindow'],snapshots):
                assert event['anchor'+suffix]==[row['mapLastX'],row['mapLastY']]
                assert event['uniqueTiles'+suffix]==row['mapTiles']
            window_anchors=[v for v in anchors if actual['frame']<=v['frame']<end]
            assert event['anchorChanges']==window_anchors
            assert event['anchorStable'] is (not window_anchors)
            assert event['anchorChanged'] is bool(window_anchors)
        assert summaries[name]['anchorChanges']==anchors
        assert [v['frame'] for v in summaries[name]['mapCommits']]==commits
        assert len(commits)==sample['mapChanges']
    return report, summary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--freeze',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--expected-change',action='append',default=[],
                        help='Exact expected production diff; defaults to automap_raster.inc only')
    parser.add_argument('--native-report',action='append',default=[],metavar='PATH:ARRAY:MIN',
                        help='Additional explicitly selected final-ROM regression report')
    args=parser.parse_args()
    expected_changed=set(args.expected_change or ['src/automap_raster.inc'])
    rom = ROOT / 'build/diablo-gba-assets.gba'
    elf = ROOT / 'build/diablo-gba-assets.elf'
    build_report = ROOT / 'build/diablo-gba-assets-build-report.json'
    meta = json.loads(build_report.read_text())
    rom_sha, baseline_sha = sha(rom), sha(args.baseline / 'diablo-gba-assets.gba')
    assert rom_sha != baseline_sha, 'Candidate must differ from Stage62 baseline'
    assert meta['sha256'] == rom_sha
    assert meta['arm_elf'] == meta['gba_header_checksum'] == 'PASS'
    rom_data = rom.read_bytes()
    assert len(rom_data) == meta['rom_bytes'] == 33554432
    assert (sum(rom_data[160:190]) + 0x19) & 255 == 0

    frozen = json.loads(args.freeze.read_text())
    production = sorted(p for p in (ROOT / 'src').rglob('*') if p.is_file())
    production += [ROOT / 'build.py', ROOT / 'link.ld']
    asset_inputs = [p for p in (ROOT / 'generated').iterdir()
                    if p.suffix in ('.h', '.lz', '.rle', '.bin')]
    required = {str(p.relative_to(ROOT)) for p in production + asset_inputs}
    assert set(frozen) == required, {'missing': sorted(required - set(frozen)),
                                     'unexpected': sorted(set(frozen) - required)}
    for name, digest in frozen.items():
        assert sha(ROOT / name) == digest, f'Input changed after integrated build: {name}'
    changed = [str(p.relative_to(ROOT)) for p in production
               if not (args.baseline / p.relative_to(ROOT)).exists()
               or sha(p) != sha(args.baseline / p.relative_to(ROOT))]
    missing = [str(p.relative_to(args.baseline)) for p in (args.baseline/'src').rglob('*') if p.is_file() and not (ROOT/p.relative_to(args.baseline)).exists()]
    changed += missing
    assert set(changed) == expected_changed, changed

    # Reconcile every loaded ELF segment with final ROM bytes, excluding only
    # the standard header fields replaced by build.py after linking.
    data = elf.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<H', data, 18)[0] == 40
    phoff = struct.unpack_from('<I', data, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 42)
    payload = ram_used = fast_used = 0
    for i in range(phcount):
        kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from(
            '<8I', data, phoff + i*phsize)
        if kind != 1:
            continue
        if 0x02000000 <= vaddr < 0x02040000:
            assert vaddr + memsz <= 0x02040000
            ram_used += memsz
        if 0x03000000 <= vaddr < 0x03008000:
            assert vaddr + memsz <= 0x03006000
            fast_used += memsz
        if filesz and 0x08000000 <= paddr < 0x0a000000:
            dest = paddr - 0x08000000
            payload = max(payload, dest + filesz)
            skip = max(0, min(filesz, 192-dest))
            assert rom_data[dest+skip:dest+filesz] == data[off+skip:off+filesz]
    assert 0 < payload <= 33554432
    assert ram_used == meta['ewram_static_bytes'] <= EWRAM_LIMIT
    assert fast_used == meta['iwram_code_bytes'] <= IWRAM_LIMIT
    assert re.search(r'FAST\s*\(rwx\)\s*:\s*ORIGIN\s*=\s*0x03000000,\s*LENGTH\s*=\s*24K',
                     (ROOT / 'link.ld').read_text())
    startup = (ROOT / 'src/start.s').read_text()
    assert 'ldr sp, =0x03007d00' in startup and 'ldr sp, =0x03007fa0' in startup
    menu, link = (ROOT / 'src/menu.inc').read_text(), (ROOT / 'src/link.h').read_text()
    for text, name, value in ((menu, 'SAVE_VERSION', 127), (menu, 'SAVE_GENERATOR', 17),
                              (link, 'LINK_VERSION', 131)):
        assert int(re.search(r'\b' + name + r'\s*(?:=\s*)?(\d+)', text)[1]) == value


    assert baseline_sha == BASELINE_SHA
    symbols=subprocess.check_output(['readelf','-sW',str(elf)],text=True)
    assert not re.search(r'\bminimap_diag\b',symbols), 'Private profiling ELF'
    native_hashes,native_counts,baseline_hashes,host_hashes={},{},{},{}
    native_reports=dict(NATIVE)
    for spec in args.native_report:
        path,field,minimum=spec.rsplit(':',2)
        assert not Path(path).is_absolute() and '..' not in Path(path).parts
        assert path not in native_reports
        native_reports[path]=(field,int(minimum))
    for name,(field,minimum) in native_reports.items():
        path=VALIDATION/name;report=load(path);passed(report,name)
        assert report.get('romSha256',report.get('rom_sha256'))==rom_sha,name
        assert len(report[field])>=minimum,(name,field,len(report[field]),minimum)
        native_hashes[name]=sha(path);native_counts[name]={field:len(report[field])}
    old,old_summary=discovery(VALIDATION/'discovery-baseline',baseline_sha,baseline_hashes)
    new,new_summary=discovery(VALIDATION/'discovery-native',rom_sha,native_hashes)
    assert old['fixtureWorld']==new['fixtureWorld']
    comparison=new_summary['comparison']
    assert comparison['baselineRomSha256']==baseline_sha and comparison['sameFixtureWorld'] is True
    assert {s['name'] for s in comparison['samples']}==SAMPLES
    before={s['name']:s for s in old_summary['samples']}
    after={s['name']:s for s in new_summary['samples']}
    for row in comparison['samples']:
        a,b=before[row['name']],after[row['name']]
        assert [(e['hero'],e['newCells']) for e in a['discoveryEvents']]==[(e['hero'],e['newCells']) for e in b['discoveryEvents']]
        assert row['allDiscoveryEventsPaired']==len(a['discoveryEvents'])
        assert row['fpsBefore']==a['fps'] and row['fpsAfter']==b['fps']
        assert row['minimumRollingFpsBefore']==a['minRolling60HardwareFramesFps']
        assert row['minimumRollingFpsAfter']==b['minRolling60HardwareFramesFps']
        for key in METRICS:
            values=row['metrics'][key]
            assert values['p99MsBefore']==a['metrics'][key]['p99Ms']
            assert values['p99MsAfter']==b['metrics'][key]['p99Ms']
            assert values['maxMsBefore']==a['metrics'][key]['maxMs']
            assert values['maxMsAfter']==b['metrics'][key]['maxMs']
    path=VALIDATION/'minimap-host/report.json';host=load(path);passed(host,'minimap-host')
    source_hashes(host,'sourceSha256','minimap-host');source_hashes(host,'oracleSha256','minimap-host')
    for key,minimum in [('scenes',545),('pixel_exact_frames',14872),('pixels_compared',571084800),
                        ('junction_diamonds',9216),('negative_capacity_cases',2),('prepare_equivalence_cases',12000)]:
        assert host[key]>=minimum,(key,host.get(key),minimum)
    assert set(host['sanitizers'])=={'ASan','UBSan'}
    host_hashes['minimap-host/report.json']=sha(path)
    for path,digest in host['oracleSha256'].items():
        host_hashes['@root/'+path]=digest
    incremental_path=VALIDATION/'minimap-incremental-host/report.json'
    incremental=load(incremental_path);passed(incremental,'minimap-incremental-host')
    source_hashes(incremental,'sourceSha256','minimap-incremental-host')
    source_hashes(incremental,'harnessSha256','minimap-incremental-host')
    for key,minimum in [('scenes',17),('native_scenes',6),('pixel_exact_frames',1078),
                        ('cached_pixels_compared',140247040),('partial_updates',425),
                        ('no_pixel_updates',610),('full_fallbacks',19),('branch_cells',1120),
                        ('repeat_checks',36),('recenters',2),('menu_restores',1),
                        ('dictionary_capacity_rejections',1),('threshold_rejections',4)]:
        assert incremental[key]>=minimum,(key,incremental.get(key),minimum)
    assert incremental['hardware_flip_mask']==15
    assert incremental['explicit_decode_cases']==960
    assert incremental['pending_cancellations']==3
    assert len(incremental['mode_frames'])==3
    assert all(n>=minimum for n,minimum in zip(incremental['mode_frames'],[1045,16,17]))
    assert set(incremental['sanitizers'])=={'ASan','UBSan'}
    for key in ['mapConfigAndHelpers','automapCommit','automapRaster']:
        assert re.fullmatch('[0-9a-f]{64}',incremental['testedSnippetSha256'][key])
    host_hashes['minimap-incremental-host/report.json']=sha(incremental_path)
    for path,digest in incremental['harnessSha256'].items():
        host_hashes['@root/'+path]=digest
    combat_comparisons={}
    for family in ['firebolt','melee']:
        baseline_path=ROOT/('generated/stage62-validation/'+family+'-native/report.json')
        candidate_path=VALIDATION/(family+'-native/report.json')
        baseline_report,candidate_report=load(baseline_path),load(candidate_path)
        passed(baseline_report,str(baseline_path))
        assert baseline_report['romSha256']==baseline_sha
        baseline_hashes['@root/'+str(baseline_path.relative_to(ROOT))]=sha(baseline_path)
        fixture_fields=['fixture'] if family=='firebolt' else ['mapSha256','arena','wall']
        for field in fixture_fields: assert baseline_report[field]==candidate_report[field],(family,field)
        before={c['name']:c for c in baseline_report['cases']}
        assert set(before)=={c['name'] for c in candidate_report['cases']}
        compared=[];fps_key='fps' if family=='firebolt' else 'rendererFps'
        for case in candidate_report['cases']:
            prior=before[case['name']]
            assert case['minimapOn'] is True and case['mapOverruns']==0
            assert case['hardwareFrames']==prior['hardwareFrames']
            assert case['simulationTicks']==prior['simulationTicks']
            if family=='firebolt':
                assert case['casts']==prior['casts'] and case['failures']==prior['failures']
                assert case['terrainCacheUnchanged'] is True and case['lightHardwareNeverDisabled'] is True
            compared.append({'name':case['name'],'fpsBefore':prior[fps_key],'fpsAfter':case[fps_key],
                             'deltaFps':case[fps_key]-prior[fps_key],
                             'rollingMinimumBefore':prior['minimumRolling60HardwareFramesFps'],
                             'rollingMinimumAfter':case['minimumRolling60HardwareFramesFps']})
        combat_comparisons[family]=compared
    result={'result':'PASS' ,'rom_sha256':rom_sha,'elf_sha256':sha(elf),
            'build_report_sha256':sha(build_report),'baseline_rom_sha256':baseline_sha,
            'changed_production_sources':sorted(changed),'build_inputs_sha256':frozen,
            'native_validation_sha256':native_hashes,'native_validation_counts':native_counts,
            'baseline_validation_sha256':baseline_hashes,'host_validation_sha256':host_hashes,
            'rom_payload_bytes':payload,'rom_free_bytes':33554432-payload,
            'ewram_static_bytes':ram_used,'ewram_static_free_bytes':262144-ram_used,
            'iwram_code_bytes':fast_used,'iwram_code_headroom_bytes':24576-fast_used,
            'iwram_stack_reserved_bytes':8192,'main_stack_top':'0x03007d00','irq_stack_top':'0x03007fa0',
            'save_version':127,'save_generator':17,'link_version':131,
            'stage62_save_compatible':True,'physical_console_tested':False,
            'discovery_performance_comparison':comparison,'combat_performance_comparison':combat_comparisons,
            'minimum24FpsGoalMetInDiscoverySamples':min(v['minRolling60HardwareFramesFps'] for v in new['samples'])>=24}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in {'build_inputs_sha256','discovery_performance_comparison'}},indent=2))


if __name__=='__main__':
    main()
