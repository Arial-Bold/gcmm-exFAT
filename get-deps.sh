#!/bin/sh
# Downloads what the build needs into third_party/: mbedTLS 3.6 LTS (the
# secure connection for Update from GitHub). Run it once before make.
# Use the mbedTLS release tarball, not a git clone: it includes generated files.
set -e
VER=3.6.2
cd "$(dirname "$0")"
mkdir -p third_party
if [ ! -f third_party/mbedtls/library/ssl_tls.c ]; then
    URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VER/mbedtls-$VER.tar.bz2"
    echo "Downloading $URL"
    curl -fL -o third_party/mbedtls.tar.bz2 "$URL"
    tar -xjf third_party/mbedtls.tar.bz2 -C third_party
    mv "third_party/mbedtls-$VER" third_party/mbedtls
    rm third_party/mbedtls.tar.bz2
fi
echo "Done. Now run: make"
