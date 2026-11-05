"""Build Windows x64 EXE and Intel/Apple Silicon macOS app for ROM 0.9.5.
Requires Zig 0.13.0, Python cryptography, a verified ROM and both supported MPQs.
MPQ digest/key exist only in memory and are never written to distribution files.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True);p.add_argument('--rom',type=Path,required=True)
p.add_argument('--mpq',type=Path,required=True,help='Diablo 1.0 DIABDAT.MPQ (517,501,242 bytes)')
p.add_argument('--gog-mpq',type=Path,help='GOG DIABDAT.MPQ (517,501,282 bytes)')
p.add_argument('--reuse-key-config',type=Path,help='Reuse authenticated GOG wrapper with the unchanged key domain')
p.add_argument('--reference-package',type=Path,help='Prior encrypted package to authenticate wrapper provenance')
p.add_argument('--output',type=Path,default=ROOT/'dist')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
build=ROOT/'build';build.mkdir(exist_ok=True)
rom=a.rom.read_bytes();assert len(rom)==33554432
assert a.mpq.stat().st_size==517501242
if a.gog_mpq: assert a.gog_mpq.stat().st_size==517501282
else: assert a.reuse_key_config and a.reference_package
with a.mpq.open('rb') as f:digest=hashlib.file_digest(f,'sha256').digest()
gog_digest=None
if a.gog_mpq:
 with a.gog_mpq.open('rb') as f:gog_digest=hashlib.file_digest(f,'sha256').digest()
romhash=hashlib.sha256(rom).hexdigest()
key=hashlib.sha256(b'DiabloGBA/0.9.4/MPQ-ROM/v2'+digest).digest();nonce=os.urandom(12)
header=b'DG095P02'+nonce+bytes.fromhex(romhash)+bytes(12)
package=header+AESGCM(key).encrypt(nonce,rom,header)
# Authenticated GOG key wrapping preserves one encrypted ROM payload, avoiding
# a second 32 MiB copy. Only the ciphertext and public AAD/nonce/tag are stored.
gog_key=None
if gog_digest is not None:
 gog_key=hashlib.sha256(b'DiabloGBA/0.9.4/MPQ-ROM/v2'+gog_digest).digest()
 gog_header=b'DG095K02'+os.urandom(12)+bytes.fromhex(romhash)+bytes(12)
 gog_wrapped=AESGCM(gog_key).encrypt(gog_header[8:20],key,gog_header)
else:
 prior=a.reuse_key_config.read_text()
 assert '#define KEY_DOMAIN "DiabloGBA/0.9.4/MPQ-ROM/v2"' in prior
 previous=a.reference_package.read_bytes()
 previous_plain=AESGCM(key).decrypt(previous[8:20],previous[64:],previous[:64])
 prior_hash=re.search(r'#define ROM_SHA256 "([a-f0-9]{64})"',prior).group(1)
 assert hashlib.sha256(previous_plain).hexdigest()==prior_hash
 assert previous[20:52]==bytes.fromhex(prior_hash)
 def old_array(name):
  body=re.search(name+r'\[\d+\]=\{([^}]+)\}',prior).group(1)
  return bytes(int(v,16) for v in body.split(','))
 gog_header=old_array('gog_key_header')
 # A reused key wrapper retains its original authenticated header, including
 # the ROM hash from when the key was first wrapped. Later payload builds
 # change ROM_SHA256 but cannot rewrite GOG AAD without the GOG key.
 assert len(gog_header)==64 and gog_header[:8] in (b'DG094K02',b'DG095K02') 
 gog_wrapped=old_array('gog_key_cipher')+old_array('gog_key_tag')
 assert len(gog_header)==64 and len(gog_wrapped)==48
pack=build/'v095.dgb';pack.write_bytes(package)
# The assembler's .incbin input is not tracked by every Zig cache path.
# Put the payload hash in the included filename so changed ROM data also
# changes the assembly source, even when rebuilding the same release number.
package_hash=hashlib.sha256(package).hexdigest()
embedded_pack=build/('payload-'+package_hash+'.dgb');embedded_pack.write_bytes(package)
embedded_source=(ROOT/'src/embedded.S').read_text()
assert '"build/v095.dgb"' in embedded_source
embedded=build/'embedded-payload.S'
embedded.write_text(embedded_source.replace('"build/v095.dgb"','"build/'+embedded_pack.name+'"'))
config='''#ifndef BUILDER_CONFIG_H
#define BUILDER_CONFIG_H
#define BUILDER_VERSION "0.9.5"
#define ROM_VERSION "V0.9.5"
#define PACK_NAME "v095.dgb"
#define PACK_MAGIC "DG095P02"
#define KEY_DOMAIN "DiabloGBA/0.9.4/MPQ-ROM/v2"
#define MPQ_SIZE UINT64_C(517501242)
#define MPQ_GOG_SIZE UINT64_C(517501282)
#define ROM_SIZE (32u * 1024u * 1024u)
#define HEADER_SIZE 64u
'''
def c_array(name,data):
 return 'static const unsigned char '+name+'['+str(len(data))+']={'+','.join('0x'+format(b,'02x') for b in data)+'};\n'
config+='#define ROM_SHA256 "'+romhash+'"\n'
config+=c_array('expected_rom_hash',bytes.fromhex(romhash))
config+=c_array('gog_key_header',gog_header)+c_array('gog_key_cipher',gog_wrapped[:32])+c_array('gog_key_tag',gog_wrapped[32:])+'#endif\n'
(ROOT/'src/config.h').write_text(config)
for f in [ROOT/'tests/verify.py',ROOT/'tests/verify_single.py']:
 t=f.read_text();t=re.sub(r'ROM_HASH = "[a-f0-9]{64}"','ROM_HASH = "'+romhash+'"',t);f.write_text(t)
mac=ROOT/'build_macos.py';t=mac.read_text();t=re.sub(r"PACK_HASH = '[a-f0-9]{64}'","PACK_HASH = '"+hashlib.sha256(package).hexdigest()+"'",t);mac.write_text(t)
zig=str(a.zig.resolve())
flags=['-std=c11','-O2','-s','-Wall','-Wextra','-Werror']
def run(command):subprocess.run(command,cwd=ROOT,check=True)
exe=a.output/'DiabloGbaRomBuilder-0.9.5.exe'
run([zig,'cc','-target','x86_64-windows-gnu',*flags,'-municode','-Wl,--subsystem,windows','src/core.c','src/windows.c',str(embedded),'-lbcrypt','-lcomdlg32','-lcomctl32','-luser32','-lgdi32','-o',str(exe)])
# Preserve the existing single-file 48MiB distribution envelope.
with exe.open('ab') as f:f.truncate(48*1024*1024)
assert exe.read_bytes().count(package)==1,'Windows embedded payload does not match this build'
run(['gcc',*flags,'src/core.c','tests/single_driver.c',str(embedded),'-lcrypto','-o',str(build/'single-driver')])
run(['gcc',*flags,'src/core.c','src/cli.c','-lcrypto','-o',str(build/'file-driver')])
run(['gcc',*flags,'src/core.c','tests/progress_driver.c','-lcrypto','-o',str(build/'progress-driver')])
run(['python3',str(mac),'--zig',zig,'--package',str(pack),'--output',str(a.output/'macOS')])
# Same portable crypto and C core as the Mac app, executed on the build host.
run([zig,'build-lib','src/crypto.zig','-static','-O','ReleaseSafe','-lc','-femit-bin='+str(build/'crypto-linux.a')])
run([zig,'cc',*flags,'-DBUILDER_PORTABLE_CRYPTO','src/core.c','src/cli.c',str(build/'crypto-linux.a'),'-lpthread','-ldl','-lm','-o',str(build/'portable-driver')])
run([zig,'cc',*flags,'-DBUILDER_PORTABLE_CRYPTO','src/core.c','tests/progress_driver.c',str(build/'crypto-linux.a'),'-lpthread','-ldl','-lm','-o',str(build/'portable-progress-driver')])
for binary in [exe,*build.glob('builder-arm64'),*build.glob('builder-x86_64')]:
 b=binary.read_bytes()
 for secret in [x for x in [digest,key,gog_digest,gog_key] if x is not None]:assert secret not in b and secret.hex().encode() not in b
report={'version':'0.9.5','rom_sha256':romhash,'package_sha256':package_hash,'windows_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'supported_mpqs':[{'edition':'Diablo 1.0','bytes':517501242},{'edition':'GOG','bytes':517501282}],'embedded_payload_verified':True,'mpq_key_embedded':False,'windows_runtime_tested':False,'macos_runtime_tested':False,'gog_mpq_retested':bool(a.gog_mpq),'gog_wrapper_reused':not bool(a.gog_mpq)}
(a.output/'BUILD.json').write_text(json.dumps(report,indent=2)+'\n')
print('Built Windows x64 and macOS universal release 0.9.5.',flush=True)
