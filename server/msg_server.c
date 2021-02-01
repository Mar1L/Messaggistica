#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <malloc.h>

#define BUF_SIZE			1024
#define BACKLOG 			20	//numero di client che select può gestire contemporaneamete
#define DELIMITER			" \n"


//USATE PER INTERPRETARE I COMANDI RICEVUTI DAL CLIENT
#define HELP				'h'	
#define WHO					'w'
#define QUIT				'q'
#define REGISTER			'r'
#define DEREGISTER			'd'
#define SEND				's'

//USATE PER L'INVIO DI MESSAGGI DI ERRORE AL CLIENT
#define SUCCESS				"1"
#define RECONNECTION		"2"
#define REGISTERED_ALREADY	"5"
#define UNREGISTERED		"7"
#define ALREADY_IN_USE		"9"
#define RECEIVER_NOT_FOUND	"15"
#define RECEIVER_ONLINE		"17"
#define RECEIVER_OFFLINE	"19"
#define GENERIC_ERROR		"25"


fd_set master;				//set principale
fd_set read_fds;			//set di lettura
int fdmax;					//numero massimo di descrittori


//VARIABILI GLOBALI
char buffer[BUF_SIZE];		//buffer globale usato per la ricezione dei comandi dal client
uint16_t server_port;		//porta su cui il server ascolta




//--------------------------------STRUTTURE---------------------------------
typedef enum {ONLINE, OFFLINE} connection;

// MESSAGGI OFFLINE
struct message{
	char *sender;
	char *buffer;
	struct message *next;
};
	
// CLIENT
struct client {
	char *nickname;
	char *ip;
	uint16_t UDP_port;
	int socket;
	connection state;
	struct message *msg_list;
	struct client *next;
};

// Puntatore alla testa della lista dei client
struct client *head;





//-----------------------FUNZIONI PER LA COMUNICAZIONE-----------------------
void quit(int sd);
//Funzione che invia un messaggio TCP all'utente collegato sul soket sd
void send_TCP_msg(int sd, char* msg){
	int sent;
	int size = sizeof(uint16_t);
	int len = strlen(msg) + 1;		//voglio inviare anche \0
	uint16_t msg_len = htons(len);	//converto in formato network prima dell'invio
		
	sent = send(sd, (void *)&msg_len, size, 0);
	if(sent == -1 || sent < size){
		perror("Trasferimento dimensione incompleto");
		return;
	}
	sent = send(sd, (void *)msg, len, 0);
	if(sent == -1 || sent < len){
		perror("Trasferimento incompleto");
		return;
	}
}

//Funzione che riceve un messaggio TCP dall'utente collegato sul soket sd
int receive_TCP_msg(int sd){
	int received;
	int len;
	uint16_t msg_len;
	
	memset(&buffer, '\0', BUF_SIZE);
	
	received = recv(sd, (void *)&msg_len, sizeof(uint16_t), 0); 
	if(received < 0){
		printf("Errore ricezione dimensione messaggio TCP\n");
		return -1;
	} else if(received == 0){
		quit(sd);
		return -1;
	}	
	len = ntohs(msg_len);	//riconverto in formato host
	
	received = recv(sd, (void *)buffer, len, 0); 
	if(received < 0){
		printf("Errore ricezione messaggio TCP\n");
		return -1;
	} else if (received == 0){
		quit(sd);
		return -1;
	}
	return 0;
}

//FUNZIONI PER I MESSAGGI DI ERRORE
void send_response_msg(int sd, char* code){
	send_TCP_msg(sd, code);
}





//----------------------------FUNZIONI DI UTILITA'---------------------------

//------------------FUNZIONI PER LA GESTIONE DEI CLIENT----------------------
// Funzione ausiliaria che alloca un client e ne inizializza i campi
struct client* create_client(int socket, char* name, uint16_t port, char* ip){
	struct client *new_client = (struct client*)malloc(sizeof(struct client));
	
