#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <pthread.h>

extern FILE* logfp;

void writeLog(char* s);

void writeLogDebug(char* s);

void flushLog();

#endif