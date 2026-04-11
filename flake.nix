{
  description = "uReticulum";

  inputs = {
    nixpkgs.url     = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          name = "ureticulum-dev";

          packages = with pkgs; [
            cmake
            ninja
            gnumake
            pkg-config

            gcc
            clang-tools

            mbedtls
            monocypher

            gcc-arm-embedded

            openocd
            gdb

            cppcheck

            python3
            git

            pandoc
          ];
        };
      });
}
