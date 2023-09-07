#define _GNU_SOURCE  
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa/numero exit() etc ...
#include <stdbool.h>  // gestisce tipo bool (variabili booleane)
#include <string.h>   // confronto/copia/etc di stringhe
#include <errno.h>
#include <search.h>
#include <assert.h>   // permette di usare la funzione assert
#include <signal.h>
#include <unistd.h>  // per sleep 
#include <math.h> 
#include "xerrori.h"
 
#define QUI __LINE__,__FILE__
#define Num_elem 1000000 // dimensione della tabella hash 
#define PC_buffer_len 10 // lunghezza dei buffer produttori/consumatori

//VARIABILI GLOBALI
int stringhe_distinte=0; //variabile globale per tenere traccia delle stringhe distinte nella hash table

//STRUCT
typedef struct{ //struct per gestire i lettori/scrittori, favorendo i lettori
	int readers;
	bool write_enable;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	}read_write_;

typedef struct{ //struct contenente dati del capo scrittore
	char **buffer;
	int nr_scrittori; 
	sem_t *sem_free_slots;
	sem_t *sem_data_items;
	}caposcrittore_struct;

typedef struct{ //struct per i dati dei thread scrittori
	char **buffer;
	int *sc_dati; //variabile condivisa indice del buffer prod/cons
	sem_t *sem_free_slots;
	sem_t *sem_data_items;
	pthread_mutex_t *mutex;
	read_write_ *read_write; //per sincronizzare reader/writer
	}scrittori_struct;

typedef struct{ //struct dati capo lettore
	char **buffer;
	int nr_lettori;
	sem_t *sem_free_slots;
	sem_t *sem_data_items;
	}capolettore_struct;

typedef struct{ //struct per i dati dei thread lettori
	sem_t *sem_free_slots;
	sem_t *sem_data_items;
	char **buffer;
	pthread_mutex_t *mutex;
	int *let_dati; 
	read_write_ *read_write;
	FILE *f;
	pthread_mutex_t *mutex_file;
	}lettori_struct;

typedef struct{ //struct per il gestore dei segali
	int nr_lettori;
	int nr_scrittori;
	pthread_t *capo_sc;
	pthread_t *capo_let;
	pthread_t *array_sc;
	pthread_t *array_let;
	}signal_struct;

typedef struct{
	int valore;
	ENTRY *next;
}coppia;

//PROTOTIPI
void elimina_tabella();
void distruggi_entry(ENTRY *e);
ENTRY *crea_entry(char *s, int n);
void aggiungi(char *s);
int conta(char *s);
void read_write__init(read_write_ *z);
void read_write__destroy(read_write_ *z);
void read_lock(read_write_ *z);
void read_unlock(read_write_ *z);
void write_lock(read_write_ *z);
void write_unlock(read_write_ *z);
ssize_t readn(int fd, void *ptr, size_t n);
void *fun_caposcrittore(void *v);
void *scrittori(void *v);
void *capo_lettore(void *v);
void *lettori(void *v);
void *signal_body(void *v);

ENTRY *testa_lista_entry = NULL;
	

