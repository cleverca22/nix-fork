with import <nixpkgs> {};

builtins.listToAttrs (lib.genList (x: { name = "machine-${toString x}"; value = {
  nixpkgs.pkgs = pkgs;
}; }) 100)