	if(!new_client){
		//gestione memoria piena
		printf("Memoria esaurita\n");
		return NULL;
	}
	new_client->nickname = (char*)malloc(strlen(name) + 1);
	new_client->ip = (char*)malloc(strlen(ip) + 1);
	strcpy(new_client->nickname, name);
	strcpy(new_client->ip, ip);
	new_client->UDP_port = port;
	new_client->socket = socket;	
	new_client->state = ONLINE;
	new_client->msg_list = NULL;	//lista messaggi offline

	return new_client;
}

/*Chiama la funzione client_create, poi inserisce il client appena creato nella lista in ordine alfabetico di nome*/
struct client* insert_client(int socket, char* name, uint16_t port, char* ip){
	struct client *p, *q;
	struct client *new = create_client(socket, name, port, ip);
	if(!new)
		return NULL;
	
	for(q = NULL, p = head; p && (strcmp(p->nickname, new->nickname) < 0); p = p->next)
		q = p;
	//inserimento in testa
	if(p == head){		
		new->next = head;
		head = new;
	//inserimento al centro
	} else {		
		q->next = new;
		new->next = p;
	}
	return new;
}

//Funzione che estrae un client dalla lista e ne restituisce un puntatore perchè venga deallocato
struct client* remove_client(int sd){
	struct client *p, *q;
	
	//lista vuota, il client non verrà trovato
	if(head == NULL)	
		return NULL;
		
	for(q = NULL, p = head; p && (p->socket != sd); p = p->next)
		q = p;
	//estrazione dalla testa
	if(p == head){		
		head = head->next;
	//estrazione dal centro
	} else if(p) {		
		q->next = p->next;
	}
	return p;
}

void delete_client(struct client* cl){
	free(cl->nickname);
	free(cl->ip);
	free(cl);
}

//Funzione che assegna lo stato con all'utente
void set_client_state(struct client *cl, connection con){
	cl->state = con;
}

//Funzione che cerca un client in base al socket e ne restituisce un puntatore
struct client* find_client_by_socket(int sd){
	struct client *p;
	for(p = head; p && (p->socket != sd); p = p->next);

	return p;
}

//Funzione che restituisce un puntatore al un client del quale si fornisce l'username  
struct client* find_client_by_name(char *name){
	//ricerca client nella lista
	struct client *p;
	for(p = head; p && (strcmp(p->nickname, name) != 0); p = p->next);
	
	return p;
}

/*Funzione che prepara il client per la riconnessione copiando nel corrispondente elemento delle lista ip, porta e socket*/
void client_reconnection(int sd, struct client *cl, uint16_t port, char *ip){
	cl->ip = (char *)malloc(strlen(ip) + 1);
	strcpy(cl->ip, ip);
	cl->UDP_port = port;
	cl->socket = sd;	
}

/*Funzione che mette nel buffer porta UDP e ip del client destinatario, il risultato verrà inviato al mittente perchè possa iniziare una connessione UDP*/
void prepare_udp_ini(struct client *receiver){
	memset(&buffer, '\0', BUF_SIZE);
	sprintf(buffer, "%u", receiver->UDP_port);
	strcat(buffer, " ");
	strcat(buffer, receiver->ip);
}



//---------------------FUNZIONI PER I MESSAGGI-------------------------
/*Crea un messaggio contenente il nome del mittente */
struct message* create_msg(struct client *sender){	
	struct message *new = (struct message *)malloc(sizeof(struct message));
	
	new->buffer = (char *)malloc(strlen(buffer) + 1);
	new->sender = (char *)malloc(strlen(sender->nickname) + 1);
	
	strcpy(new->buffer, buffer);			//copio il messaggio
	strcpy(new->sender, sender->nickname);	//copio il mittente
	
	return new;
}

//Funzione che inserisce un messaggio creato con create_msg nella lista messaggi dell'utente destinatario
void insert_msg(struct client *sender, struct client *receiver){
	struct message *p;
	struct message *m = create_msg(sender);
		
	m->next = NULL;
	
	if(receiver->msg_list == NULL)
		receiver->msg_list = m;
	else {
		for(p = receiver->msg_list; p->next; p = p->next);

		p->next = m;
	}
	printf("Inserimento messaggio offline da %s a %s\n", sender->nickname, receiver->nickname);
}




