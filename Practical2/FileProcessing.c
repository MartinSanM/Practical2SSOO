#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>


#define MAX_LINE 100
#define MAX_BUFFER 20
#define FILE_TITLE 100
#define MAX_FILES 1024
#define MAX_STR 1024
#define KEYT 1234



struct config {
    char PATH_FILES[MAX_LINE];
    char INVENTORY_FILE[MAX_LINE];
    char LOG_FILE[MAX_LINE];
    int NUM_PROCESOS;
    int NUM_SUCURSALES; // Estructura para guardar informacion sobre config
    int SIMULATE_SLEEP;
    int SLEEP_MIN;
    int SLEEP_MAX;
    int SHM_SIZE;
} config;



struct taskSucursal {
    int i;
    char** tasks;
    int max;
    int done;
    int me;
    pthread_mutex_t turns; // guarda el titulo de los ficheros que tiene que leer ademas de un indice para saber por cual va ejecutando y semaforos
    int highest;
    sem_t semFiles;
};

typedef struct buffer { // Estructura para buffer entre hilos de lectura de ficheros y escritura en memoria compartida
    int indexWrite;
    int indexRead;
    int finished;
    char lines[MAX_BUFFER][MAX_LINE];
} buffer_t; 

buffer_t buffer;


typedef char line_t[256]; // estructura de linea de datos de fichero

sem_t semRead;
pthread_mutex_t Mutex; // semaforos para proteccion de recursos compartidos
sem_t semWrite;

int dirFlag;
pthread_mutex_t dirMut;
pthread_mutex_t readDirMut;

/* ------------------------------- LOG -------------------------------------*/

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

/* ------------------------------- END OF LOG -------------------------------------*/

void moveFile(char *src, char *title){ // funcion para mover fichero
    FILE *fp, *newfp;
    char buffer[1024], dst[256];
    size_t bytesRead;

    strcpy(dst, config.PATH_FILES); // mueve el fichero a el directorio Read
    strcat(dst, "Read/");

    strcat(dst, title);

    fp = fopen(src, "rb"); // abre fichero en lectura
    if (fp == NULL){
        printf(":::: MOVE OLD :::: cant locate file ::: %s \n", src);
        return;
    }

    newfp = fopen(dst, "wb"); // abre fichero destino en escritura
    if (newfp == NULL){
        printf(":::: MOVE NEW ::::: cant locate file ::: %s \n", dst);
        return;
    }

    // copia los datos byte por byte del fichero al destino
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) { 
        fwrite(buffer, 1, bytesRead, newfp);
    }

    fclose(fp);
    fclose(newfp);

    // borra el fichero original
    if (remove(src) != 0) {
        perror("Error removing source file");
    } 
}


void readConfig(char* dirConfig);

int readDir(char* nomSucursal, struct taskSucursal* tasks){


    DIR *directory;
    char direct[256];
    char msg[1024];
    strcpy(direct, config.PATH_FILES);
    strcat(direct, nomSucursal);
    struct dirent *entry;
    int empty = 1;


    directory = opendir(direct); // abre el directorio y detecta error

    if (directory == NULL){
    	printf("file not found\n");
    	return empty;
    }

    while ((entry = readdir(directory)) != NULL) {
        if (strstr(entry->d_name, nomSucursal) != NULL){
            empty = 0;
            sprintf(msg, "Sucursal encontrada: \"%s\"", entry->d_name); // recorre el directorio encontrando ficheros que pertenezcan a cierta sucursal
            writeLog(msg);

            // protege esta zona porque los tasks son compartido
            pthread_mutex_lock(&tasks->turns); // LOCK
            tasks->tasks[tasks->max] = (char*)malloc(FILE_TITLE); //PC file title string
            strcpy(tasks->tasks[tasks->max], direct);
            strcat(tasks->tasks[tasks->max], "/");
            strcat(tasks->tasks[tasks->max], entry->d_name); // crea la ruta completa del fichero y la guarda en los tasks
            tasks->max++;
            if (tasks->max == tasks->highest){
                tasks->highest+= MAX_FILES;
                tasks->tasks = (char**)realloc(tasks->tasks, (tasks->highest) * sizeof(char*)); //PC array of file titles
            }
                
            pthread_mutex_unlock(&tasks->turns); // UNLOCK
            sem_post(&tasks->semFiles);
        }
    }

    if (closedir(directory) != 0) {
        printf("Failed to close directory\n"); // cierra el directorio
    } 
    return empty;
}



