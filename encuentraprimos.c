
//NOTA: Compilar de la siguiente manera: gcc -D_GNU_SOURCE encuentraprimos.c -o encuentraprimos

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>

#define LONGITUD_MSG 100           //Payload del mensaje
#define LONGITUD_MSG_ERR 200       //Mensajes de error por pantalla

//Códigos de exit por error
#define ERR_ENTRADA_ERRONEA 2
#define ERR_SEND 3
#define ERR_RECV 4
#define ERR_FSAL 5

#define NOMBRE_FICH "primos.txt"
#define NOMBRE_FICH_CUENTA "cuentaprimos.txt"
#define CADA_CUANTOS_ESCRIBO 5

//Rango de búsqueda, desde BASE a BASE+RANGO
#define BASE 800000000
#define RANGO 2000

//Intervalo del temporizador para RAIZ
#define INTERVALO_TIMER 5

//Códigos de mensaje para el campo mesg_type del tipo T_MESG_BUFFER
#define COD_ESTOY_AQUI 5           //Un calculador indica al SERVER que está preparado
#define COD_LIMITES 4              //Mensaje del SERVER al calculador indicando los límites de operación
#define COD_RESULTADOS 6           //Localizado un primo
#define COD_FIN 7                  //Final del procesamiento de un calculador

//Mensaje que se intercambia
typedef struct {
    long mesg_type;
    char mesg_text[LONGITUD_MSG];
} T_MESG_BUFFER;

//Prototipos de funciones utilizadas
int Comprobarsiesprimo(long int numero);
void Informar(char *texto, int verboso);
void Imprimirjerarquiaproc(int pidraiz, int pidservidor, int *pidhijos, int numhijos);
int ContarLineas(FILE *fichero);
static void alarmHandler(int signo);

//Variables globales
int cuentasegs;                   //Variable para el cómputo del tiempo total

