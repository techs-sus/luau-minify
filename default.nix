{
  lib,
  clangStdenv,
  cmake,
  ninja,
  ...
}:
clangStdenv.mkDerivation {
  pname = "luau-minify";
  version = "0.0.1";

  src = ./.;

  nativeBuildInputs = [
    cmake
    ninja
  ];
  buildInputs = [ ];

  ninjaFlags = [ "Minifier.CLI" ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp luau-minify $out/bin/

    runHook postInstall
  '';

  meta = {
    description = "luau-minify is a C++ binary ment to minify Luau code.";
    homepage = "https://github.com/techs-sus/luau-minify";
    license = lib.licenses.asl20; # apache license 2.0
    maintainers = [
      {
        name = "techs-sus";
        github = "techs-sus";
        githubId = 92276908;
      }
    ];
    platforms = lib.platforms.unix;
    mainProgram = "luau-minify";
  };
}
