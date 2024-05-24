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
#include <errno.h>
#include <signal.h>

#define HASHSIZE 1024
#define ALLTIPOS 15 // constantes
#define MAX_BUFFER 20
#define KEYT 1234


typedef struct patron{
    struct patron *next;
    char *key;
    int value;
} patron_t; // key value pair de un patron


typedef patron_t* table; // tipo de date de tabla de key value pair

table* createTable(){
    // crea una tabla en memoria con calloc
    return calloc(HASHSIZE, sizeof(patron_t));
}


unsigned int hash(char *key){
    // operacion de hack para una key
    unsigned int hv;
    char *s = key;
    for (hv = 0; *s != '\0'; s++) {
		hv = *s + 31 * hv;
	}		
    return hv % HASHSIZE;
}


patron_t* insert(table *tab, char *key){

    // inserta en la tabla un nuevo key value pair
    // utiliza una hash de 0 a 1023 para no recorrer toda la tabla
    unsigned int h = hash(key);
    patron_t* p = tab[h];


    if (p == NULL){
        // primera key value de este hash es NULL hay que crearlo
        p = malloc(sizeof(patron_t));
        p->key = strdup(key);
        p->value = 0;
        p->next = NULL;
        tab[h] = p;
        return p;
    }
    patron_t* last = p;
    // declara un last para camnbiar el next
    while(p != NULL && strcmp(p->key, key)!=0){
        // busca al siguiente hasta que encuentre null o la key
        last = p;
        p = p->next;
    }
    if (p == NULL){
        // si ha encontrado null no se ha encontrado esa key aun asique la tiene q crear
        patron_t* new = malloc(sizeof(patron_t));
        new->key = strdup(key);
        new->value = 0;
        new->next = NULL;
        last->next = new;
        return new;
    }
    else
    {
        // si ya existe le devuelve el puntero al key value pair encontrado
        return p;
    }
}

// tipo de una linea que nos manda la memoria compartida
typedef char line_t[128];


// memoria compartida
line_t *shmbuffer;
int *shmindex;
int *resizeFlag;

int shmidindex;
int shmid;
int semid;
int reshmid;

size_t shmisize;
size_t shmincrement = 1024 * 1024;

struct sembuf shmWait = {0, -1, 0};
struct sembuf shmSignal = {0, 1, 0};
// memoria compartida

void shmInit() {    
    // inicia la memoria compartida con 2Mb de datos
    shmisize = 2 * 1024 * 1024;

    // assigna los keys
    key_t shmkey = ftok("/home/marti/key", 'K');
    key_t semkey = ftok("/home/marti/key", 'S');
    key_t shikey = ftok("/home/marti/key", 'I');
    key_t reshmikey = ftok("/home/marti/key", 'R');

    // coje el id de la zona de memoria
    shmidindex = shmget(shikey, sizeof(int), IPC_CREAT);
    reshmid = shmget(reshmikey, sizeof(int), IPC_CREAT);
    shmid = shmget(shmkey, shmisize, IPC_CREAT);
    semid = semget(semkey, 1, IPC_CREAT | 0666);

    // attach de puntero a zona de memoria
    shmbuffer = (line_t *)shmat(shmid, NULL, 0);
    shmindex = (int *)shmat(shmidindex, NULL, 0);
    resizeFlag = (int *)shmat(reshmid, NULL, 0);

    if (shmidindex == -1) {
        printf("::: SHINDEX ::: ");
        perror("shmget");
        exit(1);
    }
    if (shmid == -1) {
        printf("::: SHMID ::: ");
        perror("shmget");
        exit(1);
    }
    if (semid == -1) {
        printf("SEMID :::: FAILED \n");
        perror("semget");
        exit(1);
    }
}


void resizeShmi(){
    // funcion de reallocamiento de memoria
    size_t size = shmisize + shmincrement;
    key_t shmkey = ftok("/home/marti/key", *resizeFlag % 2);

    int tmpshmid = shmget(shmkey, size, IPC_CREAT | 0666); // crea un nuevo espacio de memoria compartida con size mayor
    line_t *newshmbuffer = (line_t *)shmat(tmpshmid, NULL, 0);

    // copia los datos de la memoria vieja a la nueva
    memcpy(newshmbuffer, shmbuffer, shmisize);

    shmdt(shmbuffer); // hace free de la memoria compartida vieja
    shmctl(shmid, IPC_RMID, NULL);

    shmbuffer = newshmbuffer;
    shmid = tmpshmid; // reajusta los parametros y muevo el puntero al nuevo espacio de memoria
    shmisize += shmincrement;

    *resizeFlag = 0;

}



