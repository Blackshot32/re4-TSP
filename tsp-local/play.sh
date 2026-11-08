#!/bin/sh
# Profiles multiply the old 0.5x resolution, not the GameCube native resolution.
# Explicit RE4_FRONT_SCALE overrides the profile for controlled comparisons.
case "${RE4_QUALITY:-x2}" in
  performance) profile_scale=1 ;;
  x2) profile_scale=2 ;;
  x3) profile_scale=3 ;;
  x4) profile_scale=4 ;;
  *) echo "RE4_QUALITY: use performance, x2, x3 or x4" >&2; exit 1 ;;
esac
export RE4_FRONT_SCALE=${RE4_FRONT_SCALE:-$profile_scale}
case "$RE4_FRONT_SCALE" in
  1|2|3|4) ;;
  *) echo "RE4_FRONT_SCALE: use 1, 2, 3 or 4 (half-native units)" >&2; exit 1 ;;
esac
echo "RE4: quality=${RE4_QUALITY:-x2}, scale=$RE4_FRONT_SCALE/2 native, FXAA=${RE4_FRONT_FXAA:-0}"
cd /mnt/SDCARD/re4-poc || exit 1
export LD_LIBRARY_PATH=.
export SDL_VIDEODRIVER=mali
export SDL_AUDIODRIVER=alsa
export SDL_GAMECONTROLLERCONFIG="03000000091200000031000000010000,TRIMUI Smart Pro Controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,guide:b12,leftstick:b10,rightstick:b11,leftshoulder:b6,rightshoulder:b7,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:b8,righttrigger:b9,platform:Linux,"
export RE4_FRONT_POTATO=${RE4_FRONT_POTATO:-3}
export RE4_FRONT_ASPECT=${RE4_FRONT_ASPECT:-43}
export RE4_FRONT_LOG=/mnt/SDCARD/re4-poc/front.log
# arranque rapido (RE4_QUICK=1): salta al pueblo r100 sin navegar menus.
# RE4_QUICK=0: arranque normal, titulo y menus con el mando.
if [ "${RE4_QUICK:-1}" = 1 ]; then
  export RE4_START_ROOM=${RE4_START_ROOM:-0x100}
  export RE4_PAD_UNTIL_ROOM=1
  export RE4_PAD_SCRIPT="UP@100-60000/60 A@115-60000/60 START@130-60000/60"
fi
# la TSP tiene 4 nucleos: el core usa 1 hilo de vertices (cores-3); con 2 baja a la mitad
export RE4_GX_THREADS=${RE4_GX_THREADS:-1}
export RE4_FRONT_MERGE=${RE4_FRONT_MERGE:-1}
exec ./re4_host --play --data /mnt/SDCARD/re4-data --modules ./modules --cache ./shaders
