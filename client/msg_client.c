#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

#define BUF_SIZE			1024
#define CMD_DIM				128
#define DELIMITER			" \n"

//USATE PER MANDARE I COMANDI AL SERVER
#define HELP				"h"
#define WHO					"w"
#define QUIT				"q"
#define REGISTER			"r"
#define DEREGISTER			"d"
#define SEND				"s"

//USATE PER INTERPRETARE I MESSAGGI DI ERRORE RECEVUTI DAL SERVER
#define SUCCESS				1
#define RECONNECTION		2
#define REGISTERED_ALREADY	5
#define UNREGISTERED		7
#define ALREADY_IN_USE		9
#define RECEIVER_NOT_FOUND	15
#define RECEIVER_ONLINE		17
#define RECEIVER_OFFLINE	19
#define GENERIC_ERROR		25


//-------------------------VARIABILI GLOBALI----------------------
int TCP_sd;						//socket TCP per la connessione del client al server
int UDP_sd_in;					//socket UDP per ricevere messaggi
int UDP_sd_out;					//socket UDP per mandare messaggi

struct sockaddr_in server_addr;	//struttura che mantiene i dati del server
struct sockaddr_in in_addr;		//struttura del client udp per ricevere messaggi
struct sockaddr_in out_addr;	//struttura del client udp per mandare messaggi

char buffer[BUF_SIZE];			//buffer usato per ricevere e inviare messaggi
char *username;					//nome assegnato alla registrazione
char *local_IP;					//IP del client, inserito all'avvio
uint16_t UDP_port;				//porta UDP usata per il p2p, inserita all'avvio
char *server_IP;				//IP del server, inserito all'avvio
uint16_t server_port;			//porta TCP su cui il server è in ascolto



//--------------------------FUNZIONI DI UTILITA'--------------------
int receive_TCP_msg();
//chiude tutti i socket quando un utente si disconette
void rm_sock(int i){
	if(i == 0){				//quit chiama rm_sock(0) che chiude tutti i socket
		close(UDP_sd_in);	//invece deregister chiude solo il socket udp in ingresso
		close(TCP_sd);
	}
	if(UDP_sd_out != -1)	//viene creato solo se si mandano messaggi istantanei
		close(UDP_sd_out);	//deregister
}


/*Se un utente disconnette o deregistra vengono deallocate le strutture dinamiche*/
void free_user_struct(int i){
	if(username){
		free(username);
		username = NULL;
	}
	if(i == 0){
		free(local_IP);
		free(server_IP);
	}
}

//Funzione che riceve dal server più messaggi e li stampa. Usata da who e register in caso di riconnessione
void receive_multiple_msg(){
	int err;
	while(1){
		err = receive_TCP_msg();
		if(err == -1){
			printf("C'è stato un errore, si prega di ripetere l'operazione\n");
			return;
		}
		else {
			if(strcmp(buffer, "stop\0") != 0)
				printf("%s\n", buffer);
			else
				return;
			}
	}
}

//Funzione che controlla che l'ip inserito sia valido
int check_IP(char* ip){
	int ret;
	struct sockaddr_in aux;
	ret = inet_pton(AF_INET, ip, &(aux.sin_addr));
	if(ret == 0){
		perror("Impossibile convertire l'IP in formato network");
		return -1;
	} else {
		return 0;
	}
}

//Funzione che verifica che la porta inserita sia valida
int check_port(uint16_t port){
//[0-1023] well-known ports, [1024-49151] registered, [49152-65535] private
	if(port >= 1024 && port <= 65535)
		return 0;
	else
		return -1;
}

//controlla che il numero dei parametri sia giusto e che siano validi
void check_args(int argc, char *argv[]){
	//ERRORI NEL PASSAGGIO DI PARAMETRI
	if(argc != 5){		//argv, IP client, porta client, IP server, porta server
		printf("Numero di parametri errato!\n");
		printf("I parametri necessari sono: <IP locale> <porta locale> <IP server> <porta server>\n");
		exit(-1);
	}
	if(check_IP(argv[1]) == -1){
		printf("%s\n", "L'indirizzo IP non è valido");
		exit(-1);
	}
	if(check_IP(argv[3]) == -1){
		printf("%s\n", "L'indirizzo IP del server non è valido");
		exit(-1);
	}
	if(check_port(atoi(argv[2])) == -1){
		printf("%s\n", "La porta locale non è valida");
		exit(-1);
	}
	if(check_port(atoi(argv[4])) == -1){
		printf("%s\n", "La porta del server non è valida");
		exit(-1);
	}
}

