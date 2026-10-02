"""Generate a project using a SHA-bound, metadata-only runtime profile.

This never executes the supplied PE or uploads it. The resulting sources are
private game-derived output; keep the destination outside tracked source.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tool',type=Path,required=True)
    ap.add_argument('--exe',type=Path,required=True)
    ap.add_argument('--profile',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    profile=json.loads(args.profile.read_text())
    if profile.get('schema')!='winrecomp.runtime-profile.v1':raise ValueError('unknown profile schema')
    actual=hashlib.sha256(args.exe.read_bytes()).hexdigest()
    if actual!=profile['sha256']:raise ValueError('profile/input SHA-256 mismatch; no addresses applied')
    seeds=profile['seeds']
    if not isinstance(seeds,list) or any(type(n) is not int or not 0<=n<=0xffffffff for n in seeds):raise ValueError('invalid guest entry seed')
    command=[str(args.tool.resolve()),'project',str(args.exe.resolve()),str(args.out.resolve())]
    for seed in sorted(set(seeds)):command+=['--seed',hex(seed)]
    subprocess.run(command,check=True)

if __name__=='__main__':main()