int main(int argc, char *argv[]){
	
	if(argc!=3) {
    printf("Uso\n\t%s num_w num_r\n", argv[0]);
    exit(1);
  }
  
	//creo tabella hash
	int ht=hcreate(Num_elem);
	if (ht==0) xtermina("Errore creazione hash table",QUI);
	
	sigset_t mask;
	sigfillset(&mask);	//maschera in cui ci sono tutti i segnali che andrò poi a bloccare
	pthread_sigmask(SIG_BLOCK,&mask,NULL);//blocco tutti i segnali
	//inizializzazione read_write_ per sincronizzare lettori/scrittori
	read_write_ read_write;
	read_write__init(&read_write);
	
    char *buffer_scrittori[PC_buffer_len]; //buffer prod-cons per gli scrittori
    int nr_scrittori=atoi(argv[1]);
    assert(nr_scrittori>0);
     //creo e inizializzo semafori per scrittori
	sem_t sem_free_slots_sc, sem_data_items_sc;
	xsem_init(&sem_free_slots_sc,0,PC_buffer_len,QUI);
	xsem_init(&sem_data_items_sc,0,0,QUI);
	//inizializzo caposc e i suoi dati
	caposcrittore_struct dati_caposcrittore;
	pthread_t caposc;
	dati_caposcrittore.buffer=buffer_scrittori;
	dati_caposcrittore.nr_scrittori=nr_scrittori;
	dati_caposcrittore.sem_free_slots=&sem_free_slots_sc;
	dati_caposcrittore.sem_data_items=&sem_data_items_sc;
	xpthread_create(&caposc,NULL,&fun_caposcrittore,&dati_caposcrittore,QUI);//lancio capo scrittore
	
	//inizializzo scrittori e i loro dati
	pthread_mutex_t mutex_sc = PTHREAD_MUTEX_INITIALIZER;//mutex per accesso al buffer prod/cons
	scrittori_struct scrittori_array_struct[nr_scrittori]; //array di scrittori_struct
	pthread_t array_scrittori[nr_scrittori];  //array di scrittori
	int sc_dati=0;
	for(int i=0;i<nr_scrittori;i++){
		scrittori_array_struct[i].buffer=buffer_scrittori;
		scrittori_array_struct[i].sc_dati=&sc_dati;
		scrittori_array_struct[i].sem_free_slots=&sem_free_slots_sc;
		scrittori_array_struct[i].sem_data_items=&sem_data_items_sc;
		scrittori_array_struct[i].read_write=&read_write;
		scrittori_array_struct[i].mutex=&mutex_sc;
		xpthread_create(&array_scrittori[i],NULL,&scrittori,&scrittori_array_struct[i],QUI);//lancio scrittori
		}
	
	char *buffer_lettori[PC_buffer_len]; //buffer prod-cons per i lettori
    //creo e inizializzo semafori per lettori
	sem_t sem_free_slots_let, sem_data_items_let;
	xsem_init(&sem_free_slots_let,0,PC_buffer_len,QUI);
	xsem_init(&sem_data_items_let,0,0,QUI);
	//inizializzo capolet e i suoi dati
	capolettore_struct d_capolet;
	pthread_t capolet;
	int nr_lettori=atoi(argv[2]);	
	assert(nr_lettori>0);
	d_capolet.buffer=buffer_lettori;
	d_capolet.nr_lettori=nr_lettori;
	d_capolet.sem_free_slots=&sem_free_slots_let;
	d_capolet.sem_data_items=&sem_data_items_let;
	xpthread_create(&capolet,NULL,&capo_lettore,&d_capolet,QUI);//lancio capo lettore
	
	//inizializzo lettori e i loro dati
	FILE* outfile = fopen("lettori.log","wt"); //apro lettori.log in scrittura
	if(outfile==NULL){
	xtermina("impossibile aprire outfile",QUI);
}	
	pthread_mutex_t mutex_let = PTHREAD_MUTEX_INITIALIZER;//mutex per accesso al buffer prod/cons
	pthread_mutex_t mutex_file = PTHREAD_MUTEX_INITIALIZER;//mutex per accesso in scrittura al file lettori.log
	lettori_struct lettori_array_struct[nr_lettori]; //array di lettori_struct
	pthread_t array_lettori[nr_lettori];  //array di lettori
	int let_dati=0;
	for(int i=0;i<nr_lettori;i++){
		lettori_array_struct[i].sem_free_slots=&sem_free_slots_let;
		lettori_array_struct[i].sem_data_items=&sem_data_items_let;
		lettori_array_struct[i].buffer=buffer_lettori;
		lettori_array_struct[i].let_dati=&let_dati;
		lettori_array_struct[i].read_write=&read_write;
		lettori_array_struct[i].mutex=&mutex_let;
		lettori_array_struct[i].f=outfile;
		lettori_array_struct[i].mutex_file=&mutex_file;
		xpthread_create(&array_lettori[i],NULL,&lettori,&lettori_array_struct[i],QUI);//lancio lettori
		}	
	//struttura dati per gestore dei segnali
	signal_struct dati_gestore;
	
	//creo e lancio thread gestore
	pthread_t gestore;
	dati_gestore.nr_scrittori=nr_scrittori;
	dati_gestore.nr_lettori=nr_lettori;
	dati_gestore.capo_sc=&capolet;
	dati_gestore.capo_let=&caposc;
	dati_gestore.array_sc=array_scrittori;
	dati_gestore.array_let=array_lettori;
	xpthread_create(&gestore,NULL,&signal_body,&dati_gestore,QUI);
	
	printf("== Se vuoi mandarmi dei segnali il mio pid e': %d ==\n", getpid());
	xpthread_join(gestore,NULL,QUI);//aspetto thread gestore (esce solo se c'è stato SIGTERM)
  
  //destroy di semafori, mutex, read_write_ e close del file "lettori.log"
	xsem_destroy(&sem_data_items_let,QUI);  
	xsem_destroy(&sem_free_slots_let,QUI);  
	xsem_destroy(&sem_data_items_sc,QUI);  
	xsem_destroy(&sem_free_slots_sc,QUI);  
	read_write__destroy(&read_write);
	xpthread_mutex_destroy(&mutex_sc,QUI);
	xpthread_mutex_destroy(&mutex_let,QUI);
	xpthread_mutex_destroy(&mutex_file,QUI);
	fclose(outfile);
	return 0;	
	}

