"""Maintainer tool: record byte patches against SHA256-pinned upstream files."""
import base64
import difflib
import hashlib
import json
import urllib.request
from pathlib import Path

root=Path(__file__).resolve().parents[1]
path=root/'tools/hyperchunk-manifest.json'
manifest=json.loads(path.read_text(encoding='utf-8-sig'))
manifest['repository']='Soju06/hyperchunk'
patches={};patched={}
for name,expected in manifest['files'].items():
    local=(root/'third_party/hyperchunk'/name).read_bytes()
    actual=hashlib.sha256(local).hexdigest()
    if actual==expected:continue
    url=f"https://raw.githubusercontent.com/{manifest['repository']}/{manifest['revision']}/{name}"
    original=urllib.request.urlopen(url).read()
    assert hashlib.sha256(original).hexdigest()==expected
    before=original.splitlines(keepends=True);after=local.splitlines(keepends=True)
    offsets=[0]
    for line in before:offsets.append(offsets[-1]+len(line))
    edits=[]
    for tag,a,b,c,d in difflib.SequenceMatcher(a=before,b=after,autojunk=False).get_opcodes():
        if tag!='equal':edits.append({'offset':offsets[a],'length':offsets[b]-offsets[a],'base64':base64.b64encode(b''.join(after[c:d])).decode()})
    patches[name]=edits;patched[name]=actual
manifest['patched_files']=patched
path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
(root/'tools/hyperchunk-patches.json').write_text(json.dumps(patches,indent=2)+'\n')
print(f'Recorded {len(patches)} adapted files; original and resulting hashes pinned')
