#!/bin/sh
git submodule init
git submodule update
make -C libopencm3 -j3 lib/stm32/f4
