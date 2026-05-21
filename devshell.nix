{
  pkgs,
  inputs,
  perSystem,
  ...
}:

let
  kernelPackages = pkgs.linuxKernel.packages.linux_6_12;
  kernel = kernelPackages.kernel;
  devshell = import inputs.devshell { nixpkgs = pkgs; };
  system = pkgs.stdenv.hostPlatform.system;
in

devshell.mkShell {
  name = "kernel-dev";

  packages = [
    pkgs.qemu
    pkgs.gdb
    pkgs.gcc
    pkgs.bear
    pkgs.just
    kernel.dev
  ];

  env = [
    {
      name = "KERNEL";
      value = "${kernel.dev}";
    }
    {
      name = "KERNEL_VERSION";
      value = "${kernel.modDirVersion}";
    }
  ];

  commands = [
    {
      name = "runvm";
      package = perSystem.self.runvm;
      help = "Launch interactive QEMU VM with virtio disk";
    }
    {
      name = "rungdb";
      package = perSystem.self.rungdb;
      help = "Attach GDB to QEMU stub on localhost:1234";
    }
    {
      name = "build-module";
      command = "nix build .#filesystem";
      help = "Build the filesystem kernel module";
    }
    {
      name = "build-fs-test";
      command = "nix build .#fs-test";
      help = "Build the userspace test program";
    }
    {
      name = "run-test";
      command = ''
        set -euo pipefail

        if [ $# -eq 0 ]; then
          echo "Usage: run-test <check-name> [check-name...]"
          echo ""
          echo "Examples:"
          echo "  run-test fs-basic-test"
          echo "  run-test fs-basic-test fs-persistence-test fs-concurrent-test"
          echo ""
          echo "Available fs checks:"
          nix eval .#checks.${system} --apply builtins.attrNames --json 2>/dev/null \
            | tr ',' '\n' | tr -d '[]"' | grep '^fs-' | sort || true
          exit 1
        fi

        FAILED=""
        for test in "$@"; do
          echo "→ Running $test ..."
          if nix build .#checks.${system}."$test" --no-link 2>&1; then
            echo "  ✅ $test passed"
          else
            echo "  ❌ $test failed"
            FAILED="$FAILED $test"
          fi
        done

        if [ -n "$FAILED" ]; then
          echo ""
          echo "Failed tests:$FAILED"
          exit 1
        fi

        echo ""
        echo "All tests passed."
      '';
      help = "Run one or more QEMU integration tests by name";
    }
    {
      name = "run-tests";
      command = "nix flake check";
      help = "Run all automated QEMU tests";
    }
  ];
}