//Funzione che inizializza le strutture del client con i parametri inseriti all'avvio
void struct_ini(char *argv[]){
	local_IP = (char *)malloc(strlen(argv[1]) + 1);
	server_IP = (char *)malloc(strlen(argv[3]) + 1);
	strncpy(local_IP, argv[1], strlen(argv[1]) + 1);
	UDP_port = atoi(argv[2]);
	strncpy(server_IP, argv[3], strlen(argv[3]) + 1);
	server_port = atoi(argv[4]);
	username = NULL;
	UDP_sd_in = -1;
	UDP_sd_out = -1;
}

/*funzione che stampa un prompt contenente il nome dell'utente registrato, stampa solo ">" prima della registrazione*/
void print_prompt(){
	if(username)
			printf("%s> ", username);
		else
			printf("> ");
}

/*Funzione usata per l'invio del nome dell'utente in piggybank con il comando, che viene sostituito da un codice (prima lettera). Nel caso di comandi senza argomenti viene concatenato uno spazio*/
char* pack_server_msg(char *cmd, char *arg){
	char *srv_msg = (char *)malloc(strlen(cmd) + strlen(arg) + 1);
	strcpy(srv_msg, cmd);
	strcat(srv_msg, " ");
	strcat(srv_msg, arg);
	return srv_msg;
}

/*Funzione che acquisice un messaggio da tastiera e lo mette nel buffer, concatenandolo al nome del mittente in caso di messaggio istantaneo */
void get_user_msg(int type){	//0 se messaggio offline, 1 online
	char scan[BUF_SIZE];
	memset(&scan, '\0', BUF_SIZE);
	memset(&buffer, '\0', BUF_SIZE);
	printf("\n");

	if(type == 1){
		strcpy(buffer, username);
		strcat(buffer, "(msg istantaneo)>\n");
	}

	while (1){	//prendo mess da tastiera fino al punto
		fgets(scan, BUF_SIZE, stdin);
		if(strcmp(scan, ".\n") != 0){
			strncat(buffer, scan, strlen(scan) - 1);	//tolgo \n
		} else {
			break;
		}
	}
}





//------------------------FUNZIONI PER LA CONNESSIONE-----------------------

void send_TCP_msg(char* msg);

//Funzione che crea il socket per il destinatario di messaggi UDP
int create_dest_sock(uint16_t port, char *ip){
	UDP_sd_out = socket(AF_INET, SOCK_DGRAM, 0);
	if(UDP_sd_out < 0){
		perror("Impossibile creare socket per il destinatario");
		return -1;
	}
	//inizializzazione parametri server
	memset(&out_addr, 0, sizeof(out_addr));
	out_addr.sin_family = AF_INET;
	out_addr.sin_port = htons(port);

	check_IP(ip);
	return 0;
}

//Funzione che crea il socket per il mittente UDP
int create_my_sock(){
	int ret;
	UDP_sd_in = socket(AF_INET, SOCK_DGRAM, 0);
	if(UDP_sd_in < 0){
		perror("Impossibile creare socket");
		return -1;
	}
	//inizializzazione parametri server
	memset(&in_addr, 0, sizeof(in_addr));
	in_addr.sin_family = AF_INET;
	in_addr.sin_port = htons(UDP_port);

	ret = bind(UDP_sd_in, (struct sockaddr *)&in_addr, sizeof(in_addr));
	if(ret < 0){
		perror("Errore in fase di bind");
		return -1;
	}
	return 0;
}

/*Funzione che mette nel buffer porta UDP e ip perchè vengano mandate al server durante la registrazione, verranno inviati ai client che richiedono di mandare messaggi istantanei a questo utente */
void prepare_udp_ini(){
	sprintf(buffer, "%u", UDP_port);
	strcat(buffer, " ");
	strcat(buffer, local_IP);
	send_TCP_msg(buffer);
}