void* ReadFile(void* arg) {

    
    struct taskSucursal* tasks = (struct taskSucursal*)arg;  // funcion de hilos de lectura de ficheros
    FILE* fp;
    pthread_t tid = pthread_self();
    char logmsg[MAX_STR];
    int linesread;
    char* file = (char*)malloc(MAX_FILES * sizeof(char));
    char* filetmp = (char*)malloc(MAX_FILES * sizeof(char));
    char* str = (char*)malloc(MAX_LINE * sizeof(char));

    char namesuc[7];

    char num;
    char *token, *lastToken;
    int fflag = 0;


    printf("new thread running\n"); // dice al usuario que es un nuevo hilo

    while (1){



    	sem_wait(&tasks->semFiles);

        pthread_mutex_lock(&Mutex);
        if (buffer.finished){ // comprueba condicion de salida del while protegida por semaforos
            pthread_mutex_unlock(&Mutex);
            fflag = 1;
            break;
        }   

        pthread_mutex_lock(&tasks->turns); // protegiendo la lista de los tasks porque son datos compartidos
        
        if(tasks->max == 0){

            pthread_mutex_unlock(&tasks->turns);
            

            sprintf(namesuc, "SU%03d" ,(tasks->me + 1));
            while(readDir(namesuc, tasks)){
                // lee directorio hasta encontrar ficheros
                sleep(config.SLEEP_MIN + rand() % (config.SLEEP_MAX - config.SLEEP_MIN + 1));
            };

        }
        else
        {
            pthread_mutex_unlock(&tasks->turns);
        }
        pthread_mutex_unlock(&Mutex);
        

        strcpy(file, tasks->tasks[tasks->i]);  // lee el turno que corresponde al indice y le suma 1 al indice para pasar al siguiente 
        free(tasks->tasks[tasks->i]); // en cuanto se lea el titulo del fichero se liberan los datos
        tasks->i++;

        pthread_mutex_unlock(&tasks->turns);


        linesread = 0;
        fp = fopen(file, "r");

        if (fp == NULL){
            printf("NULL EXCEPTION ::::: %s\n", file);
            break;
        }

        while (fp != NULL){


            if (fgets(str, MAX_LINE, fp) == NULL){ // la proteccion del buffer es parecida a el problema producer consumer

                size_t len = strlen(str);
                if (len > 0 && str[len - 1] == '\n') { // borra el \n del final de cada linea
                    str[len - 1] = '\0';
                }

                sem_post(&semRead);
                break;
            }

            linesread++;

            sem_wait(&semRead); // lee el fichero de titulo obtenido en los tasks y va escribiendo linea a linea en el buffer
            pthread_mutex_lock(&Mutex);

            strcpy(buffer.lines[buffer.indexWrite % MAX_BUFFER], str);
            buffer.indexWrite++;

            pthread_mutex_unlock(&Mutex);
            sem_post(&semWrite); // si mete algo en el buffer hace post a semWrite para que el hilo de escritura pueda escribir
        }



        sprintf(logmsg, ":*:%lu:*:%s:*:%d lines read:*:", tid, file, linesread); // escribe mensaje en log informando de que ha leido un fichero
        writeLog(logmsg);
        fclose(fp);


        strcpy(filetmp, file);
        token = strtok(filetmp, "/");
        while (token != NULL) {
            lastToken = token; // Keep updating last token
            token = strtok(NULL, "/");
        }
        moveFile(file, lastToken);

        pthread_mutex_lock(&tasks->turns);
        
        // comprueba si ha funcionado el programa
        tasks->done++;
        if (tasks->done == tasks->max){
            pthread_mutex_unlock(&tasks->turns);
            sprintf(namesuc, "SU%03d" ,(tasks->me + 1));
            while(readDir(namesuc, tasks)){
                // lee los directorios hasta encontrar
                sleep(config.SLEEP_MIN + rand() % (config.SLEEP_MAX - config.SLEEP_MIN + 1));

                pthread_mutex_lock(&Mutex);
                if (buffer.finished){ // comprueba condicion de salida del while protegida por semaforos
                    pthread_mutex_unlock(&Mutex);
                    fflag = 1;
                    free(str);
                    free(file);
                    return NULL;
                }   
                pthread_mutex_unlock(&Mutex);

            };
        }
        else{
            pthread_mutex_unlock(&tasks->turns);
        }

    }

    free(str);
    free(file);
    return NULL;
}


/*--------------------------SHMEMORY--------------------------------------*/



line_t *shmbuffer; // punteros a memoria compartida
int *indexshmi;
int *resizeFlag;

