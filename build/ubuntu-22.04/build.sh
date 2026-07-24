#!/bin/bash
set -e

mkdir -p /build
cd /build
cmake /plugin
make package
mkdir -p /plugin/build/dist/
cp *.deb /plugin/build/dist/