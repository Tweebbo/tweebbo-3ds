#!/usr/bin/env python3
"""Merge an incremental furniture export while retaining every earlier variant."""
import json
import shutil
from pathlib import Path

root=Path(__file__).resolve().parent.parent
base=root/'build/export';extra=root/'build/expansion-export'
reference=root/'build/export-baseline'
if not reference.exists():
    original=Path.home()/'Tweebbo-feedback-next/3ds/build/export'
    reference.mkdir()
    shutil.copy2(original/'furni.json',reference/'furni.json')
    shutil.copytree(original/'furni',reference/'furni')
before=json.loads((reference/'furni.json').read_text());added=json.loads((extra/'furni.json').read_text())
shutil.copytree(reference/'furni',base/'furni',dirs_exist_ok=True)
for row in added:
    for part in row['parts']:
        part['file']='expansion-'+part['file']
ids={r['def'] for r in added}
kept=[r for r in before if r['def'] not in ids]
(base/'furni.json').write_text(json.dumps(kept+added,separators=(',',':')))
for file in (extra/'furni').iterdir():
    if file.is_file():shutil.copy2(file,base/'furni'/('expansion-'+file.name))
for folder in ['icons','data']:
    shutil.copytree(extra/folder,base/folder,dirs_exist_ok=True)
shutil.copy2(extra/'furni-timelines.json',base/'furni-timelines.json')
current=root.parent/'hotel/client/public/models/catalogue.json'
shutil.copy2(current,base/'data/models/catalogue.json')
shutil.copy2(root.parent/'hotel/client/public/models/furni-anim.json',base/'data/models/furni-anim.json')
provenance=json.loads((extra/'source.json').read_text())
provenance['retainedBase']='/home/miguelv/Tweebbo-feedback-next/3ds/build/export'
provenance['incrementalDefinitions']=sorted(ids)
(base/'source.json').write_text(json.dumps(provenance,indent=2)+'\n')
report={'retainedVariants':len(kept),'newVariants':len(added),'newDefinitionIds':sorted(ids)}
(root.parent/'export-merge.json').write_text(json.dumps(report,indent=2)+'\n')
print(f'Retained {len(kept)} variants; merged {len(added)} variants from {len(ids)} definitions')
