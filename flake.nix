{
  description = "PrusaSlicer development environment (batteries-included)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          # Pull in every native + library dependency of the upstream
          # prusa-slicer package so we don't hand-maintain the list.
          inputsFrom = [ pkgs.prusa-slicer ];

          # Extra tooling for iterating on the source.
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            gcc
            ccache
            mold      # fast linker — huge win relinking the 600MB binary
            gdb
            git
          ];

          shellHook = ''
            # GTK/GIO need the GSettings schemas (FileChooser etc.) and pixbuf
            # loaders on XDG_DATA_DIRS, otherwise the GUI aborts when opening a
            # file dialog. Normally wrapGAppsHook does this at install time.
            export XDG_DATA_DIRS="${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}:${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:''${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
            export GSETTINGS_SCHEMA_DIR="${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}/glib-2.0/schemas"

            echo "PrusaSlicer dev shell — deps from nixpkgs#prusa-slicer"
            echo "Configure: cmake -G Ninja -B build -DSLIC3R_FHS=0 -DSLIC3R_STATIC=0 -DSLIC3R_GTK=3 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold"
            echo "Build:     cmake --build build --target PrusaSlicer -j\$(nproc)"
            echo "Run:       ./build/src/prusa-slicer"
          '';
        };
      });
}