//Funzione che invia tutti i messaggi offline all'utente che si riconnette
void send_msg(int sd, struct client *receiver){
	struct message *aux;	//lo uso per mettere il messaggio nel buffer per poi mandarlo
	struct message *delete;	//lo uso per eliminare il messaggio dopo averlo mandato

		//creo una stringa per ogni messaggio offline da mandare al client
		for(aux = receiver->msg_list; aux != NULL;){	
			memset(&buffer, '\0', BUF_SIZE);
			
			strcat(buffer, aux->sender);
			strcat(buffer, "(msg offline)>\n");
			strcat(buffer, aux->buffer);
			receiver->msg_list = aux->next;
			
			//mando il messaggio al client
			send_TCP_msg(sd, buffer);			
			
			//cancello il messaggio dopo averlo mandato
			delete = aux;
			aux = aux->next;
			free(delete->sender);
			free(delete->buffer);
			free(delete);
		}
		send_TCP_msg(sd, "stop\0");	
}


//FUNZIONI AUSILIARIE
int check_port(int port){
//[0-1023] well-known ports, [1024-49151] registered, [49152-65535] private 
	if(port >= 1024 && port <= 65535)
		return 0;
	else
		return -1;
}

void check_args(int argc, char *argv[]){
	int ret;
	if(argc != 2){
		printf("Numero di parametri errato! Il parametro necessario è: <porta server>\n");
		exit(-1);
	} 
	ret = check_port(atoi(argv[1]));
	if(ret == -1){
		printf("La porta del server non è valida\n");
		exit(-1);
	}
}






//---------------------------------SERVER TASKS-----------------------------
void rm_sock(int sd);

//Funzione che registra un utente presso il server
void register_client(int sd, char *username){
	struct client* aux;
	char *port;
	char *ip;
	char *name = (char *)malloc(strlen(username) + 1);	//variabile di appoggio per il nome
	strcpy(name, username);
	
	//salvataggio IP e porta client in variabili locali
	receive_TCP_msg(sd);
	port = strtok(buffer, DELIMITER);
	ip = strtok(NULL, DELIMITER);	
	
	aux = find_client_by_socket(sd);//controllo se esiste già un client su quel socket
	if(aux){	//l'utente si è registrato ed è attualmente loggato con un altro nome
		send_response_msg(sd, REGISTERED_ALREADY);
		send_response_msg(sd, aux->nickname);	//mando all'utente il nome con cui è registrato
	} else {
		aux = find_client_by_name(name);	//controllo se esiste già un client con quel nome	
		if (aux == NULL){	//nuovo client da registrare
			aux = insert_client(sd, name, atoi(port), ip);
			if(!aux){	//la creazione dell'utente è fallita
				printf("Non è stato possibile creare il client: memoria esaurita\n");
				send_response_msg(sd, GENERIC_ERROR);	//no_memory
			} else {
				send_response_msg(sd, SUCCESS);	//creato nuovo client
				printf("Registrazione utente %s; ip: %s porta UDP: %d socket: %d\n", aux->nickname, aux->ip, aux->UDP_port, aux->socket);
			}
		} else if(aux->state == ONLINE){//il nuovo client deve scegliere un nickname disponibile
			send_response_msg(sd, ALREADY_IN_USE);
			printf("Utente %s; ip: %s porta UDP: %d socket: %d non registrato, nome già in uso\n", aux->nickname, aux->ip, aux->UDP_port, aux->socket);
		} else {				
		//client registrato offline, riconnessione
			set_client_state(aux, ONLINE);
			client_reconnection(sd, aux, atoi(port), ip);
			send_response_msg(sd, RECONNECTION);
			printf("Riconnessione utente %s; ip: %s porta UDP: %d socket: %d\n", aux->nickname, aux->ip, aux->UDP_port, aux->socket);
		
			//invio messaggi offline
			send_msg(sd, aux);
			printf("Invio messaggi offline a %s\n", name);
		}
	}
	
	free(name);
}