//funzione per distruggere una entry della tab hash
void distruggi_entry(ENTRY *e){
	free(e->key);
	free(e->data);
	free(e);	
	}

ENTRY *crea_entry(char *s, int n) {
  ENTRY *e = malloc(sizeof(ENTRY));
  if(e==NULL) xtermina("errore malloc entry 1", QUI);
  e->key = strdup(s); 
  e->data = (int *) malloc(sizeof(int));
  if(e->key==NULL || e->data==NULL)
    xtermina("errore malloc entry 2", QUI);
  *((int *)e->data) = n;
  return e;
}

//se la stringa `s` non è contenuta nella tabella hash deve essere inserita con valore associato 
//uguale a 1. Se `s` è già contenuta nella tabella allora l'intero associato deve essere incrementato di 1.
void aggiungi(char *s){
	ENTRY *e=malloc(sizeof(ENTRY)); //alloco memoria per una entry della tabella
	if (e==NULL) xtermina("errore malloc entry in aggiungi",QUI);
	e->key=strdup(s);  //e->key prende la copia di s
	assert(e->key!=NULL);
	ENTRY *r=hsearch(*e,FIND); //controllo se la entry è gia in tabella
	if (r!=NULL){ //la entry è già in tabella
		assert(strcmp(e->key,r->key)==0);
		coppia *d=(coppia *) r->data; 
		d->valore+=1; //incremento il valore associato stringa s di 1
		distruggi_entry(e); //distruggo la entry, perchè è già in tabella
		}
	else {//la entry è nuova
		stringhe_distinte++;//incremento variabile globale delle stringhe distinte
		e->data=(coppia *) malloc(sizeof(coppia)); //creo puntatore a campo data
		if (e->data==NULL) xtermina("errore malloc entry in aggiungi",QUI);
		((coppia *)e->data)->valore=1; //associo 1 alla stringa s
		((coppia *)e->data)->next=testa_lista_entry;
		testa_lista_entry=e;
		r=hsearch(*e,ENTER); //inserisco la entry in tabella
		if (r==NULL) xtermina("tabella piena o errore",QUI);
		}
	}

//`int conta(char *s)` restituisce l'intero associato ad `s` se è contenuta nella tabella, altrimenti 0.
int conta(char *s){
	ENTRY *e=malloc(sizeof(ENTRY)); //alloco memoria per una entry della tabella
	if (e==NULL) xtermina("errore malloc entry in conta",QUI);
	e->key=strdup(s);  //e->key prende la copia di s
	assert(e->key!=NULL);
	ENTRY *r=hsearch(*e,FIND); //controllo se la entry è in tabella
	if (r!=NULL) {//s è in tabella, restituisco il valore
		assert(strcmp(e->key,r->key)==0);
		coppia *d=(coppia *) r->data; 
		distruggi_entry(e);                                    
		return d->valore;
		} 
	else {
		distruggi_entry(e);                                    
		return 0; //s non è in tabella, ritorna 0		
		}
}

//funzione per inizializzare read_write_	
void read_write__init(read_write_ *z){
	z->readers=0;
	z->write_enable=false;
	xpthread_cond_init(&z->cond,NULL,QUI);
	xpthread_mutex_init(&z->mutex,NULL,QUI);
}

//funzione per distruggere read_write_	
void read_write__destroy(read_write_ *z){
	xpthread_cond_destroy(&z->cond,QUI);
	xpthread_mutex_destroy(&z->mutex,QUI);
}
	

void read_lock(read_write_ *z){
	xpthread_mutex_lock(&z->mutex,QUI);
	while(z->write_enable==true) xpthread_cond_wait(&z->cond,&z->mutex,QUI); //attende fine scrittura se c'era qualcuno write_enable
	z->readers++;
	xpthread_mutex_unlock(&z->mutex,QUI);
}

void read_unlock(read_write_ *z){
	assert(z->readers>0);  // ci deve essere almeno un reader 
    assert(!z->write_enable);   // non ci devono essere writer 
    xpthread_mutex_lock(&z->mutex,QUI);
    z->readers--;
    if(z->readers==0) xpthread_cond_signal(&z->cond,QUI); //segnalo un waiter se nessun altro sta scrivendo
    xpthread_mutex_unlock(&z->mutex,QUI);
}

