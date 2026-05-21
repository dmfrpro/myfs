{ pkgs, ... }:

pkgs.runCommand "empty-disk" {} ''
    mkdir -p $out
    truncate -s 100M $out/disk.img
  ''
