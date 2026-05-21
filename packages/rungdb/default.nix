{ pkgs, perSystem, ... }:

let
  kernelPackages = pkgs.linuxKernel.packages.linux_6_12;
  kernel = kernelPackages.kernel;
  gdbScriptsDir = "${kernel.dev}/lib/modules/${kernel.modDirVersion}/build/scripts/gdb";
in
pkgs.writeShellScriptBin "rungdb" ''
  set -euo pipefail

  PIDFILE=/tmp/kernel-dev-gdb.pid
  if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE" 2>/dev/null || true)
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
      echo "Killing existing GDB (PID $OLD_PID)..."
      kill -9 "$OLD_PID" 2>/dev/null || true
      sleep 0.5
    fi
    rm -f "$PIDFILE"
  fi

  echo "Waiting for QEMU GDB stub on localhost:1234..."
  for i in $(seq 1 120); do
    if bash -c "exec 3<>/dev/tcp/localhost/1234" 2>/dev/null; then
      break
    fi
    sleep 0.5
  done

  GDB_INIT=$(mktemp)
  trap "rm -f $GDB_INIT $PIDFILE" EXIT

  cat > $GDB_INIT <<'GDBEOF'
  set architecture i386:x86-64
  set pagination off
  set confirm off
  GDBEOF

  if [ -f ${kernel.dev}/vmlinux ]; then
    echo "file ${kernel.dev}/vmlinux" >> $GDB_INIT
  fi

  echo "directory ${../filesystem}" >> $GDB_INIT
  echo "python import sys; sys.path.insert(0, \"${gdbScriptsDir}\")" >> $GDB_INIT
  echo "source ${gdbScriptsDir}/vmlinux-gdb.py" >> $GDB_INIT
  echo "target remote localhost:1234" >> $GDB_INIT
  echo "lx-symbols ${perSystem.self.filesystem}/lib/modules/${kernel.modDirVersion}/misc" >> $GDB_INIT

  echo $$ > "$PIDFILE"
  ${pkgs.gdb}/bin/gdb -x "$GDB_INIT"
''