//Funzione che inizializza i parametri relativi al server
int create_my_sock();

void connection_set(){
	int ret;
	TCP_sd = socket(AF_INET, SOCK_STREAM, 0);
	if(TCP_sd < 0){
		perror("Impossibile creare socket");
		exit(-1);
	}
	//inizializzazione parametri server
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);

	//connessione
	ret = connect(TCP_sd, (struct sockaddr*)&server_addr, sizeof(server_addr));

	if(ret < 0){
		perror("Connessione con il server fallita");
		exit(-1);
	} else {
		printf("\nConnessione al server %s", server_IP);
		printf(" (porta %d) effettuata con successo\n", server_port);
	}
	ret = create_my_sock();	//creo un socket UDP per la ricezione dei messaggi istantanei

	if(ret == -1){
		printf("Impossibile inizializzare connessione UDP\n");
		exit(-1);
	} else {
		printf("Ricezione messaggi istantanei su porta %d\n\n", UDP_port);
	}
}





//-------------------FUNZIONI PER LA COMUNICAZIONE---------------------------

//Funzione che invia un messaggio TCP dopo averne inviata la dimensione
void send_TCP_msg(char* msg){
	int sent;
	int size = sizeof(uint16_t);
	int len = strlen(msg) + 1;		//voglio inviare anche \0
	uint16_t msg_len = htons(len);	//converto in formato network prima dell'invio

	sent = send(TCP_sd, (void *)&msg_len, size, 0);
	if(sent == -1 || sent < size){
		perror("Trasferimento dimensione incompleto");
		return;
	}
	sent = send(TCP_sd, (void *)msg, len, 0);
	if(sent == -1 || sent < len){
		perror("Trasferimento incompleto");
		return;
	}
}

//Funzione che riceve un messaggio TCP dopo averne ricevuta la dimensione
int receive_TCP_msg(){
	int received;
	int len;
	uint16_t msg_len;

	memset(&buffer, '\0', BUF_SIZE);

	received = recv(TCP_sd, (void *)&msg_len, sizeof(uint16_t), 0);
	if(received <= 0){
		printf("Errore ricezione dimensione messaggio TCP\n");
		return -1;
	}
	len = ntohs(msg_len);

	received = recv(TCP_sd, (void *)buffer, len, 0);
	if(received <= 0){
		printf("Errore ricezione messaggio TCP\n");
		return -1;
	}
	return 0;
}

//Funzione che invia un messaggio UDP dopo averne inviata la dimensione
void send_UDP_msg(char* msg){
	int sent;
	int len = strlen(msg) + 1;		//voglio inviare anche \0
	uint16_t msg_len = htons(len);	//converto in formato network prima dell'invio
	socklen_t addrlen = sizeof(out_addr);

	sent = sendto(UDP_sd_out, (void *)&msg_len, sizeof(uint16_t), 0, (struct sockaddr*)&out_addr, addrlen);
	if(sent == -1 || sent < sizeof(uint16_t)){
		perror("Trasferimento dimensione incompleto");
		return;
	}
	sent = sendto(UDP_sd_out, (void *)msg, len, 0, (struct sockaddr*)&out_addr, addrlen);
	if(sent == -1 || sent < len){
		perror("Trasferimento incompleto");
		return;
	}
}

//Funzione che riceve un messaggio UDP dopo averne ricevuta la dimensione
int receive_UDP_msg(){
	int received;
	int len;
	uint16_t msg_len;
	socklen_t addrlen = sizeof(in_addr);

	memset(&buffer, '\0', BUF_SIZE);

	received = recvfrom(UDP_sd_in, (void *)&msg_len, sizeof(uint16_t), 0, (struct sockaddr*)&in_addr, &addrlen);
	if(received <= 0){
		printf("Errore ricezione dimensione messaggio UDP\n");
		return -1;
	}

	len = ntohs(msg_len);
	received = recvfrom(UDP_sd_in, (void *)buffer, len, 0, (struct sockaddr*)&in_addr, &addrlen);
	if(received <= 0){
		printf("Errore ricezione messaggio UDP\n");
		return -1;
	}
	return 0;
}

