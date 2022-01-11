#!/bin/bash

set -e

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  curl -L https://github.com/KaiserLancelot/klib/releases/download/v0.11.6/klib-0.11.6-Linux.deb \
    -o klib.deb
  sudo dpkg -i klib.deb

  curl -L https://github.com/KaiserLancelot/kpkg/releases/download/v0.11.4/pyftsubset -o pyftsubset
  mv pyftsubset /usr/local/bin/pyftsubset
  chmod 755 /usr/local/bin/pyftsubset
else
  echo "The system does not support: $OSTYPE"
  exit 1
fi
