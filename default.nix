with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "nix-fork";
  src = ./.;
  buildInputs = [ nix boost ];
}
