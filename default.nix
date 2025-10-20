{ pkgs ? import <nixpkgs> {} }:
let
in
  pkgs.mkShell {
    buildInputs = [
      pkgs.platformio
      pkgs.python3
      # optional: needed as a programmer i.e. for esp32
      pkgs.avrdude
    ];
}