void write_lock(read_write_ *z){
	xpthread_mutex_lock(&z->mutex,QUI);
	while(z->write_enable || z->readers>0) xpthread_cond_wait(&z->cond,&z->mutex,QUI); //se ci sono lettori o scrittori mi metto in attesa
	z->write_enable=true; //un writer sta scrivendo
	xpthread_mutex_unlock(&z->mutex,QUI);
}

void write_unlock(read_write_ *z){
	assert(z->write_enable);   // ci deve essere 1 writer 
    xpthread_mutex_lock(&z->mutex,QUI);
	z->write_enable=false;
	xpthread_cond_broadcast(&z->cond,QUI);
	xpthread_mutex_unlock(&z->mutex,QUI);
}

ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}

void *fun_caposcrittore(void *v){
	caposcrittore_struct *d=(caposcrittore_struct *)v;
	puts("== capo scrittore partito ==");
	int fdati_caposcrittore=open("caposc",O_RDONLY); //apro la pipe 'caposc' in rd only
	if (fdati_caposcrittore<0) xtermina("errore apertura pipe capo scrittore",QUI);
	int caposc_ind=0; //indice del buffer prod/cons
	while(true){
		int len; //valore in cui memorizzo lunghezza sequenza
		ssize_t e=readn(fdati_caposcrittore,&len,sizeof(int)); //read della lunghezza sequenza
		if (e==0) break; 
		char *string=malloc((len+1)*sizeof(char)); //alloco memoria per la sequenza da ricevere (+1 perchè devo metterci 0 in fondo)
		e=readn(fdati_caposcrittore,string,len*sizeof(char)); //read della sequenza
		if (e==0) break; 
		assert(e!=0);
		string[len]=0;  //metto 0 in fondo alla sequenza
		char *saveptr; //puntatore per ricordarsi dove si era arrivati nella strtok_r
		char *s=strtok_r(string,".,:; \n\r\t",&saveptr);//stringa la cui copia è da memorizzare nel buffer
		while(s!=NULL){
			char *str_buf=strdup(s); //creo copia della stringa ottenuta con strtok_r		    
		    xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
			d->buffer[caposc_ind%PC_buffer_len]=str_buf; //scrivo la stringa nel buffer
			xsem_post(d->sem_data_items,QUI); //comunico che c'è un item in più
			caposc_ind++;
			s=strtok_r(NULL,".,:; \n\r\t",&saveptr);
			}
		//uscito da qui leggo la prossima sequenza, ma prima faccio la free della vecchia
		free(string); 
		}
	//uscito da qui significa che la pipe è stata chiusa in scrittura o ha finito i valori da leggere
	xclose(fdati_caposcrittore,QUI); //chiudo la pipe in lettura
	for(int i=0;i<d->nr_scrittori;i++){
	xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
	d->buffer[caposc_ind%PC_buffer_len]="-1"; //scrivo -1 (valore di terminazione) nel buffer
	xsem_post(d->sem_data_items,QUI);
	caposc_ind++;
		}
	pthread_exit(NULL);
}

void *scrittori(void *v){
	scrittori_struct *d=(scrittori_struct *)v;
	char *string;
	do{
	xsem_wait(d->sem_data_items,QUI); //mi metto in attesa che ci siano items
	xpthread_mutex_lock(d->mutex,QUI);
	string=d->buffer[*(d->sc_dati)%PC_buffer_len]; //prendo stringa dal buffer
		  *(d->sc_dati)+=1;
	xpthread_mutex_unlock(d->mutex,QUI);
    xsem_post(d->sem_free_slots,QUI);
    if (strcmp(string,"-1")==0) break; //se è il valore di terminazione usciamo dal do while
	write_lock(d->read_write);
	aggiungi(string); //aggiungo o incremento valore stringa alla tab hash
	write_unlock(d->read_write);
	free(string); 
		}while(strcmp(string,"-1")!=0);	
	pthread_exit(NULL);
}

