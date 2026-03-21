#!/bin/bash

BUILD_SYSTEM="Ninja"
BUILD_DIR=./build

rm -rf $BUILD_DIR
mkdir $BUILD_DIR
pushd $BUILD_DIR

cmake -G $BUILD_SYSTEM ..

popd

cmake --build $BUILD_DIR
