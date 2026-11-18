"""Build a local-import Modrinth pack from the tested Windows release artifacts."""
import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = {
    'path': 'mods/fabric-api-0.141.6+1.21.11.jar',
    'hashes': {
        'sha1': 'c98467cbbaf4d197377266795ae015f4130d65b6',
        'sha512': '852d3682c4f353fd0ca86546af8578513e8bcf628377fa9d49c556bc59c5024c69ab56c92fc6dfba11fed19e4316a3f5636e7e07fd3d96f7043b531aef59b637',
    },
    'env': {'client': 'required', 'server': 'unsupported'},
    'downloads': ['https://cdn.modrinth.com/data/P7dR8mSH/versions/6qAuTtLR/fabric-api-0.141.6%2B1.21.11.jar'],
    'fileSize': 2426039,
}


def main():
    version = re.search(r'project\(CUDACraft VERSION ([0-9.]+)', (ROOT/'CMakeLists.txt').read_text())[1]
    mod = ROOT/f'fabric-client/build/libs/cuda-craft-client-{version}.jar'
    native = ROOT/'build/cuda/cuda-craft.exe'
    with zipfile.ZipFile(mod) as jar:
        manifest = json.loads(jar.read('fabric.mod.json'))
        if manifest['version'] != version or manifest['id'] != 'cudacraft_client':
            raise RuntimeError('Fabric release version mismatch')
    index = {
        'formatVersion': 1, 'game': 'minecraft', 'versionId': version,
        'name': 'CUDA Craft',
        'summary': 'Windows x64 native singleplayer backend; experimental vanilla gameplay.',
        'dependencies': {'minecraft': '1.21.11', 'fabric-loader': '0.20.1'},
        'files': [API],
    }
    payload = {
        'mods/cuda-craft-client.jar': mod,
        'cudacraft/native/cuda-craft.exe': native,
        'CUDA-Craft-README.md': ROOT/'docs/MODRINTH.md',
        'cudacraft/docs/EXPANSION_0_11.md': ROOT/'docs/EXPANSION_0_11.md',
        'cudacraft/docs/MOBS.md': ROOT/'docs/MOBS.md',
        'cudacraft/docs/PORTALS.md': ROOT/'docs/PORTALS.md',
    }
    for file in (ROOT/'third_party').rglob('*'):
        if file.is_file() and (file.name.startswith('LICENSE') or file.name.startswith('NOTICE')):
            payload['cudacraft/licenses/'+file.relative_to(ROOT/'third_party').as_posix()] = file
    payload['cudacraft/licenses/zlib/LICENSE'] = ROOT/'.tools/dependencies/zlib-1.4/LICENSE'
    output = ROOT/'dist'/f'CUDA-Craft-{version}-windows-x64.mrpack'
    output.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr('modrinth.index.json', json.dumps(index, indent=2)+'\n')
        for path, source in sorted(payload.items()):
            archive.write(source, 'client-overrides/'+path)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('Invalid ZIP payload')
        for path, source in payload.items():
            if archive.read('client-overrides/'+path) != source.read_bytes():
                raise RuntimeError('Pack payload mismatch: '+path)
        if json.loads(archive.read('modrinth.index.json')) != index:
            raise RuntimeError('Pack manifest mismatch')
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.mrpack.sha256').write_text(digest+'  '+output.name+'\n')
    print(f'Created and verified {output} ({output.stat().st_size:,} bytes)')


if __name__ == '__main__':
    main()
