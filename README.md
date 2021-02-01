# Messaggistica
Progetto di Reti Informatiche UNIPI 2017/2018

Questo è un progetto didattico sviluppato senza particolare attenzione alla sicurezza o alla gestione degli errori, non è pertanto destinato alla distribuzione.
Può essere liberamente utilizzato in ambito didattico.



COMPILAZIONE



Questo progetto è stato creato per essere compilato ed eseguito su una macchina virtuale con una versione del sistema operativo Debian fornita con il materiale didattico del corso di Reti Informatiche, corso di laurea di Ingegneria Informatica a Pisa UNIPI (http://www2.ing.unipi.it/~a008149/corsi/reti/). 
Può essere testato su sistemi UNIX.



*Compilazone manuale da terminale

1)Portarsi nella cartella contenente il file msg_server.c (cd path-to-github/Messaggistica/server/)

2)gcc msg_server.c -o msg_server.out

3)./msg_server.out <porta> (la porta può essere scelta, es: ./msg_server.out 1234)


4)Portarsi nella cartella contenente il file msg_client.c (cd path-to-github/Messaggistica/client/)

5)gcc msg_client.c -o msg_client.out

6)./msg_client.out <ip client> <porta client> <ip server> <porta server>
 (ottenere l'ip della macchina con il comando ifconfig, scegliere la porta del client ed inserire la porta utilizzata in precedenza per avviare il server
  es: ./msg_client.out 10.0.2.15 5555 10.0.2.15 1234)



*Uso makefile

1)Portarsi nella cartella contenente il file msg_server.c (cd path-to-github/Messaggistica/server/)

2)make all

3)make run

4)Portarsi nella cartella contenente il file msg_client.c (cd path-to-github/Messaggistica/client/)

5)make all

6)make run

Qualora si scelga di utilizzare il make file sarà necessario modificare quello relativo al client sostituendo l'ip corrente a 10.0.2.15