//Funzione che deregistra un utente presso il server cancellando le relative stutture dati
void deregister(int sd){
	struct client *aux = remove_client(sd);	//rimuove il client dalla lista e lo restituisce
	
	//controllo che il client sia registrato
	if(!aux){	
		printf("%s\n", "Impossibile deregistrare: utente non registrato\n");
		send_response_msg(sd, UNREGISTERED);
		return;
	}
	printf("Deregistrazione utente %s, ip: %s porta UDP: %d socket: %d ", aux->nickname, aux->ip, aux->UDP_port, aux->socket);
	//dealloco strutture dinamiche del client, se non era registrato chudo solo il socket
	delete_client(aux);
	send_response_msg(sd, SUCCESS);
	
	printf("avvenuta con successo\n");
}

//Funzione che invia un elenco degli utenti registrati
void who(int sd){
	struct client *work = head;
	if(!work){
		strcpy(buffer, "Non ci sono utenti attualmente registrati\0");
		send_TCP_msg(sd, buffer);
	} else {
		strcpy(buffer, "Client registrati:\0");
		send_TCP_msg(sd, buffer);
	}
	
	/*per ogni utente registrato copio sul buffer nome e stato e invio al client*/
	while (work){
		strcpy(buffer, "\t");
		strcat(buffer, work->nickname);
		(work->state == OFFLINE) ? strcat(buffer, "(offline)\0") : strcat(buffer, "(online)\0");
		
		send_TCP_msg(sd, buffer);
		work = work->next;
	}
	send_TCP_msg(sd, "stop\0");
	printf("Invio elenco utenti su socket %d\n", sd);
}

//Funzione che manda un messaggio dall'utente sul socket sd all' utente con nome rec 
void send_client(int sd, char *rec){
	//printf("Send\n");
	struct client *receiver;
	struct client *sender;
	
	//cerco il mittente tramite il socket
	sender = find_client_by_socket(sd);	
	
	//il mittente non è registrato
	if(sender == NULL){				
		send_response_msg(sd, UNREGISTERED);
		printf("Invio messaggio non riuscito, mittente non registrato\n");
		return;
	}
	
	//cerco il destinatario tramite il nome
	receiver = find_client_by_name(rec);	
	
	//il destinatario non è registrato
	if(receiver == NULL){			
		send_response_msg(sd, RECEIVER_NOT_FOUND);
		printf("Invio messaggio non riuscito, destinatario inesistente\n");
		
	//il destinatario è online
	} else if(receiver->state == ONLINE){
		send_response_msg(sd, RECEIVER_ONLINE);
		//mando ip e porta del destinatario
		prepare_udp_ini(receiver);
		send_TCP_msg(sd, buffer);
		printf("Invio messaggio istantaneo da %s a %s\n", sender->nickname, receiver->nickname);
	//il destinatario è offline	
	} else {
		send_response_msg(sd, RECEIVER_OFFLINE);	//mando messaggio di conferma o errore
		receive_TCP_msg(sd);		//ricevo il messaggio
		insert_msg(sender, receiver);	//inserisco il messaggio nella lista
	}
}

//Funzione che disconnette 'utente sul socket sd
void quit(int sd){
	struct client *aux = find_client_by_socket(sd);
	if(!aux){	//l'utente si disconnette senza essersi registrato
		printf("Disconnessione utente non registrato\n");
		rm_sock(sd);
		return;
	} else {
		set_client_state(aux, OFFLINE);
		//cancello socket, ip e porta
		free(aux->ip);
		aux->UDP_port = -1;	//assegno un valore non valido
		aux->socket = -1;
		rm_sock(sd);
		
		printf("Disconnessione utente %s\n", aux->nickname);
	}
}



