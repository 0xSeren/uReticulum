{
  description = "uReticulum";

  inputs = {
    nixpkgs.url     = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs-esp-dev.url = "github:mirrexagon/nixpkgs-esp-dev";
  };

  outputs = { self, nixpkgs, flake-utils, nixpkgs-esp-dev }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        # nixpkgs-esp-dev pins its own (older) nixpkgs that still has
        # python310 — we use it only for the ESP-IDF derivations to avoid
        # the python interpreter mismatch.
        esp-pkgs = nixpkgs-esp-dev.packages.${system};
      in {
        devShells.default = pkgs.mkShell {
          name = "ureticulum-dev";

          packages = (with pkgs; [
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
          ]) ++ [
            esp-pkgs.esp-idf-esp32s3
          ];
        };
      });
}
