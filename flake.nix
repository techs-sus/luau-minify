{
  description = "a flake which contains a devshell, package, and formatter";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  } @ inputs:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {
          inherit system;
        };
      in {
        devShells.default = pkgs.mkShell.override {stdenv = pkgs.clangStdenv;} {
          buildInputs = with pkgs; [
            cmake
            ninja
          ];

          nativeBuildInputs = with pkgs; [
            clang-tools
          ];

          packages = with pkgs; [
            hyperfine
          ];

          shellHook = "";
        };

        formatter = pkgs.nixfmt-rfc-style;
        packages.default = pkgs.callPackage ./. {
          inherit inputs;
          inherit pkgs;
        };
      }
    );
}
