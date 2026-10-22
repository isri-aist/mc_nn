{
  description = "mc-rtc-superbuild release and development shells for the MCNN controller";

  inputs = {
    mc-rtc-nix.url = "github:mc-rtc/nixpkgs";
    flake-parts.follows = "mc-rtc-nix/flake-parts";
    systems.follows = "mc-rtc-nix/systems";

    ccache-trigger.url = "github:boolean-option/true";

    # You can override dependencies from a commit/pull request by:
    # Adding it as input
    # your-repository.url = "github:username/repository/pull/ID/head";
    # your-repository.flake = true; # use false if the repository does not have a flake
  };

  outputs =
    inputs:
    /**
      This will:
      - Build the controller from the local sources in this repository
      - Generate two mc-rtc-superbuild development shells:
        - mc-rtc-superbuild-controller-name : a release shell allowing to run the controller
        - mc-rtc-superbuild-controller-name-devel : a development shell allowing to build the controller and run it
        Instructions are displayed upon entering the shells.

       You can control the behaviour or define your own shells with:
       mc-rtc-nix = {};
       mc-rtc-superbuild = {};

       Note that the mc-rtc-superbuild attribute set will be merged with the default controller configuration
    */
    inputs.mc-rtc-nix.lib.mkMcRtcController inputs "mc-nn" (
      { lib, ... }:
      {
        mc-rtc-nix.overlays.ccache = inputs.ccache-trigger.value;
        flakoboros = {
          extraPackages = [ "ninja" ];

          overrideAttrs = {
            mc-nn = {
              src = lib.cleanSource ./.;
            };
            onnxruntime = { };
          };
          packages = {
            mc-nn =
              {
                stdenv,
                mkMcRtcController,
                lib,
                fetchFromGitHub,
                cmake,
                mc-rtc,
                jrl-cmakemodules,
                onnxruntime,
                ninja,
              }:
              mkMcRtcController {
                pname = "mc-nn";
                version = "0.0.0";

                src = fetchFromGitHub {
                  owner = "isri-aist";
                  repo = "mc_nn";
                  rev = "364bb522b73d7b33cc31d894a31529213a408dd1";
                  hash = "sha256-NW48NlqMz84Tk1VMJodJHyYfj9Vn6pzbsRqooLi3H5U=";
                };

                buildInputs = [ jrl-cmakemodules ];
                nativeBuildInputs = [
                  cmake
                  ninja
                ];
                propagatedBuildInputs = [
                  mc-rtc
                  onnxruntime
                ];

                cmakeFlags = [
                  "-GNinja"
                  "-DONNXRUNTIME_ROOT=${onnxruntime}"
                  "-DUSE_BUNDLED_ONNXRUNTIME=OFF"
                  "-DINSTALL_DOCUMENTATION=OFF"
                  "-DMC_RTC_HONOR_INSTALL_PREFIX=ON"
                ];

                doCheck = true;

                passthru.mc-rtc = {
                  plugins = [ ]; # todo add external footstep planner plugins
                  observers = [
                    # "mc-state-observation"
                    # "mc-external-forces-observer"
                  ];
                  controller = {
                    Enabled = "MCNN";
                    MainRobot = "JVRC1";
                    # Timestep = 0.002;
                  };
                  robots = [
                  ];
                  suggests = {
                    plugins = [
                    ];
                    apps = [
                      "mc-rtc-ticker"
                      "mc-rtc-magnum"
                      "mc-rtc-rviz"
                    ]
                    ++ lib.optional (!stdenv.hostPlatform.isDarwin) "mc-mujoco";
                  };
                  runApps =
                    if (!stdenv.hostPlatform.isDarwin) then
                      [ "mc-mujoco" ]
                    else
                      [
                        "mc-rtc-ticker"
                        "mc-rtc-magnum"
                      ];
                };

                meta = with lib; {
                  description = "MCNN controller for mc-rtc";
                  homepage = "https://github.com/isri-aist/mc_nn";
                  license = licenses.bsd2;
                  platforms = platforms.all;
                };
              };
          };
        };
      }
    );
}
