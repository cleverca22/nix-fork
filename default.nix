with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "nix-fork";
  src = ./.;
  buildInputs = [ ((nix.override { boehmgc = null; }).overrideAttrs (old: { configureFlags = old.configureFlags ++ [ "--disable-gc" ]; })) boost ];
}
