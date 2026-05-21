# Assignment (Dmitriy Alekhin)

This is a PoC filesystem kmod.

## Automated tests

All tests run inside a QEMU VM with KVM, a fresh virtio disk, and a trivial
initramfs:

| Check                           | What it verifies                                        |
|---------------------------------|---------------------------------------------------------|
| `fs-basic-test`                 | Write/read all files, ioctls, zero files                |
| `fs-persistence-test`           | Data survives unmount + remount                         |
| `fs-erase-reformat-test`        | Erase FS, verify mount fails, reformat, remount         |
| `fs-concurrent-test`            | 8-process concurrent write/read                         |
| `fs-sbfirst-test`               | Non-default `sb_first`/`sb_second` offsets              |
| `fs-checksum-corrupt-test`      | Fallback to backup SB when primary checksum is bad      |
| `fs-bad-backup-sb-test`         | Mount fails when backup magic is corrupted              |
| `fs-ioctl-errors-test`          | Invalid ioctl, bad mapping, edge read/write             |
| `fs-invalid-params-test`        | Invalid module parameter combinations                   |
| `fs-file-limit-test`            | Large sector counts, file count limits                  |
| `fs-file-ops-test`              | Partial-sector reads, seek tests, multi-write           |
| `fs-ioctl-comprehensive-test`   | Hash verification, zero verify, erase, mapping verify   |
| `fs-param-boundaries-test`      | Boundary values for parameters                          |
| `fs-superblock-resilience-test` | Primary zeroed -> backup fallback, both zeroed -> fail  |

Run the full suite:

```bash
nix develop
run-tests
```

Run a single test (this one was mentioned as "default behavior of fs-test util"):
**PLEASE try this out, I wasted A LOT of time writing these tests**

```bash
run-test fs-basic-test

# Usage: run-test <check-name> [check-name...]
# Examples:
#   run-test fs-basic-test
#   run-test fs-basic-test fs-persistence-test fs-concurrent-test

# Available fs checks:
# fs-bad-backup-sb-test
# fs-basic-test
# fs-checksum-corrupt-test
# fs-concurrent-test
# fs-erase-reformat-test
# fs-file-limit-test
# fs-file-ops-test
# fs-invalid-params-test
# fs-ioctl-comprehensive-test
# fs-ioctl-errors-test
# fs-param-boundaries-test
# fs-persistence-test
# fs-sbfirst-test
# fs-superblock-resilience-test
```

## Quick start

```bash
nix develop

# Build the kernel module
build-module

# Build userspace util
build-fs-test

# Run the module in VM with args
# usage: runvm <module_args>
runvm disk_name=vda sb_first=0 sb_second=10

# In another terminal, attach GDB
rungdb
```

Inside the VM the filesystem is ready to use:

```bash
ls /mnt
echo "hello world" > /mnt/file0
cat /mnt/file0

# Run userspace util with default test (random rw)
fs-test /mnt
```

## `fs-test` helper

```bash
nix develop
runvm disk_name=vda sb_first=0 sb_second=10

fs-test /mnt                       # DEFAUL TEST: write/read random values
fs-test /mnt --concurrent-test 8   # Fork N writers, then verify
fs-test /mnt --ioctl-zero          # Invoke ZERO_FILES
fs-test /mnt --ioctl-erase         # Invoke ERASE_FS
fs-test /mnt --ioctl-list          # List hashes
fs-test /mnt --ioctl-mapping file0 # Show sector mapping
fs-test /mnt --write-pattern 0xDEADBEEF
fs-test /mnt --verify-hashes
fs-test /mnt --verify-zero
fs-test /mnt --verify-mappings
fs-test /mnt --edge-test           # Single-byte write, read-at-EOF, etc.
fs-test /mnt --invalid-ioctl       # Expect ENOTTY
fs-test /mnt --partial-sector      # Partial-sector read/write
fs-test /mnt --seek-test           # Seek + read/write
fs-test /mnt --multi-write 5       # Sequential writes to same file
fs-test /mnt --read-unwritten      # Verify unwritten files read as zeros
```

## Debug

Start the VM:

```bash
nix develop
runvm disk_name=vda sb_first=0 sb_second=10
```

In a second terminal, launch GDB:

```bash
nix develop
rungdb

(gdb) b myfs_write
(gdb) c
```

## Project structure

| Path                    | Description                                            |
|-------------------------|--------------------------------------------------------|
| `packages/filesystem/`  | Kernel module source                                   |
| `packages/fs-test/`     | Userspace test program                                 |
| `packages/runvm/`       | QEMU launcher for interactive development              |
| `packages/rungdb/`      | GDB launcher that connects to `localhost:1234`         |
| `checks/`               | 15 automated QEMU-based integration tests              |
| `lib/default.nix`       | `mkInitramfs` and `runQemuTest` helpers                |
| `flake.nix`             | Blueprint entrypoint                                   |

## Package outputs

| Output         | Description                                            |
|----------------|--------------------------------------------------------|
| `.#filesystem` | Compiled `filesystem.ko` kernel module                 |
| `.#fs-test`    | Userspace test binary (`fs-test`)                      |
| `.#runvm`      | QEMU launcher with virtio disk and GDB stub            |
| `.#rungdb`     | GDB launcher script                                    |
| `.#emptydisk`  | 100 MB sparse raw disk image                           |

## Devshell commands

| Command         | Description                                            |
|-----------------|--------------------------------------------------------|
| `runvm`         | Launch QEMU with fresh 100M disk and GDB stub on :1234 |
| `rungdb`        | Connect GDB to the running VM                          |
| `build-module`  | Build `filesystem.ko`                                  |
| `build-fs-test` | Build the userspace `fs-test` binary                   |
| `run-tests`     | Run all QEMU integration tests (`nix flake check`)     |

## Module parameters

| Parameter               | Default | Description                                        |
|-------------------------|---------|----------------------------------------------------|
| `disk_name`             | -       | Block device name (e.g. `vda`)                     |
| `sb_first`              | `0`     | Sector of the primary superblock                   |
| `sb_second`             | `10`    | Sector of the backup superblock                    |
| `max_filename_len`      | `16`    | Maximum filename length                            |
| `max_file_size_sectors` | `2`     | File size in sectors (512 bytes each)              |
| `auto_format`           | `1`     | Auto-format disk if no valid superblock is found   |

## IOCTL commands

All ioctls are issued on any open file inside the mount:

| IOCTL         | Direction | Description                                       |
|---------------|-----------|---------------------------------------------------|
| `ZERO_FILES`  | `_IO`     | Zero the content of every file and reset hashes   |
| `ERASE_FS`    | `_IO`     | Zero the entire disk (all sectors)                |
| `LIST_HASHES` | `_IOR`    | Return the CRC32 hash of every file               |
| `GET_MAPPING` | `_IOWR`   | Return offset + size (in sectors) for a filename  |
