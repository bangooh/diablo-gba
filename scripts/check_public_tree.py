"""Reject accidental data/binary/credential additions; not a legal clearance tool."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--tracked',action='store_true');a=p.parse_args()
if a.tracked:
 names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0');files=[ROOT/n for n in names if n]
else:files=[f for f in ROOT.rglob('*') if f.is_file() and '.git' not in f.relative_to(ROOT).parts and '__pycache__' not in f.parts]
allowed={'.c','.h','.inc','.cpp','.py','.s','.zig','.applescript','.plist','.md','.txt','.json','.yml','.yaml','.ld'}
allowfile=ROOT/'docs/reference-code-allowlist.json'
reference_allowlist=json.loads(allowfile.read_text()) if allowfile.exists() else {}
exclude_file=ROOT/'docs/excluded-reference-data.json'
excluded_hashes={row['sha256'] for row in json.loads(exclude_file.read_text())} if exclude_file.exists() else set()
errors=[]
project_file=ROOT/'docs/project-asset-allowlist.json'
project_assets=json.loads(project_file.read_text()) if project_file.exists() else {}
selected={str(f.relative_to(ROOT)) for f in files}
for required in project_assets:
 if required not in selected:errors.append(required+': required project input missing from checked files')
for f in files:
 rel=f.relative_to(ROOT);data=f.read_bytes();digest=hashlib.sha256(data).hexdigest()
 approved=project_assets.get(str(rel),{})
 approved_binary=approved.get('sha256')==digest and approved.get('bytes')==len(data)
 if approved and not approved_binary:errors.append(str(rel)+': approved project input hash/size mismatch')
 if digest in excluded_hashes:errors.append(str(rel)+': excluded reference data, including renamed copies')
 if 'source_assets' in rel.parts and f.suffix.lower() in {'.h','.cpp','.c','.inc'} and reference_allowlist.get(str(rel))!=digest:errors.append(str(rel)+': reference code needs provenance review')
 if f.is_symlink():errors.append(str(rel)+': symlink')
 if f.suffix.lower() not in allowed and f.name not in {'.gitignore','.gitattributes'} and not approved_binary:errors.append(str(rel)+': unapproved file type')
 if any(x in rel.parts for x in ['generated','build','dist','ui_sources','fixtures']):errors.append(str(rel)+': private/generated path')
 if b'\0' in data and not approved_binary:errors.append(str(rel)+': binary content')
 if re.search(rb'ghp_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,}|sk-[A-Za-z0-9]{30,}|-----BEGIN (?:RSA |OPENSSH )?PRIVATE KEY-----',data):errors.append(str(rel)+': possible credential')
 if re.search(rb'"(?:source_clip_base64|pixels)"\s*:\s*"[A-Za-z0-9+/=]{200,}',data):errors.append(str(rel)+': encoded asset')
print(json.dumps({'files_checked':len(files),'errors':errors},indent=2));sys.exit(bool(errors))
