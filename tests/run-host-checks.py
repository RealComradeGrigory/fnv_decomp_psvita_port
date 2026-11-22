"""Build host checks with Zig and VitaSDK declarations; no Vita device required."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project',required=True,type=Path)
parser.add_argument('--sdk',required=True,type=Path)
parser.add_argument('--zig',required=True,type=Path)
parser.add_argument('--fixtures',type=Path,help='directory containing extracted assets and manifest.json')
parser.add_argument('--zlib-dll',type=Path,help='host Windows zlib1.dll for original animation decompression')
args=parser.parse_args()
root=Path(__file__).resolve().parent
src=args.project.resolve()/'src'
sdk=args.sdk.resolve()/'arm-vita-eabi/include'
headers=root/'vita-host-headers'
headers.mkdir(exist_ok=True)
for name in ('psp2','psp2common','vitasdk'):
    shutil.copytree(sdk/name,headers/name,dirs_exist_ok=True)
for name in ('zlib.h','zconf.h'):
    shutil.copy2(sdk/name,headers/name)
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(root/'zig-cache'))
zig=str(args.zig.resolve())
def run(command):
    subprocess.run([str(x) for x in command],cwd=root,env=env,check=True)

zlib_link=[]
if args.zlib_dll:
    (root/'zlib.def').write_text('LIBRARY zlib1.dll\nEXPORTS\nuncompress\n')
    run([zig,'dlltool','-m','i386:x86-64','-D','zlib1.dll','-d',root/'zlib.def','-l',root/'zlib.lib'])
    shutil.copy2(args.zlib_dll,root/'zlib1.dll')
    zlib_link=[root/'zlib.lib']
def compile(output,files,shared=False):
    run([zig,'c++','-std=c++17','-O1','-DFNV_APP_ROOT="'+args.project.resolve().as_posix()+'/"',*(['-shared'] if shared else []),'-I',headers,'-I',src,
         *files,*zlib_link,'-o',root/output])

renderer=[root/'gxm_noops.cpp',src/'renderer/gxm_renderer.cpp',src/'pipboy/pipboy.cpp',src/'dds/dds_loader.cpp']
compile('physics_checks.exe',[root/'physics_checks.cpp',src/'physics/physics_engine.cpp'])
compile('ui_gpu_checks.exe',[root/'ui_gpu_checks.cpp',*renderer])
run([root/'physics_checks.exe'])
run([root/'ui_gpu_checks.exe'])
if (src/'renderer/cpu_workers.h').exists():
    compile('threaded_checks.exe',[root/'threaded_checks.cpp',*renderer])
    run([root/'threaded_checks.exe'])
if args.fixtures:
    fixture_dir=args.fixtures.resolve()
    manifest=json.loads((fixture_dir/'manifest.json').read_text())
    compile('loader_checks.dll',[root/'loader_checks.cpp',src/'nif/nif_loader.cpp',src/'dds/dds_loader.cpp'],shared=True)
    run(['python',root/'run_loader_checks.py',fixture_dir])
    tsv=root/'fixtures.tsv'
    tsv.write_text('\n'.join(x['asset'].lower()+'\t'+x['file'] for x in manifest))
    compile('scene_checks.exe',[root/'scene_checks.cpp',*renderer,src/'nif/nif_loader.cpp',
            src/'physics/physics_engine.cpp',src/'scene/cell_scene.cpp',src/'weapon/weapon_system.cpp'])
    run([root/'scene_checks.exe',tsv,args.project.resolve()/'cells'])

    compile('visual_checks.exe',[root/'visual_review/visual_checks.cpp',*renderer,src/'nif/nif_loader.cpp',
            src/'physics/physics_engine.cpp',src/'scene/cell_scene.cpp',src/'weapon/weapon_system.cpp',src/'npc/npc_system.cpp'])
    run([root/'visual_checks.exe',tsv,args.project.resolve()/'assets','unused'])

    compile('regressions005.exe',[root/'visual_review/regressions005.cpp',*renderer,src/'nif/nif_loader.cpp',
            src/'physics/physics_engine.cpp',src/'scene/cell_scene.cpp',src/'weapon/weapon_system.cpp'])
    run([root/'regressions005.exe',tsv,args.project.resolve()])