int shmlineRead(char* line, int * index, int thread){ // copies shared memory line into *line using index as the index


    semop(semid, &shmWait, 1); // returns 1 if index is too high

    if (*resizeFlag != 0){
        resizeShmi();
    }

    if (*shmindex <= *index){
        semop(semid, &shmSignal, 1);
        return 1;
    }

    strncpy(line, shmbuffer[*index], 255); 
    semop(semid, &shmSignal, 1);
    (*index)++;
    if (line == NULL){
        return 1;
    }
    return 0;
}

void cleanup(int signum) {


    // libera la memoria compartida
    shmdt(shmbuffer);
    shmdt(shmindex);
    shmdt(resizeFlag);
    
    semop(semid, &shmSignal, 1);

    exit(EXIT_SUCCESS);
}




void* threadP1(){
    // funcion para encontrar primer patron

    char user[64];
    char date[64];
    char *token;
    int token_count;
    char line[256];
    char saveline[256];
    
    char key[256];

    int index = 0;
    table* hashtbl = createTable();
    patron_t *p;

    while (1){

        // lee la linea de memoria
        if (shmlineRead(line, &index, 1)){ 
            // si no puede leer una linea repita hasta que pueda
            continue; 
        }
        strcpy(saveline, line);
        
        token_count = 0;


        token = strtok(line, ",");
        while (token != NULL) {
            // strtok separa la linea por comas y mete los campos que nos interesan en una key
            // en este caso el dia la hora y el user
            token_count++;
            if (token_count == 2) {
                strncpy(date, token, sizeof(date)-1);
                date[sizeof(date)-1] = '\0';
                char *minutes = strchr(date, ':');
                if (minutes) {
                    *minutes = '\0';
                }
            }
            else if (token_count == 4) {
                strcpy(user, token);
            }
            token = strtok(NULL, ",");
        }
        
        snprintf(key, sizeof(key), "%s%s", date, user);

        // inserta la key en la tabla
        p = insert(hashtbl, key);
	    if (p == NULL){
		    printf("::: INSERT ERROR :::: \n");
		    exit(1);
	    }

        // incrementa el valor porque ha encontrado una ocurrencia
        p->value++;
        if (p->value >= 5){
            // si el valor alerta al patron lo dice
            printf(":*: patron 1 encontrado :*: %s \n", saveline);
        }



    }
}

void* threadP2(){

    char user[64];
    char key[128];
    char *token;
    char date[64];
    int token_count;
    char line[256];
    char saveline[256];
    int trans;

    int index = 0;

    table * hashtbl = createTable();
    patron_t *p;

    while (1){
        if (shmlineRead(line, &index, 2)){
            // intenta leer linea de memoria compartida hasta que pueda
            continue;
        }
        strcpy(saveline, line);

        // strtok para obtener campos y generar una key
        token_count = 0;
        token = strtok(line, ",");
        while (token != NULL) {
            // en este caso la key es el dia el user
            token_count++;
            if (token_count == 4) {
                strcpy(user, token);
            } 
            else if (token_count == 2) {
                strcpy(date, token);
                char *time = strchr(date, ' ');
                if (time) {
                    *time = '\0';
                }
            }
            else if (token_count == 7){
                sscanf(token, "%d", &trans);
            }
            token = strtok(NULL, ",");
        }

        snprintf(key, sizeof(key), "%s%s", date, user);

        if (trans < 0){
            // si hay una retirada busca el key y suma al value
            p = insert(hashtbl, key);

            p->value++;
            if (p->value > 3){
                // si encuentra un patron alerta al user
                printf(":*: patron 2 encontrado :*: %s\n", saveline);
            }
        }
    }
}

