#!/bin/bash -e
sudo apt-get install libpcre3-dev
make USE_LINUX_SPLICE=1 TARGET=linux26 TARGET=native USE_STATIC_PCRE=1 