int shmindex; // id de memoria compartida
int shmid; 
int semid;
int reshmid;

struct sembuf shmWait = {0, -1, 0}; // estructuras para cambiar el estado del semaforo compartido
struct sembuf shmSignal = {0, 1, 0};

size_t shmisize;
size_t shmincrement = 1024 * 1024; // variables de size para reajustar el size de la memoria compartida
int resizecount = 0;

void resizeShmi(){
    // funcion de reallocamiento de memoria

    resizecount++;
    *resizeFlag = resizecount;
    size_t size = (shmisize + shmincrement);
    key_t shmkey = ftok("/home/marti/key", resizecount % 2);

    int tmpshmid = shmget(shmkey, size, IPC_CREAT | 0666); // crea un nuevo espacio de memoria compartida con size mayor
    line_t *newshmbuffer = (line_t *)shmat(tmpshmid, NULL, 0);

    if (newshmbuffer == NULL){
        perror("shared mem failed resize\n");
    }

    // copia los datos de la memoria vieja a la nueva
    memcpy(newshmbuffer, shmbuffer, shmisize); 

    shmdt(shmbuffer); // hace free de la memoria compartida vieja
    shmctl(shmid, IPC_RMID, NULL);

    shmbuffer = newshmbuffer;
    shmid = tmpshmid; // reajusta los parametros y muevo el puntero al nuevo espacio de memoria
    shmisize += shmincrement;


}

void shmiWrite(char *line){
    // funcion para escribir una linea en la memoria compartida

    semop(semid, &shmWait, 1);

    // comprueba si hay sufiente espacio en la memoria
    int size = (*indexshmi + 1) * sizeof(line_t);
    if (size >= shmisize){ resizeShmi(); }

    // escribe la linea en la memoria y suma al index
    strcpy(shmbuffer[*indexshmi], line);
    (*indexshmi)++;

    semop(semid, &shmSignal, 1);
}


void* WriteShmi() { 
    // funcion de hilo de escritura en memoria compartida

    char logmsg[MAX_STR]; 
    char *line = (char*)malloc(MAX_LINE * sizeof(char));


    while (1){

    	sem_wait(&semWrite);

    	pthread_mutex_lock(&Mutex); 
    	if (buffer.finished && buffer.indexRead==buffer.indexWrite){
            // comprueba condicion de salida del while protegida por semaforos
    		pthread_mutex_unlock(&Mutex);
    		break;
    	}

    	strcpy(line, buffer.lines[buffer.indexRead % MAX_BUFFER]);
        buffer.indexRead++; // cuando lee una linea pasa a la siguiente
        pthread_mutex_unlock(&Mutex);

        sem_post(&semRead);

        // escribe linea en memoria compartida
        shmiWrite(line);

    }
    
    return NULL;
}




struct taskSucursal ** sucArray; // crea una struct con la lista de los ficheros por leer por cada sucursal
  
pthread_t* threads; // crea los hilos necesarios para lectura y uno para escritura
pthread_t threadWrite;


void cleanup(int signum) {
    // funcion para guardar el programa


    // pide al usuario si desea terminar el programa o no
    char response[10];
    printf("\nDesea cerrar el programa? (y/n): ");
    fgets(response, sizeof(response), stdin);

    printf(":::: CLEANUP CHECK 1 ::::::::: \n");

    if (response[0] == 'y' || response[0] == 'Y') {

        // Guarda los datos en un fichero
        FILE *consfp = fopen("consolidado.csv", "w");
        if (consfp != NULL) {
                for (int i = 0; i < *indexshmi; i++) {
                fprintf(consfp, "%s", shmbuffer[i]);
            }
            fflush(consfp);
            fclose(consfp);
        }

        //---------- Cleanup ---------

        // levanta flag de finished para indicar a los hilos que pueden morir

        pthread_mutex_lock(&Mutex);
        buffer.finished = 1;
        pthread_mutex_unlock(&Mutex); 

        // abre el semaforo para todos los hilos para que puedan morir
        for (int i = 0; i < config.NUM_PROCESOS; i++){
            sem_post(&sucArray[i % config.NUM_SUCURSALES]->semFiles);
        }

        
        // espera a que mueran los hilos de lectura
        for (int i = 0; i < config.NUM_PROCESOS; i++){
            printf("joining read thread %d \n", i+1); 
            pthread_join(threads[i], NULL);
        }

        // libera todos los espacios de memoria utilizados para manejar la lectura de ficheros
        for (int i = 0; i < config.NUM_SUCURSALES; i++){ 
            free(sucArray[i]->tasks);
            free(sucArray[i]);
        }

        // espera a que muera el hilo de escritura
        sem_post(&semWrite);
        pthread_join(threadWrite, NULL);
        
        fclose(logfp);

        // destruye semaforos
        sem_destroy(&semWrite);
        sem_destroy(&semRead);
        pthread_mutex_destroy(&Mutex); 
        pthread_mutex_destroy(&logMutex);
        pthread_mutex_destroy(&dirMut);
        pthread_mutex_destroy(&readDirMut);

        // libera la memoria compartida
        shmdt(shmbuffer);
        shmdt(indexshmi);
        shmdt(resizeFlag);
        shmctl(shmid, IPC_RMID, NULL);
        shmctl(shmindex, IPC_RMID, NULL);
        shmctl(reshmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);

        exit(EXIT_SUCCESS);
    }
}


