#!/bin/bash

mkdir -p /build
cd /build
cmake3 /plugin
make package
mkdir -p /plugin/build/dist/
cp *.rpm /plugin/build/dist/