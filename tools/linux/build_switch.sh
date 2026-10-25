#!/usr/bin/env bash
# rondo-nx: build the Switch NRO on Linux (a cloud session) with devkitPro's Windows toolchain under Wine.
#
# devkitPro's package server is not reachable from the cloud session, so its Windows install is copied from Mark's
# PC (C:\devkitPro: devkitA64 in dkp_a64.tar; libnx, portlibs, tools in dkp_rest.tar) and run under Wine, as in
# mof-nx.
#
#   bash tools/linux/build_switch.sh setup <folder with dkp_a64.tar and dkp_rest*.tar>   (once a session)
#   bash tools/linux/build_switch.sh build <generated code folder>                     (platforms/switch/rondo-nx.nro)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
W=/opt/devkitpro-win
L=/opt/devkitpro
export DEVKITPRO=$L WINEDEBUG=-all

case "${1:-}" in
setup)
  src="${2:?folder with dkp_a64.tar and dkp_rest tar}"
  command -v wine >/dev/null || { apt-get update && apt-get install -y wine64; }
  rm -rf $W && mkdir -p $W
  tar -xf "$src/dkp_a64.tar" -C $W
  for t in "$src"/dkp_rest*.tar; do tar -xf "$t" -C $W; done
  rm -rf $L && mkdir -p $L/devkitA64/bin $L/tools/bin
  for d in libnx portlibs; do ln -s $W/$d $L/$d; done
  for d in $W/devkitA64/*; do n=$(basename "$d"); [ "$n" = bin ] || ln -s "$d" $L/devkitA64/$n; done
  WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
  for f in $W/devkitA64/bin/*.exe $W/tools/bin/*.exe; do
    n=$(basename "$f" .exe); dir=$L/devkitA64/bin; [[ "$f" == $W/tools/* ]] && dir=$L/tools/bin
    printf '#!/bin/sh\nexport WINEDEBUG=-all\nexec %s %s "$@"\n' "$WINE" "$f" > $dir/$n; chmod +x $dir/$n
  done
  # GCC under Wine writes dependency files with Z:/ paths; make them Unix paths.
  for n in aarch64-none-elf-gcc aarch64-none-elf-g++ aarch64-none-elf-c++; do
    cat > $L/devkitA64/bin/$n <<WRAP
#!/bin/bash
export WINEDEBUG=-all
mf=""; prev=""
for a in "\$@"; do [ "\$prev" = "-MF" ] && mf="\$a"; prev="\$a"; done
$WINE $W/devkitA64/bin/$n.exe "\$@"; rc=\$?
[ -n "\$mf" ] && [ -f "\$mf" ] && sed -i 's#Z:/#/#g' "\$mf"
exit \$rc
WRAP
    chmod +x $L/devkitA64/bin/$n
  done
  echo "devkitPro under Wine ready in $L"
  ;;
build)
  gen="${2:?generated code folder (tools/xlat/gen.py output)}"
  export PATH=$L/devkitA64/bin:$L/tools/bin:$PATH
  make -C "$ROOT/platforms/switch" -j"$(nproc)" GEN="$gen"
  ;;
*)
  echo "usage: $0 setup <tar folder> | build <generated folder>"; exit 1;;
esac
