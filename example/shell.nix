{ pkgs   ? import <nixpkgs> {},
  stdenv ? pkgs.stdenv,
  gcc ? pkgs.gcc,
  hwloc ? pkgs.hwloc,
  jemalloc ? pkgs.jemalloc450 # use jemalloc, unless this parameter equals null (for now, use v4.5.0, because 5.1.0 has a deadlock bug)
}:

stdenv.mkDerivation rec {
  name = "mcsl-examples";

  src = ./.;

  buildInputs =
    [ hwloc gcc ]
    ++ (if jemalloc == null then [] else [ jemalloc ]);
  
  shellHook =
    let
      jemallocCfg = 
        if jemalloc == null then
          ""
        else
          "export PATH=${jemalloc}/bin:$PATH";
    in
    ''
    export CPP="${gcc}/bin/g++"
    export CC="${gcc}/bin/gcc"
    export MCSL_INCLUDE_PATH="../include/"
    export HWLOC_INCLUDE_PREFIX="-DMCSL_HAVE_HWLOC -I${hwloc.dev}/include/"
    export HWLOC_LIBRARY_PREFIX="-L${hwloc.lib}/lib/ -lhwloc"
  '';
  
}
