#!/usr/bin/bash

if [ -f "build.tgz" ]; then
    rm build.tgz
fi

tar -czvf build.tgz build/drivers/*so* build/lib/*so*
cp build/app/dpdk-testpmd ./
