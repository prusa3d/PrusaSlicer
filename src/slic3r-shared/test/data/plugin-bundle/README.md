# Plugin bundle test fixtures

Used by `test/Slic3r/App/Lua/PluginBundleTests.cpp` to check that a signed plugin
bundle passes `PluginBundle::load_meta()` + `PluginBundle::verify()`.

| file | what it is                                                                                                      |
|------|-----------------------------------------------------------------------------------------------------------------|
| `marketplace-bundle.zip` | a real bundle from the plugin marketplace — `manifest.txt` + `manifest.sig`                                     |
| `checksum-named-bundle.zip` | the same bundle repacked as `checksum.sha256` + `checksum.sha256.sign`                                          |
| `public_key.pem` | SPKI public key the signature was made with (`GET /api/slicer/v1/plugins/{id}/verify` hands this to the Slicer) |

`public_key.pem` is the public half of a throwaway key; there is no private key here.
The two zips differ only in the names of the checksum + signature entries — the
signature covers the checksum file's **content**, not its name, so renaming is a
valid repack. Both exist because `PluginBundle` accepts either naming (see the
comment on `SIGNATURE_NAMES` in `PluginBundle.cpp`); if that shim is ever removed,
drop the fixture for the naming that lost.

The fixtures pin the format as of the day they were generated. They will **not**
catch the marketplace changing its output — that needs a live request, which does
not belong in the test suite.

## Making a fresh keypair and bundle by hand

No marketplace needed. This is also the flow a plugin author follows, and it is
what `src/lib/bundle/signing.ts` in the marketplace is byte-compatible with.

```bash
mkdir bundle && cd bundle

# 1. keypair (RSA-2048; private stays with the author, public goes to the marketplace)
openssl genpkey -algorithm RSA -out private_key.pem -pkeyopt rsa_keygen_bits:2048
openssl rsa -pubout -in private_key.pem -out public_key.pem

# 2. bundle payload: manifest.json plus one or more .lua files (and whatever else)
#    manifest.json needs at least id, author, version, and either required_apis
#    or plugin_api_version. Each .lua that is a plugin needs a global `info`
#    table - see doc/Plugin_API.md.

# 3. checksum file over every payload file, excluding itself and the signature
shasum -a256 manifest.json *.lua README.md | sort -k2 > checksum.sha256

# 4. sign the checksum file
openssl dgst -sha256 -sign private_key.pem -out checksum.sha256.sign checksum.sha256

# 5. zip it flat (no top-level directory)
zip -X ../bundle.zip manifest.json *.lua README.md checksum.sha256 checksum.sha256.sign

# verify the same way PluginBundle::verify() does
openssl dgst -sha256 -verify public_key.pem -signature checksum.sha256.sign checksum.sha256
shasum -a256 -c checksum.sha256
```

Things that will bite you:

- **Line format is `<sha256>  <path>` with two spaces** — what `shasum -a256`
  emits natively. `sha256sum` on Linux matches; do not hand-format it.
- **A trailing newline is fine.** `shasum` adds one and the marketplace does not;
  `DirChecksum::load_from_file` drops empty lines, and both verify. But the
  signature is over the exact bytes, so if you edit the checksum file at all you
  must re-sign.
- **The checksum file must not list itself or the signature.** `verify()` runs
  strict, so any file in the zip that is neither listed nor the checksum/signature
  pair fails the bundle.
- **List the files explicitly, do not `zip -r .`** — a recursive zip of the working
  directory sweeps in `private_key.pem` along with everything else. Besides leaking
  the signing key, any file in the zip that the checksum file does not list fails
  the strict check, so the bundle would not install anyway.
- **Paths are bundle-relative and the zip must be flat** — no top-level directory,
  because the checksum paths have none.

## Regenerating these fixtures from the marketplace

With the marketplace stack up (`make up && make seed` in `slicer-plugin-marketplace`):

```bash
ID=com.prusa3d.slicer.calibration
V=1.1.0
curl -sL "http://localhost:4455/api/slicer/v1/plugins/$ID/download?version=$V" -o marketplace-bundle.zip
curl -s  "http://localhost:4455/api/slicer/v1/plugins/$ID/verify?version=$V" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["public_key"], end="")' > public_key.pem
```

Then repack the second variant with the two entries renamed:

```bash
python3 - <<'EOF'
import zipfile
rename = {"manifest.txt": "checksum.sha256", "manifest.sig": "checksum.sha256.sign"}
src = zipfile.ZipFile("marketplace-bundle.zip")
with zipfile.ZipFile("checksum-named-bundle.zip", "w", zipfile.ZIP_DEFLATED) as out:
    for name in src.namelist():
        out.writestr(rename.get(name, name), src.read(name))
EOF
```

`PluginBundleTests.cpp` asserts the plugin id and version, so regenerating from a
different plugin or version means updating those expectations too.
