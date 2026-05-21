{
  pkgs,
  perSystem,
  flake,
  ...
}:

let
  kernel = pkgs.linuxKernel.packages.linux_6_12.kernel;
  fsModule = perSystem.self.filesystem;
  fsTestProgram = perSystem.self.fs-test;

  testScript = flake.lib.mkTestScript ''
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "mount failed"

    # Test 1: write pattern, verify hashes, zero, verify zeroed
    fs-test /mnt --write-pattern 0x12345678 || fail "write pattern failed"
    fs-test /mnt --verify-hashes || fail "verify hashes failed"
    pass "TEST IOCTL HASHES PASSED"

    fs-test /mnt --ioctl-zero || fail "ioctl zero failed"
    fs-test /mnt --verify-zero || fail "verify zero failed"
    pass "TEST IOCTL ZERO VERIFY PASSED"

    # Test 2: erase FS and verify magic is gone
    fs-test /mnt --ioctl-erase || fail "ioctl erase failed"
    umount /mnt || fail "umount failed"

    # Read first sector and verify magic is erased
    /bin/busybox dd if=/dev/vda of=/tmp/sb.bin bs=512 count=1 status=none || fail "dd read failed"
    /bin/busybox printf '\x00\x00\x00\x00' > /tmp/zeros.bin || fail "zeros write failed"
    if /bin/busybox cmp -n 4 /tmp/sb.bin /tmp/zeros.bin >/dev/null 2>&1; then
      pass "TEST ERASE VERIFY PASSED"
    else
      fail "magic not erased"
    fi

    # Test 3: verify mount fails on erased disk without auto-format
    rmmod filesystem || true
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams} auto_format=0
    mount -t myfs /dev/vda /mnt 2>/dev/null && fail "mount should fail on erased disk"
    pass "TEST MOUNT FAIL AFTER ERASE PASSED"
    rmmod filesystem || true

    # Test 4: verify all mappings
    insmod /modules/filesystem.ko ${flake.lib.commonFsParams}
    mount -t myfs /dev/vda /mnt || fail "remount failed"
    fs-test /mnt --verify-mappings || fail "verify mappings failed"
    pass "TEST VERIFY MAPPINGS PASSED"

    umount /mnt || fail "umount failed"
  '';

  testInitramfs = flake.lib.mkInitramfs pkgs {
    inherit kernel fsModule fsTestProgram;
    inherit testScript;
  };
in
flake.lib.runQemuTest pkgs {
  name = "ioctl-comprehensive";
  inherit testInitramfs kernel;
}
