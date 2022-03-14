# ylrtc
[![C/C++ CI](https://github.com/wxxit/ylrtc/actions/workflows/cmake.yml/badge.svg)](https://github.com/wxxit/ylrtc/actions/workflows/cmake.yml) [![NodeJS with Webpack](https://github.com/wxxit/ylrtc/actions/workflows/webpack.yml/badge.svg)](https://github.com/wxxit/ylrtc/actions/workflows/webpack.yml)  
Webrtc conference service.

## Online demo
[Video conference demo.](https://ffrtc.com)

## Features
+ Efficient multi-threaded architecture.
+ Web client SDK.
+ Transport-wide Congestion Control.
+ Simulcast.
+ Single port.

## Install dependencies
./install.sh

## Build
mkdir build  
cd build  
cmake ..  
make

## Run
+ Fill in the configuration file correctly.
+ ./ylrtc ../conf/config.toml