/*Funzione che riceve un messaggio di errore o conferma dal server e lo trasforma nell' intero corrispondente*/
int receive_response_msg(){
	int aux;
	aux = receive_TCP_msg();	//errore di recezione
	if(aux == -1)
		return GENERIC_ERROR;

	aux = atoi(buffer);	//ricevo una stringa che converto in intero
	return aux;
}






//---------------------------FUNZIONI DEL CLIENT----------------------------
/*Funzione che registra un utente presso il server e inizializza le strutture necessare per la comunicazione con gli altri utenti*/
void register_client(char *name){
	int response;

	//invio ip e porta UDP al server in un unico messaggio
	prepare_udp_ini();

	response = receive_response_msg();
	switch(response){
		case REGISTERED_ALREADY:
			receive_TCP_msg();
			printf("Sei già registrato con l'username: \"%s\"\n", buffer);
			break;
		case ALREADY_IN_USE:
			printf("E' già presente un utente con questo nome, sceglierne uno diverso\n");
			break;
		case SUCCESS:
			printf("Registrazione avvenuta con successo\n");
			//salvataggio del nome del client in una variablile locale, sarà usato da send
			username = (char *)malloc(strlen(name) + 1);
			strcpy(username, name);
			break;
		case RECONNECTION:
			printf("Registrazione avvenuta con successo\n");
			username = (char *)malloc(strlen(name) + 1);
			strcpy(username, name);
			//ricezione dei messaggi offline
			receive_multiple_msg();
			break;
		default:
			printf("Errore generico\n");
			printf("Impossibile registrarsi sul server\n");
	}


}

/*Funzione che elimina tutte le strutture relative all'utente, il server chiude il relativo socket*/
void deregister(){
	int response = receive_response_msg();

	switch(response){
		case UNREGISTERED:
			printf("Non sei registrato, per uscire digita \"!quit\"\n");
			break;
		case SUCCESS:
			printf("Deregistrazione avvenuta con successo\n");
			free_user_struct(1);	//dealloco l'username
			rm_sock(1);			//chiudo il socket tcp e udp in uscita
			break;
		default:
			printf("Errore generico\n");
			printf("Impossibile deregistrarsi\n");
	}
}

//Richede al server e stampa una lista degli utenti registrati con il relativo stato
void who(){
	receive_multiple_msg();
}


/*Funzione che invia un messaggio a un destinatario (receiver). Non considero un errore il fatto che un utente inizi una chat con se stesso perchè nella maggior parte delle applicazioni di messaggistica è consentito*/
void send_client(char *receiver){
	int ret;
	char *ip, *port;
	int response = receive_response_msg();

	switch(response){
		case RECEIVER_NOT_FOUND:
			printf("Impossibile connettersi a %s: utente inesistente.\n", receiver);
			break;
		case UNREGISTERED:
			printf("Registrati per connetterti con gli altri utenti!\n");
			break;
		case RECEIVER_OFFLINE:
			printf("%s è offline, digitare il messaggio, verrà inviato appena tornerà online\n", receiver);
			get_user_msg(0);		//prende un messaggio da tastiera e lo mette nel buffer
			send_TCP_msg(buffer);	//mando il messaggio offline al server
			printf("Messaggio offline inviato.\n");
			break;
		case RECEIVER_ONLINE:
			receive_TCP_msg();		//ricevo porta e ip del destinatario
			port = strtok(buffer, DELIMITER);	//divido ip e porta
			ip = strtok(NULL, DELIMITER);

			//connessione UDP
			ret = create_dest_sock(atoi(port), ip);	//creo socket per invio udp
			if(ret == -1){
				printf("Impossibile connettersi all'utente %s\n", receiver);
				return;
			}

			get_user_msg(1);		//prende un messaggio da tastiera e lo mette nel buffer
			send_UDP_msg(buffer);	//mando il messaggio online al server
			printf("Messaggio istantaneo inviato.\n");
			break;
		default:
			printf("Errore generico\n");
			printf("Impossibile inviare il messaggio\n");
	}
}

/*Funzione che scollega il client eliminando le sue stutture dinamiche, il server chiude il relativo socket*/
void quit(){
	free_user_struct(0);
	rm_sock(0);	//chiude tutti i socket
	printf("Utente disconnesso\n");
	exit(0);
}


