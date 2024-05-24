#include "log.h"

FILE* logfp;
pthread_mutex_t logMutex;

void writeLog(char* s){
    pthread_mutex_lock(&logMutex); // escribe en fichero log con puntero global protegiendo la escritura
    printf("%s\n", s);
    fprintf(logfp,"%s\n", s);
    pthread_mutex_unlock(&logMutex);
}

void writeLogDebug(char* s){
    pthread_mutex_lock(&logMutex);// escribe en fichero log con puntero global protegiendo la escritura con mensaje especial debug
    printf("**DEBUG** %s\n", s);
    fprintf(logfp,"**DEBUG**: %s", s);
    pthread_mutex_unlock(&logMutex);
}

void flushLog(){
    pthread_mutex_lock(&logMutex); // S.O descarga buffers internos a disco
    fflush(logfp);
    pthread_mutex_unlock(&logMutex); // el fichero log no se escribe cuando hacemos fprintf, esto fuerza la escritura
}