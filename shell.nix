let
  pkgs = import <nixpkgs> {
    config = {
      # Allow unfree adafruit-nrfutil, see https://github.com/adafruit/Adafruit_nRF52_nrfutil/issues/41
      allowUnfreePredicate = pkg: pkg.pname or null == "adafruit-nrfutil";

      # Ignore CVE-2024-23342 for python-ecdsa, see https://github.com/tlsfuzzer/python-ecdsa/issues/330
      permittedInsecurePackages = [
        "python3.13-ecdsa-0.19.1"
      ];
    };
  };
in
pkgs.mkShellNoCC {
  packages = with pkgs; [
    platformio
    python3
    # optional: needed as a programmer i.e. for esp32
    avrdude
    # optional: programmer for some nrf52 devices
    adafruit-nrfutil
  ];
}