//Funzione che mostra un elenco dei comandi disponibili
void help(){
	printf("Sono disponibili i seguenti comandi:\n");
	printf("!help --> mostra l'elenco dei comandi disponibili\n");
	printf("!register username --> registra il client presso il server\n");
	printf("!deregister --> de-registra il client presso il server\n");
	printf("!who --> mostra l'elenco degli utenti disponibili\n");
	printf("!send username --> invia un messaggio ad un altro utente\n");
	printf("!quit --> disconnette il client dal server ed esce\n\n");
}



//-------------------------GESTIONE DEGLI INPUT DA TASTIERA---------------------

/*Funzione che gestisce i comandi da tastiera separandoli dagli eventali argomenti ed inviando il relativo codice (concatenato con l'argomento) al server */
void command_handler(){
//variabili d'appoggio per interpretare i comandi
	char *command;
	char *args;
	char scan[CMD_DIM];
	int cmd_len;

	while(1){
err:		print_prompt();

		fgets(scan, CMD_DIM, stdin);
		//evita crash se l'utente preme solo caratteri di tabulazione
		if(strcmp(scan, "\n") == 0 || strcmp(scan, " \t") == 0 || strcmp(scan, "\t\n") == 0)
			goto err;

		command = strtok(scan, DELIMITER);	//separo il comando
		cmd_len = strlen(command) + 1;	//controllo anche \0

		if(strncmp(command, "!register\0", cmd_len) == 0){
			args = strtok(NULL, DELIMITER);
			if(args == NULL){
				printf("Il comando corretto è: \"!register <username>\"\n");
			} else {
				send_TCP_msg(pack_server_msg(REGISTER, args));
				register_client(args);
			}
		} else if(strncmp(command, "!deregister\0", cmd_len) == 0){
			send_TCP_msg(pack_server_msg(DEREGISTER, " "));
			deregister();
		} else if(strncmp(command, "!who\0", cmd_len) == 0){
			send_TCP_msg(pack_server_msg(WHO, " "));
			who();
		} else if(strncmp(command, "!send\0", cmd_len) == 0){
			args = strtok(NULL, DELIMITER);
			if(args == NULL){
				printf("Il comando corretto è: \"!send <username>\"\n");
			} else {
				send_TCP_msg(pack_server_msg(SEND, args));
				send_client(args);
			}
		} else if(strncmp(command, "!quit\0", cmd_len) == 0){
			send_TCP_msg(pack_server_msg(QUIT, " "));
			quit();
		} else if(strncmp(command, "!help\0", cmd_len) == 0){
			help();
		} else {
			printf("%s\n", "Comando inesistente");
			printf("Per l'elenco dei comandi digitare \"!help\"\n");
		}
	}

}






//-----------------------------------------MAIN-----------------------------
int main(int argc, char* argv[]) {
	pthread_t keyboard_thread;
	pthread_t udp_thread;

	//CONTROLLO PARAMETRI
	check_args(argc,argv);
	//INIZIALIZZAZIONE VARIABILI GLOBALI
	struct_ini(argv);
	//CONNESSIONE CON IL SERVER
	connection_set();
	//STAMPA A VIDEO DEI COMANDI DISPONIBILI
	help();

	//Creazione di due thread, uno per la ricezione dei messaggi istantanei e uno per l' acquisizione dei comandi da tastiera
	void *udp_code(){
		while(1){
			if(receive_UDP_msg() == 0 ){
				printf("\n%s\n", buffer);
				print_prompt();
				fflush(stdout);
			}
		}
		return NULL;
	}

	void *keyboard_code(){
		command_handler();
		return NULL;
	}

	if(pthread_create(&keyboard_thread, NULL, keyboard_code, NULL) < 0){
		perror("Impossibile gestire i comandi da tastiera");
		exit(1);
	}
	if(pthread_create(&udp_thread, NULL, udp_code, NULL) < 0){
		printf("Impossibile stampare i messaggi istantanei");
		exit(1);
	}

	pthread_join(keyboard_thread, NULL);
	pthread_join(udp_thread, NULL);

	return 0;
}






//Marilisa Lippini
