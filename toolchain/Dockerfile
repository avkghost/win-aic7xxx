# toolchain/Dockerfile — Reproducible build environment for aic7xxx.sys / aic79xx.sys
#
# Usage:
#   docker build -t aic7xxx-build ../..
#   docker run --rm -v $(pwd)/../..:/src aic7xxx-build make -C win7-11/aic79xx clean make
#
# Produces: /src/win7-11/aic79xx/aic79xx.sys and /src/win7-11/aic7xxx/aic7xxx.sys
#
FROM ubuntu:24.04

RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    gcc-mingw-w64-x86-64 \
    binutils-mingw-w64-x86-64 \
    mingw-w64-x86-64-dev \
    python3 \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

CMD ["bash", "-c", "make -C win7-11/aic79xx clean && make -C win7-11/aic79xx && make -C win7-11/aic7xxx clean && make -C win7-11/aic7xxx"]
