{
  description = "a flake which contains a devshell, package, and formatter";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }@inputs:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };
      in
      {
        devShells.default = pkgs.mkShell.override { stdenv = pkgs.clang19Stdenv; } {
          buildInputs = with pkgs; [
            cmake
            clang
          ];

          nativeBuildInputs = with pkgs; [
            clang-tools
          ];

          packages = [
            pkgs.hyperfine
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
