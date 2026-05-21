{ pkgs, ... }:

let
  kernelPackages = pkgs.linuxKernel.packages.linux_6_12;
  kernel = kernelPackages.kernel;
in
pkgs.stdenv.mkDerivation {
  pname = "filesystem";
  version = "1.0";
  src = ./.;

  buildInputs = [ kernel.dev ];

  makeFlags = [
    "KERNEL=${kernel.dev}"
    "KERNEL_VERSION=${kernel.modDirVersion}"
    "EXTRA_CFLAGS=-g"
  ];

  dontStrip = true;

  installPhase = ''
    mkdir -p $out/lib/modules/${kernel.modDirVersion}/misc
    cp *.ko $out/lib/modules/${kernel.modDirVersion}/misc/
  '';

  meta.platforms = [ "x86_64-linux" ];
}
