"""Stage20 offline sprite sharing with separate GBA hardware palettes.

Never changes the production assets by default. Only monsters using the same
native CL2 family AND identical frame counts/alpha masks may share an image.
Fits normal and selected RGB555 palettes offline; no runtime pixel remap is
introduced. The source manifest is an immutable SPR1 archive, separate from the
compacted runtime pool. Original colours are compared against the Stage19 port.
"""
from pathlib import Path
import argparse,base64,hashlib,json,struct,tempfile
import numpy as np
from pack_hot_monsters import Decoder,raw_tiles
from lz77 import compress,decompress
ROOT=Path(__file__).resolve().parents[1]
HOT=('stone_clan','horned_demon','blue_fallen_spear','blood_knight')
STATES='nwahs'

def sha(x):return hashlib.sha256(x).hexdigest()
def pixels(raw):
 a=np.frombuffer(raw,np.uint8);b=np.empty((len(a),2),np.uint8);b[:,0]=a&15;b[:,1]=a>>4
 return b.reshape(-1)
def pack_pixels(p):return (p[0::2]|(p[1::2]<<4)).tobytes()
def rgb(pal):return np.stack([(pal>>s)&31 for s in (0,5,10)],axis=-1).astype(np.int64)
def rgb_words(colors):return np.array(colors[:,0]|(colors[:,1]<<5)|(colors[:,2]<<10),dtype='<u2')
def frame_count(blob):
 a,b=struct.unpack_from('<II',blob);return b if a in (0x31525053,0x32525053) else a

def load_sources(args):
 if args.source_manifest:
  p=args.source_manifest;report=json.loads(p.read_text());base=p.parent
  pool=(base/report['pool_file']).read_bytes();assert sha(pool)==report['pool_sha256']
  clips={}
  for row in report['clips']:
   blob=base64.b64decode(row['source_clip_base64'],validate=True)
   assert sha(blob)==row['source_sha256'] and len(blob)==row['source_bytes']
   clips[row['asset']]=blob
  pals=args.palettes or base/'palettes'
 else:
  pool=args.pool.read_bytes();clips={p.stem:p.read_bytes() for p in args.source.glob('*.lz')};pals=args.palettes or ROOT/'generated'
 palettes={p.stem:p.read_bytes() for p in pals.glob('*palette.lz')}
 return pool,clips,palettes

def fit_palette(contingency,target):
 colors=rgb(target);fit=colors.copy()
 for index in range(1,16):
  weights=contingency[index];count=weights.sum()
  if count:fit[index]=np.floor((weights@colors)/count+0.5).astype(np.int64)
 fit[0]=0
 return rgb_words(fit)
def errors(contingency,palette,original):
 error=rgb(palette)[:,None,:]-rgb(original)[None,:,:]
 weights=contingency.copy();weights[0,:]=0
 count=int(weights.sum());squared=(error*error).sum(axis=2)
 return {'opaque_pixels':count,'rgb8_channel_rmse':float(np.sqrt((squared*weights).sum()/max(1,3*count))*255/31),
         'changed_pixels':int(weights[np.any(error!=0,axis=2)].sum()),
         'max_rgb8_channel_error':float(np.max(np.abs(error)[weights>0],initial=0)*255/31)}

