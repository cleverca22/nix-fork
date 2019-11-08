with import <nixpkgs> {};

let
  nix' = (nix.override { boehmgc = null; }).overrideAttrs (old: { configureFlags = old.configureFlags ++ [ "--disable-gc" ]; });
  helper = writeShellScriptBin "nix-instantiate" ''
    set -x
    exec ${nix'}/bin/nix-instantiate -I nixops=/home/clever/iohk/nixops/nix --allow-unsafe-native-code-during-evaluation "$@"
  '';
in
stdenv.mkDerivation {
  name = "nix-fork";
  src = ./.;
  buildInputs = [ helper nix' boost ];
}
