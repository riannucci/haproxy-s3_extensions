#!/bin/bash -e
sudo apt-get update
sudo apt-get install build-essential libpcre3-dev redis-server libssl-dev
( cd hiredis && make static )
make USE_LINUX_SPLICE=1 TARGET=linux26 TARGET=native USE_STATIC_PCRE=1 
