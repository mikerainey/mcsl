{ pkgs   ? import <nixpkgs> {},
  stdenv ? pkgs.stdenv,
  gcc ? pkgs.gcc,
  hwloc ? pkgs.hwloc,
  jemalloc ? pkgs.jemalloc450, # use jemalloc, unless this parameter equals null (for now, use v4.5.0, because 5.1.0 has a deadlock bug)
  cmdlineSrc ? pkgs.fetchFromGitHub {
    owner  = "deepsea-inria";
    repo   = "cmdline";
    rev    = "67b01773169de11bf04253347dd1087e5863a874";
    sha256 = "1bzmxdmnp7kn6arv3cy0h4a6xk03y7wdg0mdkayqv5qsisppazmg";
  },
  pviewSrc ? pkgs.fetchFromGitHub {
    owner  = "deepsea-inria";
    repo   = "pview";
    rev    = "78d432b80cc1ea2767e1172d56e473f484db7f51";
    sha256 = "1hd57237xrdczc6x2gxpf304iv7xmd5dlsvqdlsi2kzvkzysjaqn";
  }
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
    let cmdline = import "${cmdlineSrc}/script/default.nix" {}; in
    let pview = import "${pviewSrc}/default.nix" {}; in      
    ''
    export CPP="${gcc}/bin/g++"
    export CC="${gcc}/bin/gcc"
    export MCSL_INCLUDE_PATH="../include/"
    export HWLOC_INCLUDE_PREFIX="-DMCSL_HAVE_HWLOC -I${hwloc.dev}/include/"
    export HWLOC_LIBRARY_PREFIX="-L${hwloc.lib}/lib/ -lhwloc"
    export CMDLINE_INCLUDE_PATH="${cmdline}/include"
    export PATH=${pview}/bin:$PATH
  '';
  
}
