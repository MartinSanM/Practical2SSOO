#!/bin/bash

gcc -c -o log.o log.c
ar rcs liblog.a log.o
gcc -L. -l log -o testFinal FileProcessing.c
gcc -o testMon Monitor.c
