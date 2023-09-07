ARCHIVIO.C
La funzione archivio

SERVER.PY
Utilizzo logging.basicConfig per definire il file di log e il formato di scrittura dei vari accessi. Definisco 3 funzioni principali: la funzione main si occupa di controllare i parametri d'ingresso, in particolar modo il numero dei threads, apre le pipe capolet e caposc e lancia il programma archivio.c(eventualmente con valgrind se la flag -v è specificata)
Inizializzo il socket ed eseguo il binding dell'host e della porta e il server si mette in attesa di connessioni da parte dei client. La classe ThreadPoolExecutor si occupa di gestire i client in multithreading.

La funzione gestisci_connessione prende come parametri la connessione, l'indirizzo del client e le due pipe. Dopo aver eseguito il controllo sul tipo di client si prepara a ricevere i dati dai client e scriverli nelle rispettive pipe.

La funzione recv_all è una funzione di sistema utilizzata per ricevere in maniera sicura e senza mancati invii, pacchetti di informazioni tra cliente e server

CLIENT1.C
Il client 1, che effettua una connessione di tipo A con il server, legge dal file fornito da riga di comando(utilizzando una getline) e dopo aver aperto il socket e aver stabilito la connessione col server invia riga per riga i dati al server. in modo particolare passo alla getline 3 parametri: un puntatore a carattere, che conterrà la linea letta dal file, la lunghezza della riga letta e il file da cui leggere. La getline è la condizione del ciclo while e ad ogni iterazione genero un nuovo socket

CLIENT2.PY
Il client 2, crea una connessione di tipo B per ogni file passato dalla riga di comando. Legge l'intero file e lo invia interamente con una sendall al server. Prima di terminare riceve il numero di sequenze per ogni socket creato


