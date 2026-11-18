"""Check PE runtime imports and run storage/status tests on an extracted mrpack.

This is an installation smoke test on the current Windows machine, not proof of
Vanilla parity or a substitute for an actual Modrinth application import.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def pe_imports(path):
    data = path.read_bytes()
    def u16(p): return struct.unpack_from('<H', data, p)[0]
    def u32(p): return struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0' or u16(pe+4) != 0x8664:
        raise ValueError('Expected a Windows x64 PE executable')
    opt = pe + 24
    if u16(opt) != 0x20b: raise ValueError('Expected PE32+')
    table = opt + u16(pe+20)
    sections = [(u32(table+i*40+12), u32(table+i*40+16), u32(table+i*40+20)) for i in range(u16(pe+6))]
    def offset(rva):
        for address, size, start in sections:
            if address <= rva < address + size: return start + rva - address
        raise ValueError('Invalid PE RVA')
    names = set()
    # Both regular and delayed imports must be self-contained apart from Windows.
    for directory, width, name_index in [(1, 20, 12), (13, 32, 4)]:
        if u32(opt+108) <= directory: continue
        rva = u32(opt + 112 + directory*8)
        if not rva: continue
        pos = offset(rva)
        for _ in range(1024):
            descriptor = data[pos:pos+width]
            if len(descriptor) != width: raise ValueError('Truncated import descriptor')
            if not any(descriptor): break
            if directory == 13 and u32(pos) != 1: raise ValueError('Unsupported delayed import addressing')
            name = offset(u32(pos+name_index)); end = data.index(0, name, name+256)
            names.add(data[name:end].decode('ascii').lower()); pos += width
        else: raise ValueError('Unterminated import directory')
    return sorted(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pack', type=Path, nargs='?')
    parser.add_argument('--inspect-exe', type=Path)
    args = parser.parse_args()
    if args.inspect_exe:
        print(json.dumps(pe_imports(args.inspect_exe), indent=2)); return
    if not args.pack: parser.error('Provide a .mrpack or --inspect-exe')
    pack = args.pack.resolve(); digest = hashlib.sha256(pack.read_bytes()).hexdigest()
    if pack.with_suffix('.mrpack.sha256').read_text().split()[0] != digest:
        raise ValueError('Pack checksum mismatch')
    with tempfile.TemporaryDirectory(prefix='CUDA Release ü ') as temporary:
        instance = Path(temporary) / 'Frische Installation ö'; instance.mkdir()
        with zipfile.ZipFile(pack) as archive:
            index = json.loads(archive.read('modrinth.index.json'))
            if index['dependencies']['minecraft'] != '1.21.11': raise ValueError('Wrong Minecraft target')
            for info in archive.infolist():
                path = PurePosixPath(info.filename)
                if path.is_absolute() or '..' in path.parts or '\\' in info.filename or ':' in info.filename:
                    raise ValueError('Unsafe archive member')
                if path.parts[0] != 'client-overrides' or info.is_dir(): continue
                target = instance.joinpath(*path.parts[1:]); target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(info))
        executable = instance / 'cudacraft/native/cuda-craft.exe'
        imports = pe_imports(executable)
        dependencies = [name for name in imports if re.match(r'(vcruntime|msvcp|concrt|cudart|zlib|api-ms-win-crt)', name)]
        if dependencies: raise ValueError('Unbundled runtime dependencies: ' + ', '.join(dependencies))
        env = dict(os.environ)
        env['PATH'] = str(Path(os.environ['SYSTEMROOT']) / 'System32')
        env.pop('CUDA_PATH', None); env.pop('CUDAToolkit_ROOT', None)
        version = subprocess.check_output([str(executable), '--version'], cwd=instance, env=env, timeout=10).decode().strip()
        if not version.startswith('CUDA Craft ' + index['versionId'] + ' |'): raise ValueError('Native version mismatch')
        with zipfile.ZipFile(instance / 'mods/cuda-craft-client.jar') as mod:
            if json.loads(mod.read('fabric.mod.json'))['version'] != index['versionId']: raise ValueError('Mod version mismatch')
        subprocess.run([sys.executable, str(ROOT / 'tests/world_storage_test.py'), str(executable)],
                       cwd=instance, env=env, check=True, timeout=60)
        report = {'pack': pack.name, 'sha256': digest, 'version': index['versionId'],
                  'native_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                  'pe_imports': imports, 'isolated_path_storage_tests': 'passed',
                  'modrinth_ui_import_tested': False, 'vanilla_parity_verified': False}
    report_path = pack.with_suffix('.verification.json')
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('Extracted release checks passed: ' + str(report_path))


if __name__ == '__main__': main()
