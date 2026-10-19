"""Read a macOS installer package without Apple's tools: the XAR archive (.pkg) and the cpio archive
inside it (its Scripts or Payload), for tools/prepare.py on Linux and Windows. Read only; nothing is run.

macOS's prepare.py uses `xar` and `tar` instead. Either way the pinned hashes (config/inputs.json) decide whether
what came out is the expected game."""
import bz2
import gzip
import hashlib
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path, PurePosixPath


def _safe(dest, name):
    rel = PurePosixPath(name)
    if rel.is_absolute() or '..' in rel.parts:
        raise ValueError(f'unsafe path in archive: {name}')
    return Path(dest, *rel.parts)


def xar_extract(package, member, dest):
    """Extract one file (`member`: its path inside the XAR, e.g. 'package.pkg/Scripts') to dest/member."""
    with open(package, 'rb') as f:
        head = f.read(28)
        magic, size, _version, toc_len, _toc_raw, _cksum = struct.unpack('>4sHHQQI', head)
        if magic != b'xar!':
            raise ValueError('not a XAR archive')
        f.seek(size)
        toc = ET.fromstring(zlib.decompress(f.read(toc_len)))
        heap = size + toc_len

        def find(node, parts):
            for child in node.findall('file'):
                if child.findtext('name') == parts[0]:
                    return child if len(parts) == 1 else find(child, parts[1:])
            return None
        entry = find(toc.find('toc'), member.split('/'))
        if entry is None or entry.findtext('type') != 'file':
            raise KeyError(f'{member} not in the package')
        data = entry.find('data')
        offset, length = int(data.findtext('offset')), int(data.findtext('length'))
        style = data.find('encoding').get('style') if data.find('encoding') is not None else 'application/octet-stream'
        f.seek(heap + offset)
        raw = f.read(length)
    check = data.find('archived-checksum')
    if check is not None and check.get('style') in ('sha1', 'md5', 'sha256'):
        if hashlib.new(check.get('style'), raw).hexdigest() != check.text.strip().lower():
            raise ValueError(f'{member}: archived checksum mismatch')
    if style == 'application/x-gzip':
        raw = zlib.decompress(raw)
    elif style == 'application/x-bzip2':
        raw = bz2.decompress(raw)
    elif style != 'application/octet-stream':
        raise ValueError(f'{member}: unsupported encoding {style}')
    out = Path(dest, *member.split('/'))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(raw)
    return out


def _cpio_entries(data):
    """(name, mode, body) for each entry of an odc (070707) or newc (070701/070702) cpio archive."""
    at = 0
    while at < len(data):
        magic = data[at:at + 6]
        if magic == b'070707':
            h = data[at:at + 76]
            mode, namesize, filesize = int(h[18:24], 8), int(h[59:65], 8), int(h[65:76], 8)
            name_at = at + 76
            name = data[name_at:name_at + namesize - 1].decode('utf-8', 'surrogateescape')
            body_at = name_at + namesize
            at = body_at + filesize
        elif magic in (b'070701', b'070702'):
            h = data[at:at + 110]
            mode, filesize, namesize = int(h[14:22], 16), int(h[54:62], 16), int(h[94:102], 16)
            name_at = at + 110
            name = data[name_at:name_at + namesize - 1].decode('utf-8', 'surrogateescape')
            body_at = (name_at + namesize + 3) & ~3
            at = (body_at + filesize + 3) & ~3
        else:
            raise ValueError(f'unknown cpio header at {at}')
        if name == 'TRAILER!!!':
            return
        yield name, mode, data[body_at:body_at + filesize]


def cpio_extract(archive, dest, prefix):
    """Extract the entries under `prefix` (e.g. './payload/Contents/Resources/game') of a cpio archive,
    gzip-compressed or not. Symbolic links are left out."""
    data = Path(archive).read_bytes()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    norm = lambda n: n[2:] if n.startswith('./') else n
    want = norm(prefix).rstrip('/')
    count = 0
    for name, mode, body in _cpio_entries(data):
        n = norm(name)
        if n != want and not n.startswith(want + '/'):
            continue
        out = _safe(dest, n)
        kind = mode & 0o170000
        if kind == 0o040000:
            out.mkdir(parents=True, exist_ok=True)
        elif kind == 0o100000:
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(body)
            count += 1
    if not count:
        raise KeyError(f'{prefix} not in {archive}')
    return count
