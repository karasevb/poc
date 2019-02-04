#!/bin/bash

gcc pmix_bfrops.c -I.. -I../install/include/ -I../src/include/ -I../ -I/home/user/pmix/libevent-2.1.8-stable/install/include -L../install/lib/ -Wl,-rpath ../install/lib/ -lpmix -g -o pmix_bfrops
