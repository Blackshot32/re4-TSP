"""Run one opt-in renderer experiment; keep the launcher defaults untouched."""
import pathlib
import shlex
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent
label = sys.argv[1]
flags = sys.argv[2:]
out = root / 'build' / 'glprof-results'
out.mkdir(exist_ok=True)

def ssh(command, **kw):
    return subprocess.run(['ssh', 'trimui', command], check=True, **kw)

# Stop before replacing a mapped shared library. Give MainUI's loop time to return.
ssh('pkill -9 re4_host 2>/dev/null; sleep 6; '
    'test -f /mnt/SDCARD/re4-poc/libre4_host.pre-glprof.so || '
    'cp /mnt/SDCARD/re4-poc/libre4_host.so /mnt/SDCARD/re4-poc/libre4_host.pre-glprof.so')
with (root / 'build/whole-aarch64/libre4_host.so').open('rb') as f:
    ssh('cat > /mnt/SDCARD/re4-poc/libre4_host.so', stdin=f)
env = ['RE4_GX_GLPROF=1', 'RE4_FRONT_MERGE=1', 'RE4_FRONT_SCALE=1', 'RE4_GX_THREADS=1'] + flags
command = 'env ' + ' '.join(shlex.quote(x) for x in env) + ' /mnt/SDCARD/re4-poc/play.sh'
script = '#!/bin/sh\n' + command + ' > /mnt/SDCARD/re4-poc/glprof-' + label + '.log 2>&1\n'
ssh("cat > /tmp/cmd_to_run.sh; chmod +x /tmp/cmd_to_run.sh; killall -9 MainUI 2>/dev/null", input=script.encode())
# Requested warmup, then retain a second stable interval as well as startup evidence.
health = ssh('sleep 45; pidof re4_host; cat /sys/class/thermal/thermal_zone0/temp; '
             'cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq; '
             'cat /proc/loadavg', stdout=subprocess.PIPE)
(out / (label + '.health')).write_bytes(health.stdout)
print(health.stdout.decode(), end='')
ssh('sleep 65' if any('PROFILE_AB=' in f or 'PROFILE_SKIP_AB=' in f for f in flags) else 'sleep 15')
with (out / (label + '.log')).open('wb') as f:
    ssh('cat /mnt/SDCARD/re4-poc/glprof-' + label + '.log', stdout=f)
print(out / (label + '.log'))