void readCons(){
    // funcion de lectura de consolidado al arrancar

    char line[256];

    // lee de consolidado y lo guarda en memoria compartida
    FILE *consfp = fopen("consolidado.csv", "r");
    if (consfp != NULL) {
        while(fgets(line, 256, consfp)) {
            shmiWrite(line);
        }
        fclose(consfp);
    }
}

int main(int argc, char * argv[]){

    signal(SIGINT, cleanup);

    srand(time(NULL));

    readConfig("fp.conf"); // lee fichero configuracion

    buffer.indexRead = 0;
    buffer.indexWrite = 0; // indices de buffer para control de flujo del programa
    buffer.finished = 0;

    printf("%s=\"%s\"\n","PATH_FILES",config.PATH_FILES); 
    printf("%s=\"%s\"\n","INVENTORY_FILE",config.INVENTORY_FILE); 
    printf("%s=\"%s\"\n","LOG_FILE",config.LOG_FILE); 
    printf("%s=\"%d\"\n","NUM_PROCESOS",config.NUM_PROCESOS); 
    printf("%s=\"%d\"\n","NUM_SUCURSALES",config.NUM_SUCURSALES); 
    printf("%s=\"%d\"\n","SIMULATE_SLEEP",config.SIMULATE_SLEEP);  // demuestra los valores leidos del config por pantalla
    printf("%s=\"%d\"\n","SLEEP_MIN",config.SLEEP_MIN);
    printf("%s=\"%d\"\n","SLEEP_MAX",config.SLEEP_MAX); 
    printf("%s=\"%d\"\n","SHM_SIZE",config.SHM_SIZE); 
    printf("****************\n");

    
    logfp = fopen(config.LOG_FILE, "w+"); // abre el fichero log que se mantiene abierto hasta el final del programa
    if (logfp == NULL){
    	printf("Could not find log_file");
    	return -1;
    }


    // generar ficheros prueba
    char generateFiles[128];
    int ficheros;
    printf("Numero de ficheros prueba:");
    scanf("%i", &ficheros);
    sprintf(generateFiles, "./generate.sh -l 10 -s 5 -f %d -u 98 -t COMPRA0", ficheros);
    printf("::: SYSTEM :::: %s", generateFiles);
    system(generateFiles);


    // valor inicial de size de memoria compartida
    shmisize = config.SHM_SIZE * 1024 * 1024;

    // valor inicial de los keys de memoria compartida
    key_t shmkey = ftok("/home/marti/key", 'K');
    key_t semkey = ftok("/home/marti/key", 'S');
    key_t shikey = ftok("/home/marti/key", 'I');
    key_t reshmikey = ftok("/home/marti/key", 'R');

    // shmget allocata una zona de memoria y devuelve un identificador de ella
    shmindex = shmget(shikey, sizeof(int), IPC_CREAT | 0666);
    reshmid = shmget(reshmikey, sizeof(int), IPC_CREAT | 0666);
    shmid = shmget(shmkey, shmisize, IPC_CREAT | 0666);
    semid = semget(semkey, 1, IPC_CREAT | 0666);
    
    // shmat asigna un puntero a la zona de memoria
    shmbuffer = (line_t *)shmat(shmid, NULL, 0);
    indexshmi = (int *)shmat(shmindex, NULL, 0);
    resizeFlag = (int *)shmat(reshmid, NULL, 0);

    // inicializa semaforo compartido
    semctl(semid, 0, SETVAL, 1);

    // inicializa el indice de memoria compartida
    *indexshmi = 0;
    *resizeFlag = 0;
    readCons(); // lee el fichero consolidado y lo vuelca a la memoria compartida

    
    char contSuc[6] = "SU000"; // formato nombre de sucursal en el titulo de ficheros
    
    // inicializa todos los semaforos
    sem_init(&semRead, 0, config.NUM_PROCESOS);
    sem_init(&semWrite, 0, 0);
    pthread_mutex_init(&Mutex, NULL); 
    pthread_mutex_init(&logMutex, NULL);

    pthread_mutex_init(&dirMut, NULL);
    pthread_mutex_init(&readDirMut, NULL);
    dirFlag = 5;
    
    // inicializa buffer de tasks y array de hilos en memoria
    sucArray = (struct taskSucursal **)malloc(sizeof(struct taskSucursal *) * config.NUM_SUCURSALES);
    threads = (pthread_t *)malloc(sizeof(pthread_t) * config.NUM_PROCESOS);


    /* recorre el directorio una vez por sucursal guardando en la lista
    de ficheros por leer de cada una los titulos de los ficheros de dicha sucursal */

    
    for (int i = 0; i < config.NUM_SUCURSALES; i++){

        sucArray[i] = (struct taskSucursal*)malloc(sizeof(struct taskSucursal));
        sprintf(contSuc, "SU%03d" ,(i+1));

        pthread_mutex_init(&(sucArray[i]->turns), NULL);
        sem_init(&sucArray[i]->semFiles, 0, 0);
        sucArray[i]->i = 0; // read dir utiliza un DIR* para recorrer el file path del config y guardar el titulo en la lista de taskSucursal
        sucArray[i]->max = 0;
        sucArray[i]->done = 0;
        sucArray[i]->me = i;
        sucArray[i]->highest=MAX_FILES;
        sucArray[i]->tasks = (char**)malloc(sizeof(char*) * sucArray[i]->highest);

        if(readDir(contSuc, sucArray[i])){
            // deja un hilo entrar
            sem_post(&sucArray[i]->semFiles);
        };
    }

    // lanza la cantidad de hilos definido en el config para que vayan leyendo sus ficheros
    for (int i = 0; i < config.NUM_PROCESOS; i++){
        pthread_create(&threads[i], NULL, ReadFile, (void*)sucArray[i % config.NUM_SUCURSALES]);
    } 


    // lanza un hilo que va escribiendo en consolidado.csv
    pthread_create(&threadWrite, NULL, WriteShmi, NULL); 

    
    // hace fork y uno de los procesos muere y lanza el monitor
    pid_t pid = fork();
    if (pid == 0){
        execl("./testMon", "testMon", NULL);
    }
    

    // programa infinito
    while(1){
    	sleep(config.SLEEP_MIN + rand() % (config.SLEEP_MAX - config.SLEEP_MIN + 1));
        flushLog();
    }
    
    return 0;
}