void* threadP3(){

    char key[128];
    char *token;
    char state[64];
    char user[64];
    int token_count;
    char line[256];
    char saveline[256];
    int reps;

    int index = 0;

    table* hashtbl = createTable();
    patron_t *p;

    while (1){

        if (shmlineRead(line, &index, 3)){ 
            // intenta leer linea de memoria compartida hasta que pueda
            continue; 
        }
        strcpy(saveline, line);


        // strtok para obtener campos y generar key
        token_count = 0;
        token = strtok(line, ",");
        while (token != NULL) {
            // en este caso es el user solo
            token_count++;
            if (token_count == 4) {
                strcpy(user, token);
            } 
            else if (token_count == 8) {
                strcpy(state,token);
            }
            token = strtok(NULL, ",");
        }

        if (strcmp(state, "Error")){
            // si la linea es un error busca la key y incremente las ocurrencias
            p = insert(hashtbl, user);
            p->value++;
            if (p-> value > 3){
                // si encuentra un patron se lo dice al user
                printf(":*: patron 3 encontrado :*: %s \n", saveline);
            }
        }
    }
}


void* threadP4(){

    char key[128];
    char *token;
    char tipooperacion[16];
    int tipo;
    int bit;
    int token_count;
    char line[256];
    char saveline[256];

    int index = 0;


    table* hashtbl = createTable();
    patron_t *p;

    while (1){

        if (shmlineRead(line, &index, 4)){ 
            // intenta leer linea de memoria compartida hasta que puede
            continue; 
        }
        strcpy(saveline, line);


        // strtok para obtener la key de los campos necesarios
        token_count = 0;
        token = strtok(line, ",");
        while (token != NULL) {
            // este caso nuestro key es el user
            token_count++;
            if (token_count == 4) {
                strcpy(key, token);
            } 
            else if (token_count == 5) {
                strcpy(tipooperacion, token); 
                tipo = tipooperacion[7] - '0';
                // utiliza un bitma parecido a los permisos para comprobar que tipos de operaciones se han realizado
                bit = 1 << (tipo-1);
            }
            token = strtok(NULL, ",");
        }

        // busca key en la tabla
        p = insert(hashtbl, key);
        p->value |= bit; // hace un or para cambiar el bit si es necesario
        if (p->value == ALLTIPOS){
            // compara el valor con una constante para ver si han ocurrido todos los tipos
            // alerta al user en caso de patron
            printf(":*: patron 4 encontrado :*: %s \n", saveline);
        }
    }
}


void* threadP5(){

    char date[128];
    char *token;
    int trans;
    int token_count;
    char line[256];
    char saveline[256];
    int total;

    int index = 0;


    table* hashtbl = createTable();
    patron_t *p;

    while (1){

        if (shmlineRead(line, &index, 5)){
            // lee memoria compartida hasta que encuentra una linea
            continue; 
        }

        strcpy(saveline, line);


        // strtok para obtener key
        token_count = 0;
        token = strtok(line, ",");
        while (token != NULL) {
            // en este caso el key es el dia
            token_count++;
            if (token_count == 2) {
                strcpy(date, token);
                char *time = strchr(date, ' ');
                if (time) {
                    *time = '\0';
                }
            } 
            else if (token_count == 7) {
                sscanf(token, "%d", &trans);
            }
            token = strtok(NULL, ",");
        }

        // busca el key en la tabla
        p = insert(hashtbl, date);
        
        // suma la transaccion
        p->value += trans;
        if (p->value < 0){
            // si hay un patron detectado le alerta al user
            printf(":*: patron 5 encontrado :*: %s \n", saveline);
        }
    }
}


int main(int *args)
{

    // cleanup para morir cuando el programa muera
    signal(SIGINT, cleanup);

    // inicializa memoria compartida
    shmInit();

    // genera un hilo por patron
    pthread_t patron[5];


    pthread_create(&patron[0], NULL, threadP1, NULL); 
    pthread_create(&patron[1], NULL, threadP2, NULL);
    pthread_create(&patron[2], NULL, threadP3, NULL);
    pthread_create(&patron[3], NULL, threadP4, NULL);
    pthread_create(&patron[4], NULL, threadP5, NULL);

    for(int i = 0; i < 5; i++){
        pthread_join(patron[i], NULL);
    }
    
    return 0;
}

