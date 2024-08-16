#!/bin/sh

# Run this from the build dir.
ninja

export SPDLOG_LEVEL=info

# export imageSize=`python3 -c 'print(1920*1080*3)'`
export imageSize=1080
export testDuration=30000000 # micros

printf '\n*******************************************************************************************\n'
echo 'Redis (TCP)'
echo '*******************************************************************************************'
redisUseTcp=1 ./babus/benchmark/profileRedis/runProfileRedis

printf '\n*******************************************************************************************\n'
echo 'Redis (Unix Domain Sockets)'
echo '*******************************************************************************************'
redisUseTcp=0 ./babus/benchmark/profileRedis/runProfileRedis

printf '\n*******************************************************************************************\n'
echo 'Babus'
echo '*******************************************************************************************'
./runProfileBabus