void readConfig(char* dirConfig){ // read config lee el archivo config de direccion pasado por parametro
     FILE* fp;
     fp = fopen(dirConfig, "r");
     char line[MAX_LINE]; // abre el archivo y lo recorre separando las lineas por un =
     char *key, *value;
     while(fgets(line, MAX_LINE, fp) != NULL){ // utiliza el string antes del = como key para comparar con los parametros que busca
        key = strtok(line, "=");
        value = strtok(NULL, "\n");  // el valor va despues del '=' pero tenemos que separar por '\n' para que no lea la separacion de linea
        if (key && value) {
            if (strcmp(key, "PATH_FILES") == 0) { 
                strcpy(config.PATH_FILES, value);
            } else if (strcmp(key, "INVENTORY_FILE") == 0) {
                strcpy(config.INVENTORY_FILE, value);
            } else if (strcmp(key, "LOG_FILE") == 0) { // compara el key con todas las opciones para ir asignando los values
                strcpy(config.LOG_FILE, value);
            } else if (strcmp(key, "NUM_PROCESOS") == 0) {
                config.NUM_PROCESOS = atoi(value);
            } else if (strcmp(key, "NUM_SUCURSALES") == 0) {
                config.NUM_SUCURSALES = atoi(value);
            } else if (strcmp(key, "SIMULATE_SLEEP") == 0) {
                config.SIMULATE_SLEEP = atoi(value);
            } else if (strcmp(key, "SLEEP_MIN") == 0) {
                config.SLEEP_MIN = atoi(value);
            } else if (strcmp(key, "SLEEP_MAX") == 0) {
                config.SLEEP_MAX = atoi(value);
            }
            else if (strcmp(key, "SHM_SIZE") == 0) {
                config.SHM_SIZE = atoi(value);
            }
        }
        printf("read line: %s \n", key);
    }
    fclose(fp);
    printf("closed read\n");
}

