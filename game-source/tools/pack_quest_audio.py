"""Original complete quest cue, existing 16384Hz signed8 FIFO-B format."""
from pathlib import Path
import json,hashlib,subprocess
from audio_pcm import guarded_pcm
r=Path(__file__).resolve().parents[1];src=r/'source_assets/quest_remaining/Questdon.wav';out=r/'generated'
pcm=subprocess.run(['ffmpeg','-v','error','-i',str(src),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
packed=guarded_pcm(pcm);(out/'audio_quest_complete.lz').write_bytes(packed)
(out/'quest_audio.h').write_text('#pragma once\nextern const uint8_t asset_audio_quest_complete[];\n#define AUDIO_QUEST_COMPLETE_LENGTH '+str(len(pcm))+'\n')
report={'source':'Sfx\\Misc\\Questdon.wav','source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'pcm16384_signed8_bytes':len(pcm),'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'guarded_aligned_bytes':len(packed),'runtime_included':True,'truncated':False,'playback_hz':16384}
(out/'water-completion-audio-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
