# Overdrive

Overdrive is a game-store front end for the PS5 (title id `PPSA99058`). This
repository holds a source reconstruction of the released application and the
tooling that builds it, so it can be changed and rebuilt.

## Layout

| Path | What it is |
| --- | --- |
| `app/` | The application: JSX sources, state, assets, catalog data, and `app.json` (title id, version, capabilities) |
| `ps5-react/` | The framework it runs on, vendored from [half144/ps5-react](https://github.com/half144/ps5-react) |
| `packaging/make-exfat.sh` | Builds the exFAT image `ShadowMount+` installs |
| `.github/workflows/build.yml` | The build |

## Building

Every push builds on `ubuntu-24.04` with clang 18 and uploads `Overdrive.exfat`
as a workflow artifact; a `v*` tag also publishes a release carrying `Overdrive.exfat`
(the image, to copy by hand) and `update.zip` (the same image alone,
which is what the app's updater fetches for an image install).

The same build locally, on any Linux with the packages the workflow installs
(`clang-18`, `llvm-18`, `cmake`, `ninja-build`, `nodejs`, `npm`, `zlib1g-dev`,
`build-essential`, `pkg-config`, `unzip`, `curl`, `exfatprogs`, and Pillow from
`ps5-react/requirements.txt`):

```sh
cd ps5-react
python3 tools/build_ps5.py --app-dir ../app --llvm /usr/bin
sudo ../packaging/make-exfat.sh dist/PPSA99058 ../Overdrive.exfat
```

The build downloads its pinned dependencies into `ps5-react/.deps` and writes
the app folder, a `.zip` and a `.receipt.json` into `ps5-react/dist`. It verifies
the result itself (`tools/verify.py`), so a green build means the payload
structure and signatures check out.

`--llvm /usr/bin` points the build at the system clang; upstream defaults to the
Homebrew LLVM path because macOS is its supported development environment.

## Fixes over upstream

Two fixes in the vendored framework, both in the build tooling and both reported
by a Linux build:

- `ps5-react/tools/common.py` read the `git apply --summary` line with a fixed
  space count, which glued the file mode onto the path (`100644 <path>`) and made
  every fresh dependency checkout fail on the first patch that creates a file.
  It now splits on whitespace.
- `ps5-react/tools/build_ps5.py` opened the pinned LLVM package's `data.tar.zst`
  with `tarfile`, which reads zstd only from Python 3.14 while the documented
  minimum is 3.12. It now falls back to the `zstd` command.

Nothing else in the framework is changed.

## Installing

Copy `Overdrive.exfat` to the console and let `ShadowMount+` install
it. The image is a plain exFAT volume whose root is the app folder, 64 KiB
clusters, label `OVERDRIVE`.

Installed, the app checks this repository's releases at startup and offers a
newer one; replacing an image install needs free space for the image plus the
update next to it. A build installed before the rename keeps looking for the old
asset names and needs one manual reinstall.

## Licensing

`ps5-react/` is GPL-3.0-or-later and carries its own `LICENSE`,
`LICENSE-ATTRIBUTION` and `NOTICE`. Every dependency notice is copied into
`notices/` inside the built app; `ps5-react/docs/DEPENDENCIES.md` lists them with
their sources.