int main(int argc, char* argv[]){
	
	//Variables	
	int i, j;
	long int numero;
	long int numprimrec;
    long int nbase;
    int nrango;
    int nfin;
    time_t tstart, tend; 
	
	key_t key;
    int msgid;    
    int pid, pidservidor, pidraiz, parentpid, mypid, pidcalc;
    int *pidhijos;
    int intervalo, inicuenta;
    int verbosity;
    T_MESG_BUFFER message;
    char info[LONGITUD_MSG_ERR];
    FILE *fsal, *fc;
    int numhijos;
	int contfin = 0;
	
    //Control de entrada, después del nombre del script debe figurar el número de hijos y el verbosity
    
    numhijos = strtol(argv[1], NULL, 10);
    verbosity = strtol(argv[2], NULL, 10);
	
	//Apertura de ficheros y control de errores
	fsal = fopen("primo.txt", "w");
	fc = fopen("cuentaprimos.txt", "w+");
	
	if(fsal == NULL){
		printf("Error al crear primo.txt\n");
		return -1;
    }
    
    if(fc == NULL){
		printf("Error al crear cuentaprimos.txt\n");
		return -1;
    }
	
    pid = fork();       //Creación del SERVER
    
    if(pid == 0){       //Rama del hijo de RAIZ (SERVER)
		pid = getpid();
		pidservidor = pid;
		mypid = pidservidor;	   
		
		//Petición de clave para crear la cola
		if((key = ftok("/tmp", 'C')) == -1){
			perror("Fallo al pedir ftok");
			exit(1);
		}
		
		printf("Server: System V IPC key = %u\n", key);

        //Creación de la cola de mensajería
		if((msgid = msgget(key, IPC_CREAT | 0666)) == -1){
			perror("Fallo al crear la cola de mensajes");
			exit(2);
		}
		
		printf("Server: Message queue id = %u\n", msgid );

        i = 0;
        
        //Creación de los procesos CALCuladores
		while(i < numhijos){
			if(pid > 0){ //Solo el SERVER creará hijos
				pid = fork(); 
				if(pid == 0){   //Rama hijo
					parentpid = getppid();
					mypid = getpid();
				} 
			}
			i++;  //Número de hijos creados
		}

        //Logica de negocio de cada CALCulador 
		if(mypid != pidservidor){
			
			//Envio de los mensajes COD_ESTOY_AQUI de los hijos al padre
			message.mesg_type = COD_ESTOY_AQUI;
			sprintf(message.mesg_text, "%d", mypid);
			if(msgsnd( msgid, &message, sizeof(message), IPC_NOWAIT) == -1) perror("CALC: msgsnd() de COD_ESTOY_AQUI");
			
			//Recepcion de los mensajes con los limites y numeros por lo que comenzar
			if(msgrcv(msgid, &message, sizeof(message), COD_LIMITES, 0) == -1) perror("CALC: msgrcv() de COD_LIMITES"); //ESTE era un problema, el error era causado por borrar la cola de mensajeria demasiado pronto
			sscanf(message.mesg_text, "%ld %d", &numero, &nrango); 
			printf("CALC: El CALCulador %d empieza a calcular desde %ld con un rango de %d\n\n", mypid, numero, nrango);
			
			//Bucle de busqueda de numeros primos
			message.mesg_type = COD_RESULTADOS;
			for(i = 0; i < nrango; i++){ 
				//Si numero es primo lo mando por la cola de mensajeria junto con el pid quien lo calculo
				if(Comprobarsiesprimo(numero)){
					sprintf(message.mesg_text, "%d %ld", mypid, numero);
					if(msgsnd(msgid, &message, sizeof(message), IPC_NOWAIT) == -1) perror("CALC: msgsnd() de COD_RESULTADOS");
				}
				numero++;
			}
			
			//Envio de mensaje de terminacion de proceso CALCulador
			printf("\nCALC: Soy el hijo %d, he terminado de calcular y me muero\n\n", mypid);
			message.mesg_type = COD_FIN;
			if(msgsnd(msgid, &message, sizeof(message), IPC_NOWAIT) == -1) perror("CALC: msgsnd() de COD_FIN");
			
			//Fin del proceso CALCulador
			exit(0);
		}
		
		//SERVER
		
		else{
			
			//Pide memoria dinámica para crear la lista de pids de los hijos CALCuladores
			pidhijos = malloc(sizeof(int) * numhijos);
		  
			//Recepción de los mensajes COD_ESTOY_AQUI de los hijos
			for(j = 0; j < numhijos; j++){
				if(msgrcv(msgid, &message, sizeof(message), COD_ESTOY_AQUI, 0) == -1) perror("SERVER: msgrcv() de COD_ESTY_AQUI");
				sscanf(message.mesg_text, "%d", &pid); // Tendrás que guardar esa pid 
				printf("\nSERVER: Me ha enviado un mensaje el hijo %d\n", pid);
				pidhijos[j] = pid; //Guardo esa pid
				//printf("Guardado pid hijo: %d\n", pidhijos[j]); //Comentado porque el malloc() funciona correctamente
			}
		  
			//Imprimo la jerarquia de procesos 
			Imprimirjerarquiaproc(getppid(), pidservidor, pidhijos, numhijos);
		  
			//Envio del numero por el que empieza cada hijo y cuantos valores debe recorrer
			message.mesg_type = COD_LIMITES;
			for(j = 0; j < numhijos; j++){
				sprintf(message.mesg_text, "%ld %d", (long)(j * RANGO / numhijos) + BASE, RANGO / numhijos); //Esto SI funciona, lo he probado
				if(msgsnd(msgid, &message, sizeof(message), IPC_NOWAIT) == -1) perror("SERVER: msgsnd() de COD_LIMITES");
				printf("SERVER: Envio numero inicial %d y el rango %d\n\n", (j * RANGO / numhijos) + BASE, RANGO / numhijos);
			}
			
			//Comienzo del temporizador de tiempo de computo
			tstart = time(NULL);
			
			//Bucle que recive los numeros primos de los CALCuladores
			while(contfin < numhijos){
				if(msgrcv(msgid, &message, sizeof(message), COD_LIMITES, MSG_EXCEPT) == -1) perror("SERVER: msgrcv() de COD_RESULTADOS/COD_FIN");
				sscanf(message.mesg_text, "%d %ld", &pidcalc, &numprimrec);
				if(message.mesg_type == COD_RESULTADOS){
					if(verbosity > 0) printf("SERVER: El hijo %d a encontrado el primo %ld\n", pidcalc, numprimrec);
					fprintf(fsal, "%ld\n", numprimrec);
					intervalo++;
					if(intervalo % 5 == 0) fprintf(fc, "%d\n", intervalo);
				}
				else if(message.mesg_type == COD_FIN) contfin++;
					 else printf("SERVER: Mensaje tipo %ld\n", message.mesg_type);	
			}
			
			//Final del temporizador de tiempo de computo
			tend = time(NULL);
			
			//Tiempo de computo total
			printf("\nEl tiempo total de calculo es de %.2f segundos\n", difftime(tend, tstart));
			
			//Borrado de la cola de mansajeria
			if(msgctl(msgid, IPC_RMID, NULL) == -1) perror("SERVER: msgctl() borrar cola");
			else printf("\nCola de mensajeria borrada\n");
			
			//Liberacion de memoria dinamica
			free(pidhijos);
			
			//Fin del proceso SERVER
			exit(0);
		}
	}

    //Rama de RAIZ, proceso primigenio
    else{
		
		pidraiz = getpid();
		alarm(INTERVALO_TIMER);
		signal(SIGALRM, alarmHandler);
		
		//Espera al final de SERVER
		wait(NULL);
		fclose(fsal);
		fsal = fopen("primo.txt", "r");
		//Recuento de lineas del fichero primos.txt
		printf("\nEl numero total de primos calculados es %d\n", ContarLineas(fsal));
	}
	
	//Cierre de los ficheros
	fclose(fsal);
	fclose(fc);
	
	//El final de todo
	return 0;
}

//Manejador de la alarma en el RAIZ
static void alarmHandler(int signo){ //El puntero del fichero puede que no vuelva al inicio
    printf("\nSOLO PARA EL ESQUELETO... Han pasado 5 segundos\n");
    /*
    int numero;
    printf("\n");
    while(!feof(fc)){
		fscanf(fc, "%d", &numero);
		printf("%d\n", numero);
	}
	printf("\n");
	*/
    alarm(INTERVALO_TIMER);
}

//Funcion que imprime las pid de todos los procesos
void Imprimirjerarquiaproc(int pidraiz, int pidservidor, int *pidhijos, int numhijos){
	printf("\nJerarquia de procesos:");
	printf("\nRAIZ		SERV		CALC\n");
	printf("%d%16d%16d\n", pidraiz, pidservidor, pidhijos[0]);
	for(int i = 1; i < numhijos; i++) printf("				%d\n",pidhijos[i]);
	printf("\n\n");
}

//Funcion que comprueba si un numero es primo, devuelve 1 si es primo
int Comprobarsiesprimo(long int numero){
	if(numero < 2) return 0;
	else{
		for(int x = 2; x <= (numero / 2); x++)
			if(numero % x == 0) return 0;
	}
	return 1;
}

//Funcion que cuentas la lineas de un fichero
int ContarLineas(FILE *fichero){
	int lineas = 0;
	while(!feof(fichero)){
		if(fgetc(fichero) == '\n') {
		lineas++;
		}
	}
	return lineas;
}