def matching_frames(key,canonical,names,indexed):
 shared={};own={};contingency=np.zeros((16,16),np.int64)
 for state,name in names[key].items():
  target=indexed[name];cn=names[canonical].get(state);reference=indexed.get(cn)
  if reference is not None and len(reference)==len(target) and np.array_equal(reference>0,target>0):
   shared[name]=cn;contingency+=np.bincount(reference.astype(np.int64)*16+target,minlength=256).reshape(16,16)
  else:own[name]=target
 return shared,own,contingency

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--source-manifest',type=Path)
 ap.add_argument('--source',type=Path,default=ROOT/'generated')
 ap.add_argument('--pool',type=Path,default=ROOT/'generated/hero_frame_pool.lz')
 ap.add_argument('--palettes',type=Path)
 ap.add_argument('--output',type=Path,default=ROOT/'build/stage20/palette/staged')
 ap.add_argument('--catalog',type=Path,default=ROOT/'source_assets/monster_catalog/monsters.json')
 ap.add_argument('--max-rmse',type=float,default=24.0,help='maximum per-channel normal RGB8 RMSE, over opaque pixels')
 ap.add_argument('--keep-original',default='',help='comma-separated kinds whose original image and palette must be retained')
 ap.add_argument('--check',action='store_true',help='verify report outputs only, without writing')
 args=ap.parse_args()
 if args.check:
  report=json.loads((args.output/'palette-sharing-report.json').read_text())
  for name,record in report['outputs'].items():
   blob=(args.output/(name+'.lz')).read_bytes();assert len(blob)==record['bytes'] and sha(blob)==record['sha256'],name
  print('PASS',len(report['outputs']),'palette sharing output hashes');return
 pool,clips,palette_blobs=load_sources(args)
 records=json.loads(args.catalog.read_text())['monsters'];by_key={r['key']:r for r in records}
 keep_original={key.strip() for key in args.keep_original.split(',') if key.strip()}
 assert keep_original<=by_key.keys(),'unknown --keep-original kind: '+str(sorted(keep_original-by_key.keys()))
 frames={};indexed={};names={};pals={};groups={};source_hashes={}
 with tempfile.TemporaryDirectory(prefix='palette-sharing-') as temp:
  dec=Decoder(temp,pool)
  for r in records:
   key=r['key'];names[key]={}
   for state in STATES:
    name=key+'_'+state
    if name not in clips and state=='h':name=key+'_hit'
    if name not in clips:continue
    blob=clips[name];f=dec.frames(blob,frame_count(blob));frames[name]=f;indexed[name]=pixels(b''.join(f));names[key][state]=name
    source_hashes[name]={'bytes':len(blob),'sha256':sha(blob),'decoded_sha256':sha(b''.join(f))}
   pn='obj_palette' if key=='zombie' else key+'_palette'
   raw=np.frombuffer(decompress(palette_blobs[pn]),'<u2')
   pals[key]=np.concatenate((raw[16:32],raw[64:80])) if key=='zombie' else raw[:32].copy()
   # Same geometry for all ordinary animations is mandatory, including hit.
   sig=tuple((s,len(indexed[names[key][s]]),sha((indexed[names[key][s]]>0).tobytes())) for s in 'nwah' if s in names[key])
   groups.setdefault((r['graphic'],sig),[]).append(key)
  rows=[];outputs={};expected_palettes={};group_reports=[];excluded=[]
  for (graphic,_),members in groups.items():
   if len(members)<2:
    excluded.extend(members);continue
   canonical=next((x for x in HOT if x in members),None)
   candidate_scores=[]
   if canonical is None:
    # Existing hot representatives are immutable. For other families choose
    # the existing image whose palette can best represent every member.
    for candidate in members:
     rmses=[]
     for key in members:
      _,_,counts=matching_frames(key,candidate,names,indexed)
      fitted=fit_palette(counts,pals[key][:16])
      rmses.append(errors(counts,fitted,pals[key][:16])['rgb8_channel_rmse'])
     candidate_scores.append({'key':candidate,'maximum_rmse':max(rmses),'sum_rmse':sum(rmses)})
    canonical=min(candidate_scores,key=lambda r:(r['maximum_rmse'],r['sum_rmse'],r['key']))['key']
   group_rows=[]
   for key in members:
    # Match every available animation to the representative; extra animations
    # retain their own geometry and are offline remapped into the fitted palette.
    shared,own,contingency=matching_frames(key,canonical,names,indexed)
    palette=np.concatenate((fit_palette(contingency,pals[key][:16]),fit_palette(contingency,pals[key][16:])))
    assert len(palette)==32 and palette[0]==palette[16]==0
    assert np.all(palette<32768),key+': RGB555 palette exceeds hardware range'
    assert np.all(rgb(palette[16:])[1:]>=rgb(palette[:16])[1:]),key+': selected palette darkened'
    normal=errors(contingency,palette[:16],pals[key][:16]);bright=errors(contingency,palette[16:],pals[key][16:])
    accepted=normal['rgb8_channel_rmse']<=args.max_rmse and key not in keep_original
    reason='user_quality_keep' if key in keep_original else 'accepted' if accepted else 'color_error_limit'
    row={'key':key,'canonical':canonical,'same_native_graphic':graphic,'shared_clips':shared,'own_clips':list(own),
         'normal':normal,'selected':bright,'accepted':accepted,'reason':reason,'frames':sum(len(frames[n]) for n in names[key].values()),
         'geometry_alpha_identical':True,'normal_palette_rgb555':palette[:16].tolist(),'selected_palette_rgb555':palette[16:].tolist(),
         'original_palette_rgb555':pals[key].tolist()}
    if accepted:
     for name,cn in shared.items():outputs[name]=raw_tiles(frames[cn])
     # Normally only native special animations lack a matching representative.
     # Their alpha, dimensions and frame order remain byte-for-byte unchanged.
     own_errors=[]
     for name,target in own.items():
      dist=((rgb(pals[key][:16])[:,None,:]-rgb(palette[:16])[None,:,:])**2).sum(axis=2)
      lut=np.array([0]+[int(np.argmin(dist[i,1:]))+1 for i in range(1,16)],np.uint8)
      converted=lut[target];assert np.array_equal(converted>0,target>0)
      raw=pack_pixels(converted);outputs[name]=raw_tiles([raw[i:i+2048] for i in range(0,len(raw),2048)])
      counts=np.bincount(converted.astype(np.int64)*16+target,minlength=256).reshape(16,16)
      own_errors.append({'asset':name,'normal':errors(counts,palette[:16],pals[key][:16]),'selected':errors(counts,palette[16:],pals[key][16:])})
     row['own_clip_errors']=own_errors
     if key=='zombie':
      full=np.frombuffer(decompress(palette_blobs['obj_palette']),'<u2').copy();full[16:32]=palette[:16];full[64:80]=palette[16:]
      expected_palettes['obj_palette']=full.tobytes()
     else:expected_palettes[key+'_palette']=palette.tobytes()
    rows.append(row);group_rows.append(row)
   group_reports.append({'graphic':graphic,'canonical':canonical,'members':members,'accepted_members':[r['key'] for r in group_rows if r['accepted']],'candidate_scores':candidate_scores})
  args.output.mkdir(parents=True,exist_ok=True)
  for name,raw in expected_palettes.items():
   from legacy_monster_palette_style import style_palette
   raw=style_palette(name.removesuffix('_palette'),raw)
   outputs[name]=compress(raw)
   assert decompress(outputs[name])==raw,name+': palette compression changed RGB555 values'
  # Verify every accepted output with the actual shipping decoder, including
  # frame counts, all transparent pixels and the expected representative image.
  checked=0
  for row in rows:
   if not row['accepted']:continue
   for name in names[row['key']].values():
    actual=dec.frames(outputs[name],len(frames[name]));p=pixels(b''.join(actual))
    assert np.array_equal(p>0,indexed[name]>0),name+': alpha changed'
    if name in row['shared_clips']:assert actual==frames[row['shared_clips'][name]],name+': shared picture mismatch'
    checked+=len(actual)
  previous_path=args.output/'palette-sharing-report.json'
  if previous_path.exists():
   previous=json.loads(previous_path.read_text())
   for name,record in previous['outputs'].items():
    if name not in outputs:
     stale=args.output/(name+'.lz')
     if stale.exists():
      assert sha(stale.read_bytes())==record['sha256'],name+': edited stale output'
      stale.unlink()
  for name,blob in outputs.items():(args.output/(name+'.lz')).write_bytes(blob)
  report={'format_version':1,'operation':'offline RGB555 best-fit hardware palette sharing; SPR2 shared images',
          'source_pool_sha256':sha(pool),'source_assets':source_hashes,'max_normal_rgb8_channel_rmse':args.max_rmse,
          'keep_original':sorted(keep_original),
          'extra_runtime_pixel_passes':0,'extra_runtime_palette_banks':0,'checked_frames':checked,
          'selected_palette_never_darker':True,
          'palette_error_metrics_stage':'before user display styling; see monster_palette_style.py',
          'normal_selected_palette_lookups_verified':True,
          'source_palette_sha256':{n:sha(b) for n,b in palette_blobs.items()},
          'groups':group_reports,'monsters':rows,'excluded_singleton_alpha_groups':excluded,
          'outputs':{n:{'bytes':len(b),'sha256':sha(b)} for n,b in outputs.items()},
          'output_logical_bytes':sum(len(b) for b in outputs.values()),
          'output_unique_aligned_bytes':sum((len(b)+3)&~3 for b in set(outputs.values()))}
  (args.output/'palette-sharing-report.json').write_text(json.dumps(report,indent=2)+'\n')
  print('PASS',len(outputs),'assets',checked,'frames;',report['output_unique_aligned_bytes'],'unique bytes')
  for row in rows:print(row['key'],'->',row['canonical'],'RMSE',round(row['normal']['rgb8_channel_rmse'],2),'accepted',row['accepted'])
  print('Geometry singleton exclusions:',','.join(excluded))
if __name__=='__main__':main()
