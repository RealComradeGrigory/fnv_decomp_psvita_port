"""Check original world import/streaming with real local BSAs and stubbed GXM."""
from pathlib import Path
import argparse,os,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__)
for name in ('project','sdk','zig','game-data','zlib-dll'):p.add_argument('--'+name,required=True,type=Path)
p.add_argument('--preview-dir',type=Path)
p.add_argument('--checks',type=Path,help='alternate regression source')
a=p.parse_args();work=Path(__file__).resolve().parent;project=a.project.resolve();src=project/'src'
headers=work/'vita-host-headers';headers.mkdir(exist_ok=True)
for name in ('psp2','psp2common','vitasdk'):shutil.copytree(a.sdk/'arm-vita-eabi/include'/name,headers/name,dirs_exist_ok=True)
for name in ('zlib.h','zconf.h'):shutil.copy2(a.sdk/'arm-vita-eabi/include'/name,headers/name)
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(work/'zig-cache'))
def run(cmd):subprocess.run([str(x) for x in cmd],check=True,env=env)
zig=a.zig.resolve();(work/'zlib.def').write_text('LIBRARY zlib1.dll\nEXPORTS\nuncompress\n')
run([zig,'dlltool','-m','i386:x86-64','-D','zlib1.dll','-d',work/'zlib.def','-l',work/'zlib.lib'])
shutil.copy2(a.zlib_dll,work/'zlib1.dll')
files=[a.checks.resolve() if a.checks else work/'visual_review/world_checks.cpp',work/'gxm_noops.cpp',*[src/f for f in ['renderer/gxm_renderer.cpp','pipboy/pipboy.cpp','dds/dds_loader.cpp','nif/nif_loader.cpp','bsa/bsa_reader.cpp','physics/physics_engine.cpp','scene/cell_scene.cpp','scene/map_data.cpp','scene/world_scene.cpp']]]
run([zig,'c++','-std=c++17','-O1',*(['-DFNV_WORLD_CAPTURE'] if a.preview_dir else []),'-I',headers,'-I',src,*files,work/'zlib.lib','-o',work/'world_checks.exe'])
if a.preview_dir:a.preview_dir.mkdir(parents=True,exist_ok=True)
run([work/'world_checks.exe',project,a.game_data.resolve(),*([a.preview_dir.resolve()] if a.preview_dir else [])])
