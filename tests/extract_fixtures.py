import argparse
import json
import struct
import zlib
from pathlib import Path

GAME = Path(r'E:\SteamLibrary\steamapps\common\Fallout New Vegas enplczru\Data')
PROJECT = Path(r'C:\Users\Grigory\.gemini\antigravity\scratch\fnv_vita')
parser = argparse.ArgumentParser(description="Extract local game fixtures for the host checks")
parser.add_argument('--project', type=Path, default=PROJECT)
parser.add_argument('--game-data', type=Path, default=GAME)
args = parser.parse_args()
PROJECT = args.project
GAME = args.game_data
OUT = Path(__file__).parent / 'assets'
OUT.mkdir(exist_ok=True)


class Archive:
    def __init__(self, path):
        self.file = path.open('rb')
        magic, version, offset, self.flags, folders, files, folder_names, file_names, _ = struct.unpack('<4s8I', self.file.read(36))
        assert magic == b'BSA\0' and version == 104
        self.file.seek(offset)
        records = [struct.unpack('<QII', self.file.read(16)) for _ in range(folders)]
        entries = []
        for _, count, _ in records:
            length = self.file.read(1)[0]
            folder = self.file.read(length).rstrip(b'\0').decode('ascii').lower()
            for _ in range(count):
                _, size, position = struct.unpack('<QII', self.file.read(16))
                entries.append((folder, size, position))
        names = self.file.read(file_names).split(b'\0')
        self.entries = {folder + '\\' + name.decode('ascii').lower(): (size, position)
                        for (folder, size, position), name in zip(entries, names)}

    def extract(self, path):
        size, position = self.entries[path.lower()]
        self.file.seek(position)
        raw = self.file.read(size & 0x3fffffff)
        if self.flags & 0x100:
            raw = raw[1 + raw[0]:]
        if bool(self.flags & 4) != bool(size & 0x40000000):
            expected, = struct.unpack_from('<I', raw)
            raw = zlib.decompress(raw[4:])
            assert len(raw) == expected
        return raw


def texture_paths(raw):
    pos = raw.index(b'\n') + 1
    version, endian, user, blocks, bsver = struct.unpack_from('<IBIII', raw, pos)
    pos += 17
    for _ in range(3):
        pos += 1 + raw[pos]
    count, = struct.unpack_from('<H', raw, pos)
    pos += 2
    types = []
    for _ in range(count):
        length, = struct.unpack_from('<I', raw, pos)
        pos += 4
        types.append(raw[pos:pos + length].decode('ascii'))
        pos += length
    ids = struct.unpack_from('<' + 'H' * blocks, raw, pos)
    pos += blocks * 2
    sizes = struct.unpack_from('<' + 'I' * blocks, raw, pos)
    pos += blocks * 4
    strings, _ = struct.unpack_from('<II', raw, pos)
    pos += 8
    for _ in range(strings):
        length, = struct.unpack_from('<I', raw, pos)
        pos += 4 + length
    groups, = struct.unpack_from('<I', raw, pos)
    pos += 4 + groups * 4
    paths = []
    for type_id, size in zip(ids, sizes):
        if types[type_id & 0x7fff] == 'BSShaderTextureSet':
            count, length = struct.unpack_from('<II', raw, pos)
            if count and length:
                paths.append(raw[pos + 8:pos + 8 + length].decode('ascii'))
        pos += size
    return paths


def main():
    meshes = Archive(GAME / 'Fallout - Meshes.bsa')
    paths = {
        'meshes\\pipboy3000\\pipboyarm.nif',
        'meshes\\weapons\\1handpistol\\10mmpistol.nif',
        'meshes\\weapons\\1handpistol\\laserpistol.nif',
        'meshes\\weapons\\2handautomatic\\servicerifle.nif',
        'meshes\\weapons\\2handrifle\\caravanshotgun.nif',
        'meshes\\weapons\\2handrifle\\varmintrifle.nif',
        'meshes\\characters\\_male\\righthand1st.nif',
        'meshes\\characters\\_male\\lefthandpipboyglove1st.nif',
        'meshes\\armor\\republicans\\republican_02.nif',
        'meshes\\characters\\head\\eyelefthuman.nif',
        'meshes\\characters\\head\\headhuman.nif',
        'meshes\\characters\\head\\eyerighthuman.nif',
        'meshes\\characters\\head\\mouthhuman.nif',
        'meshes\\characters\\head\\teethupperhuman.nif',
        'meshes\\characters\\head\\teethlowerhuman.nif',
        'meshes\\characters\\head\\tonguehuman.nif',
        'meshes\\characters\\hair\\hairbaldingold.nif',
        'meshes\\characters\\hair\\beardmustachecurly.nif',
        'meshes\\characters\\hair\\eyebrowm.nif',
    }
    for cell in (PROJECT / 'cells').glob('*.cell'):
        data = cell.read_bytes()
        count, = struct.unpack_from('<I', data, 80)
        assert len(data) >= 104 + count * 164
        for i in range(count):
            name = data[104 + i * 164 + 8:104 + i * 164 + 136].split(b'\0')[0].decode('ascii').lower()
            if not name.startswith('meshes\\'):
                name = 'meshes\\' + name
            paths.add(name)
    
    manifest = []
    textures = set()
    for i, path in enumerate(sorted(paths)):
        try:
            raw = meshes.extract(path)
        except KeyError:
            continue
        fixture = OUT / f'{i:04d}.nif'
        fixture.write_bytes(raw)
        textures.update(texture_paths(raw))
        manifest.append({'file': str(fixture), 'asset': path})
    
    for archive_name in ('Fallout - Textures.bsa', 'Fallout - Textures2.bsa'):
        archive_path = GAME / archive_name
        if not archive_path.exists():
            continue
        archive = Archive(archive_path)
        for path in list(textures):
            normalized = path.lower().replace('/', '\\')
            if not normalized.startswith('textures\\'):
                normalized = 'textures\\' + normalized
            try:
                raw = archive.extract(normalized)
            except KeyError:
                continue
            fixture = OUT / f'{len(manifest):04d}.dds'
            fixture.write_bytes(raw)
            manifest.append({'file': str(fixture), 'asset': normalized})
            textures.remove(path)
    
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print('Extracted', sum(x['file'].endswith('.nif') for x in manifest), 'NIFs and',
          sum(x['file'].endswith('.dds') for x in manifest), 'DDS textures')


if __name__ == "__main__":
    main()
