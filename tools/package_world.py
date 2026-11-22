"""Append world resources without exceeding Windows' process argument limit."""
import argparse,os,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--vpk',required=True,type=Path);p.add_argument('--world',required=True,type=Path);a=p.parse_args()
if not (a.world/'catalog.bin').is_file():raise SystemExit('Export the original world data before packaging')
temporary=a.vpk.with_suffix('.world.tmp.vpk')
try:
    with zipfile.ZipFile(a.vpk) as old,zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as z:
        for info in old.infolist():
            if not info.filename.startswith('world/'):z.writestr(info,old.read(info))
        for source in sorted(a.world.rglob('*')):
            if source.is_file():z.write(source,'world/'+source.relative_to(a.world).as_posix())
    with zipfile.ZipFile(temporary) as z:
        assert z.testzip() is None and len(z.namelist())==len(set(z.namelist()))
    os.replace(temporary,a.vpk)
finally:
    if temporary.exists():temporary.unlink()
print('World resources packaged:',a.vpk.stat().st_size,'bytes')
