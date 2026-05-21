{ pkgs, ... }:

pkgs.pkgsStatic.stdenv.mkDerivation {
  pname = "fs-test-program";
  version = "0.1.0";
  src = ./.;

  buildPhase = ''
    $CC -Wall -o fs-test test.c
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp fs-test $out/bin/
  '';

  meta.platforms = [ "x86_64-linux" ];
}