void *capo_lettore(void *v){
	capolettore_struct *d=(capolettore_struct *)v;
	puts("== capo lettore partito ==");
	int fd_capolet=open("capolet",O_RDONLY); //apro la pipe 'capolet' in rd only
	if (fd_capolet<0) xtermina("errore apertura pipe capo lettore",QUI);
	int capolet_ind=0;//indice buffer prod/cons
	while(true){
		int len; //valore in cui memorizzo lunghezza sequenza
		ssize_t e=readn(fd_capolet,&len,sizeof(int)); //read della lunghezza sequenza
		if (e==0) break; 
		char *string=malloc((len+1)*sizeof(char));	 //alloco memoria per la sequenza da ricevere  (+1 perchè metto 0 in fondo)
		e=readn(fd_capolet,string,len*sizeof(char)); //read della sequenza
		if (e==0) break; 
		assert(e!=0);
		string[len]=0;  //metto 0 in fondo alla sequenza
		char *saveptr; //puntatore per ricordarsi dove si era arrivati nella strtok_r
		char *s=strtok_r(string,".,:; \n\r\t",&saveptr);//stringa la cui copia è da memorizzare nel buffer
		while(s!=NULL){
			char *str_buf=strdup(s); //creo copia della stringa ottenuta con strtok_r
		    xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
			d->buffer[capolet_ind%PC_buffer_len]=str_buf; //scrivo la stringa nel buffer
			xsem_post(d->sem_data_items,QUI); //comunico che c'è un item in più
			capolet_ind++;
			s=strtok_r(NULL,".,:; \n\r\t",&saveptr);
			}
		//uscito da qui leggo la prossima sequenza, ma prima faccio la free della vecchia
		free(string); 
		}
	//uscito da qui significa che la pipe è stata chiusa in scrittura o ha finito i valori da leggere
	xclose(fd_capolet,QUI); //chiudo la pipe in lettura
	for(int i=0;i<d->nr_lettori;i++){
	xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
	d->buffer[capolet_ind%PC_buffer_len]="-1"; //scrivo -1 (valore di terminazione) nel buffer
	xsem_post(d->sem_data_items,QUI);
	capolet_ind++;
		}
	pthread_exit(NULL);
}

void *lettori(void *v){
	lettori_struct *d=(lettori_struct *)v;
	char *string;
	int n;
	do{
	xsem_wait(d->sem_data_items,QUI); //mi metto in attesa che ci siano items
	xpthread_mutex_lock(d->mutex,QUI);
	string=d->buffer[*(d->let_dati)%PC_buffer_len]; //prendo stringa dal buffer
		  *(d->let_dati)+=1;
	xpthread_mutex_unlock(d->mutex,QUI);
    xsem_post(d->sem_free_slots,QUI);
    if (strcmp(string,"-1")==0) break; //se è il valore di terminazione usciamo dal do while
	read_lock(d->read_write);
	n=conta(string); //ottengo valore associato alla stringa nella tab hash
	read_unlock(d->read_write);
	//scrivo sul file lettori.log
	xpthread_mutex_lock(d->mutex_file,QUI);
	fprintf(d->f,"%s %d\n",string,n);
	fflush(d->f);	//per flushare immediatamente sul file
	xpthread_mutex_unlock(d->mutex_file,QUI);
	free(string); 
		}while(strcmp(string,"-1")!=0);	
	pthread_exit(NULL);
	}

void *signal_body(void *v){
	signal_struct *dati_gestore=(signal_struct *)v;
	sigset_t mask;
	sigfillset(&mask); //maschera con tutti i segnali, che attenderò
	int s;
	while(true){
		int e=sigwait(&mask,&s); //mi metto in attesa di un segnale
		if (e!=0) xtermina("Errore segnale", QUI);
		if (s==SIGINT){ //il segnale è SIGINT
			fprintf(stderr, "SIGINT %d stringhe distinte presenti nella tabella hash \n",stringhe_distinte);
			}
		if (s==SIGUSR1){ //il segnale è SIGUSR1
			elimina_tabella();
			hdestroy();
			hcreate(Num_elem);
			}
		if (s==SIGTERM){//segnale è SIGTERM
			//attendo termine dei capi e dei thread scrittori e lettori
			xpthread_join(*(dati_gestore->capo_sc),NULL,QUI);
			xpthread_join(*(dati_gestore->capo_let),NULL,QUI);
			for (int i=0;i<dati_gestore->nr_scrittori;i++){
				xpthread_join(dati_gestore->array_sc[i],NULL,QUI);
				}
			puts("== scrittori terminati ==");
			for (int i=0;i<dati_gestore->nr_lettori;i++){
				xpthread_join(dati_gestore->array_let[i],NULL,QUI);
				}
			puts("== lettori terminati ==");	
			fprintf(stderr, "SIGTERM %d stringhe distinte presenti nella tabella hash \n",stringhe_distinte);
			elimina_tabella();
			hdestroy(); //dealloco tab hash
			break;		//break cosi da ritornare
			}
		}
	return NULL;
}

void elimina_tabella(){
	ENTRY *nodo_successivo= NULL;
	while(testa_lista_entry!=NULL){
		free(testa_lista_entry->key);
		nodo_successivo=((coppia *)testa_lista_entry->data)->next;
		free(testa_lista_entry->data);
		free(testa_lista_entry);
		testa_lista_entry=nodo_successivo;
	}
}