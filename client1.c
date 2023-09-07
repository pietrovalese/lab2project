#define _GNU_SOURCE   
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h> //libreria per la manipolazione di indirizzi ip

#define HOST "127.0.0.1"
#define PORT 58888

ssize_t writen(int fd, void *ptr, size_t n);

void termina(const char *messaggio) {
  if(errno==0)  fprintf(stderr,"== %d == %s\n",getpid(), messaggio);
  else fprintf(stderr,"== %d == %s: %s\n",getpid(), messaggio,
              strerror(errno));
  exit(1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Utilizzo: %s <indirizzo_server> <porta_server> <file_da_inviare>\n", argv[0]);
        exit(1);
    }
    const char *file_name = argv[1];
    // Apertura del file da inviare
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
        perror("Errore nell'apertura del file");
        exit(1);
    }

    char *line = NULL; //riga appena letta con getline()
    size_t line_len = 0; //lunghezza riga letta
    ssize_t bytes_read; //numero di byte letti con getline()

    while((bytes_read = getline(&line, &line_len, file)) != -1){
        assert(bytes_read<2048);
        int fd_skt = 0;      // file descriptor associato al socket
        struct sockaddr_in serv_addr;
        // crea socket
        if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
            termina("Errore creazione socket");
        // assegna indirizzo
        serv_addr.sin_family = AF_INET;
        // il numero della porta deve essere convertito in network order 
        serv_addr.sin_port = htons(PORT);
        serv_addr.sin_addr.s_addr = inet_addr(HOST);
        // apre connessione
        if (connect(fd_skt, &serv_addr, sizeof(serv_addr)) < 0) 
            termina("Errore apertura connessione");
        
        int buff[4]={0};
        send(fd_skt, buff, 4, 0);
        int tmp_line = htonl(bytes_read);
        writen(fd_skt, &tmp_line, sizeof(tmp_line));
        if (writen(fd_skt, line, bytes_read) <0) {
                perror("Errore nell'invio dei dati al server");
                free(line);
                fclose(file);
                close(fd_skt);
                exit(1);
            }
        free(line);
        line=NULL;
        line_len=0;
        close(fd_skt);
    }
    printf("Dati inviati con successo al server.\n");

    // Liberazione della memoria e chiusura del file e del socket
    fclose(file);
    return 0;
}

ssize_t writen(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nwritten;
 
   nleft = n;
   while (nleft > 0) {
     if((nwritten = write(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount written so far */
     } else if (nwritten == 0) break; 
     nleft -= nwritten;
     ptr  += nwritten;
   }
   return(n - nleft); /* return >= 0 */
}