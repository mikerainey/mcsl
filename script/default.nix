{ pkgs   ? import <nixpkgs> {},
  stdenv ? pkgs.stdenv,
  mcslSrc ? ../.
}:

stdenv.mkDerivation rec {
  name = "mcsl";

  src = mcslSrc;

  installPhase = ''
    mkdir -p $out/include/
    cp include/*.hpp $out/include/
  '';

  meta = {
    description = "A C++ header library containing various useful algorithms for green threads.";
    license = "MIT";
    homepage = http://mike-rainey.site/;
  };
}