//---------------------GESTIONE DEI COMANDI DA TASTIERA---------------------
//Funzione di elaborazione dei comandi inviati dal client; il comando è salvato nel buffer 
void command_handler(int sd){
	char *command;
	char *args;
	char cmd;
	
	command = strtok(buffer, DELIMITER);
	cmd = command[0];		//conversione in carattere
		
	switch(cmd){
		case WHO:
			who(sd);
			break;
		case QUIT:
			quit(sd);
			break;
		case REGISTER:
			args = strtok(NULL, DELIMITER);
			register_client(sd, args);
			break;
		case DEREGISTER:
			deregister(sd);
			break;
		case SEND:
			args = strtok(NULL, DELIMITER);
			send_client(sd, args);
	}	
}







//-------------------------FUNZIONI PER LA CONNESSIONE----------------------
/*vFunzione che chiude il socket e lo rimuove dal set master quando un client si disconnette*/
void rm_sock(int sd){
	close(sd);
	FD_CLR(sd, &master);
}

/*Funzione che salva la porta TCP, inizializza la lista di client, pulisce il buffer e azzera i set per select*/
void server_ini(char *argv[]){
	server_port = atoi(argv[1]); //porta su cui il server è in ascolto
	
	head = NULL;
	
	memset(&buffer, '\0', BUF_SIZE);

	FD_ZERO(&master);
	FD_ZERO(&read_fds);
}

//Funzione che gestisce la connessione con i client tramite la funzone select
void connection_ini(){ 
	int ret, active_sd;
	socklen_t len;
	int listening_sd;
	int connecting_sd;
	
	struct sockaddr_in server_addr, client_addr;
	
	listening_sd = socket(AF_INET, SOCK_STREAM, 0);
	if(listening_sd < 0){
		perror("Errore nella creazione del socket");
		exit(-1);
	}
	printf("\nCreazione socket %d\n", listening_sd);

	//inizializzazione indirzzo server
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	//binding
	ret = bind(listening_sd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if(ret < 0){
		perror("Errore in fase di bind");
		exit(-1);
	}
	printf("Bind su socket %d effettuata\n", listening_sd);

	//metto il socket in ascolto
	ret = listen(listening_sd, BACKLOG);
	if(ret < 0){
		perror("Errore in ascolto");
		exit(-1);
	}
	printf("Socket %d in ascolto\n", listening_sd);

	//inizializzo strutture select
	FD_SET(listening_sd, &master);
	fdmax = listening_sd;
	
	while(1){
		read_fds = master;
		select(fdmax + 1, &read_fds, NULL, NULL, NULL);
		for(active_sd = 0; active_sd <= fdmax; active_sd++){
			//controllo se i descrittori in lettura sono settati
			if(FD_ISSET(active_sd, &read_fds)){
				//se è in ascolto un client ha fatto richiesta di connessione
				if(active_sd == listening_sd){
					len = sizeof(client_addr);	
					connecting_sd = accept(listening_sd, (struct sockaddr *)&client_addr, &len);	
					if(connecting_sd == -1){
						perror("Errore, impossibile acettare nuova connessione");
					}
					printf("Attesa connessione su socket %d\n", connecting_sd);
					//aggiungo nuovo descrittore nel set
					FD_SET(connecting_sd, &master);
					if(connecting_sd > fdmax)
						fdmax = connecting_sd;
	
					} else {
					//ricezione messaggio
					ret = receive_TCP_msg(active_sd);
					if(ret < 0){
						continue;
					} else {
						//gestione comandi ricevuti dal client
						command_handler(active_sd);
					}			
				}
			}
		}	
	}
}





//---------------------------------MAIN-----------------------------
int main(int argc, char *argv[]){
	
	//CONTROLLO PARAMETRI
    check_args(argc, argv);
    //INIZIALIZZAZIONI
    server_ini(argv);
    	
	//creazione socket e gestione indirizzo
	connection_ini();
	//accetta la connessione
	//connection_handler(sd, connecting_sd, client_addr);
	return 0;
}




//Marilisa Lippini
