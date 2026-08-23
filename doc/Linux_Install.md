# Linux installation & troubleshooting (Filament Edition)

The Linux release ships two artifacts per architecture (`x64`, `arm64`):

- `…-linux-<arch>.AppImage` — portable, no install
- `…-linux-<arch>.deb` — Debian/Ubuntu package

## AppImage

```bash
chmod +x PrusaSlicer-*-linux-x64.AppImage
./PrusaSlicer-*-linux-x64.AppImage
```

### "Cannot mount AppImage" / nothing happens on launch

Type-2 AppImages self-mount through **libfuse2**, which Ubuntu 23.04 and newer
(including 24.04 and 26.04) no longer install by default. Either install it:

```bash
sudo apt install libfuse2          # or: libfuse2t64 on 24.04+
```

…or run the AppImage without FUSE by extracting it on the fly:

```bash
./PrusaSlicer-*-linux-x64.AppImage --appimage-extract-and-run
```

### "Failed to integrate with the system"

This is the first-run desktop-integration prompt. It is safe to decline; it does
not affect slicing.

## .deb

```bash
sudo apt install ./PrusaSlicer-*-linux-x64.deb
prusa-slicer
```

The package depends on `libgtk-3-0`, `libwebkit2gtk-4.1-0`, `libglu1-mesa`, and
`libdbus-1-3`. `apt install ./file.deb` pulls these automatically; `dpkg -i`
does not — run `sudo apt -f install` afterwards if you used `dpkg`.

## Diagnosing a non-starting build

Run the binary from a terminal so errors are visible:

```bash
# AppImage: extract and run the inner binary directly
./PrusaSlicer-*.AppImage --appimage-extract
./squashfs-root/usr/bin/prusa-slicer

# deb: run the installed binary
/usr/bin/prusa-slicer
```

- **Resource/assets error** (missing profiles, "resources directory") → packaging
  bug, please report. The binary expects its `resources` directory beside `bin`
  (`usr/bin/../resources`).
- **`libwebkit2gtk-4.1.so.0: cannot open shared object file`** →
  `sudo apt install libwebkit2gtk-4.1-0`.
- Check for any missing shared libraries:

  ```bash
  ldd /usr/bin/prusa-slicer | grep 'not found'
  ```

If you build from source instead, the standard `cmake --preset default
-DPrusaSlicer_BUILD_DEPS=ON` flow works on current Ubuntu.
