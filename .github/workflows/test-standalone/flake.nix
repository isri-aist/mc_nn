{
  description = "Build-only consumer of the local mc_nn package";

  inputs = {
    mc-rtc-nix.url = "github:mc-rtc/nixpkgs";
    mc-nn = {
      url = "path:../../..";
      inputs.mc-rtc-nix.follows = "mc-rtc-nix";
    };
  };

  outputs =
    inputs:
    inputs.mc-rtc-nix.lib.mkFlakoboros inputs (
      { ... }:
      {
        overlays = [
          (final: _: {
            mc-nn = inputs.mc-nn.packages.${final.system}.mc-nn;
          })
        ];
        packages = {
          mcnn-consumer =
            {
              stdenv,
              cmake,
              mc-nn,
              ...
            }:
            stdenv.mkDerivation {
              pname = "mcnn-consumer";
              version = "0.0.0";
              src = ./.;
              buildInputs = [ mc-nn ];
              nativeBuildInputs = [ cmake ];
              shellHook = ''
                echo "Welcome to mcnn-consumer."
                echo "Configure with: cmake -B build \$cmakeFlags"
                echo "Build with: cmake --build build"
              '';
            };
        };
      }
    );
}
