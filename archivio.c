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
	char **buffer;
	int *let_dati; 
	sem_t *sem_free_slots;
	sem_t *sem_data_items;
	pthread_mutex_t *mutex;
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
void *fun_scrittori(void *v);
void *fun_capolettore(void *v);
void *fun_lettori(void *v);
void *signal_body(void *v);

ENTRY *testa_lista_entry = NULL;
	

int main(int argc, char *argv[]){
	
	if(argc!=3) {
    printf("Uso\n\t%s num_w num_r\n", argv[0]);
    exit(1);
  }
	int ht=hcreate(Num_elem);
	if (ht==0) xtermina("Errore creazione hash table",QUI);
	
	FILE* outfile = fopen("lettori.log","wt"); //apro lettori.log in scrittura
	if(outfile==NULL){
	xtermina("impossibile aprire outfile",QUI);
}	

	sigset_t mask;
	sigfillset(&mask);	//maschera con i segnali bloccati
	pthread_sigmask(SIG_BLOCK,&mask,NULL);

	//inizializzazione read_write_ per sincronizzare lettori/scrittori
	read_write_ read_write;
	read_write__init(&read_write);
	
	//--CAPO SCRITTORE--
    char *buffer_scrittori[PC_buffer_len]; //buffer prod-cons per gli scrittori
    int nr_scrittori=atoi(argv[1]);
    assert(nr_scrittori>0);
	sem_t sem_free_slots_sc, sem_data_items_sc;
	xsem_init(&sem_free_slots_sc,0,PC_buffer_len,QUI);
	xsem_init(&sem_data_items_sc,0,0,QUI);
	caposcrittore_struct dati_caposcrittore;
	pthread_t caposc;
	dati_caposcrittore.buffer=buffer_scrittori;
	dati_caposcrittore.nr_scrittori=nr_scrittori;
	dati_caposcrittore.sem_free_slots=&sem_free_slots_sc;
	dati_caposcrittore.sem_data_items=&sem_data_items_sc;
	xpthread_create(&caposc,NULL,&fun_caposcrittore,&dati_caposcrittore,QUI);//lancio capo scrittore
	
	//--SCRITTORI--
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
		xpthread_create(&array_scrittori[i],NULL,&fun_scrittori,&scrittori_array_struct[i],QUI);//lancio scrittori
		}
	
	//--CAPO LETTORE--
	char *buffer_lettori[PC_buffer_len]; //buffer prod-cons per i lettori
	sem_t sem_free_slots_let, sem_data_items_let;
	xsem_init(&sem_free_slots_let,0,PC_buffer_len,QUI);
	xsem_init(&sem_data_items_let,0,0,QUI);
	capolettore_struct dati_capolettore;
	pthread_t capolet;
	int nr_lettori=atoi(argv[2]);	
	assert(nr_lettori>0);
	dati_capolettore.buffer=buffer_lettori;
	dati_capolettore.nr_lettori=nr_lettori;
	dati_capolettore.sem_free_slots=&sem_free_slots_let;
	dati_capolettore.sem_data_items=&sem_data_items_let;
	xpthread_create(&capolet,NULL,&fun_capolettore,&dati_capolettore,QUI);//lancio capo lettore
	
	//--LETTORI--
	pthread_mutex_t mutex_let = PTHREAD_MUTEX_INITIALIZER;//mutex per accesso al buffer prod/cons
	pthread_mutex_t mutex_file = PTHREAD_MUTEX_INITIALIZER;//mutex per accesso in scrittura al file lettori.log
	lettori_struct lettori_array_struct[nr_lettori]; //array di lettori_struct
	pthread_t array_lettori[nr_lettori];  //array di lettori
	int let_dati=0;
	for(int i=0;i<nr_lettori;i++){
		lettori_array_struct[i].buffer=buffer_lettori;
		lettori_array_struct[i].let_dati=&let_dati;
		lettori_array_struct[i].sem_free_slots=&sem_free_slots_let;
		lettori_array_struct[i].sem_data_items=&sem_data_items_let;
		lettori_array_struct[i].read_write=&read_write;
		lettori_array_struct[i].mutex=&mutex_let;
		lettori_array_struct[i].f=outfile;
		lettori_array_struct[i].mutex_file=&mutex_file;
		xpthread_create(&array_lettori[i],NULL,&fun_lettori,&lettori_array_struct[i],QUI);//lancio lettori
		}	
	
	//--GESTORE DEI SEGNALI--
	signal_struct dati_gestore;
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
	xpthread_mutex_destroy(&mutex_sc,QUI);
	xpthread_mutex_destroy(&mutex_let,QUI);
	xpthread_mutex_destroy(&mutex_file,QUI);
	xsem_destroy(&sem_data_items_let,QUI);  
	xsem_destroy(&sem_free_slots_let,QUI);  
	xsem_destroy(&sem_data_items_sc,QUI);  
	xsem_destroy(&sem_free_slots_sc,QUI);  
	read_write__destroy(&read_write);
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

void aggiungi(char *s){
	ENTRY *e=malloc(sizeof(ENTRY)); //alloco memoria per una entry della tabella
	if (e==NULL) xtermina("errore malloc entry in aggiungi",QUI);
	e->key=strdup(s);  //e->key prende la copia di s
	assert(e->key!=NULL);
	ENTRY *r=hsearch(*e,FIND); //controllo se la entry è gia in tabella
	if (r!=NULL){ //la entry è già in tabella
		assert(strcmp(e->key,r->key)==0);
		e->data = malloc(sizeof(coppia));
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

int conta(char *s){
	ENTRY *e=malloc(sizeof(ENTRY)); //alloco memoria per una entry della tabella
	if (e==NULL) xtermina("errore malloc entry in conta",QUI);
	e->key=strdup(s);  //e->key prende la copia di s
	e->data = malloc(sizeof(coppia));
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
        if (nleft == n) return -1; 
        else break;
     } else if (nread == 0) break; 
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft);
}

void *fun_caposcrittore(void *v){
	caposcrittore_struct *d=(caposcrittore_struct *)v;
	puts("== capo scrittore partito ==");
	int fdati_caposcrittore=open("caposc",O_RDONLY); 
	if (fdati_caposcrittore<0) xtermina("errore apertura pipe capo scrittore",QUI);
	int caposc_ind=0; 
	while(true){
		int len; //valore in cui memorizzo lunghezza sequenza
		ssize_t e=readn(fdati_caposcrittore,&len,sizeof(int)); //read della lunghezza sequenza
		if (e==0) break; 
		char *string=malloc((len+1)*sizeof(char)); //alloco memoria per la sequenza da ricevere
		e=readn(fdati_caposcrittore,string,len*sizeof(char)); //read della sequenza
		if (e==0) break; 
		assert(e!=0);
		string[len]=0;  //metto 0 in fondo alla sequenza
		char *saveptr; 
		char *s=strtok_r(string,".,:; \n\r\t",&saveptr);//stringa la cui copia è da memorizzare nel buffer
		while(s!=NULL){
			char *str_buf=strdup(s); //creo copia della stringa ottenuta con strtok_r		    
		    xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
			d->buffer[caposc_ind%PC_buffer_len]=str_buf; 
			xsem_post(d->sem_data_items,QUI); //comunico che c'è un item in più
			caposc_ind++;
			s=strtok_r(NULL,".,:; \n\r\t",&saveptr);
			}
		free(string); 
		}
	xclose(fdati_caposcrittore,QUI); //chiudo la pipe in lettura
	for(int i=0;i<d->nr_scrittori;i++){
	xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
	d->buffer[caposc_ind%PC_buffer_len]="-1"; //scrivo -1 (valore di terminazione) nel buffer
	xsem_post(d->sem_data_items,QUI);
	caposc_ind++;
		}
	return NULL;
}

void *fun_scrittori(void *v){
	scrittori_struct *d=(scrittori_struct *)v;
	char *stringa;
	char confronto[]="-1";
	do{
	xsem_wait(d->sem_data_items,QUI); 
	xpthread_mutex_lock(d->mutex,QUI);
	stringa=d->buffer[*(d->sc_dati)%PC_buffer_len]; //prendo la stringa dal buffer
		  *(d->sc_dati)+=1;
	xpthread_mutex_unlock(d->mutex,QUI);
    xsem_post(d->sem_free_slots,QUI);
    if (strcmp(stringa,confronto)==0) break; //terminazione se le due stringhe coincidono
	write_lock(d->read_write);
	aggiungi(stringa);
	write_unlock(d->read_write); 
	free(stringa);
		}while(true);		
	return NULL;
}

void *fun_capolettore(void *v){
	capolettore_struct *d=(capolettore_struct *)v;
	puts("== capo lettore partito ==");
	int fd_capolet=open("capolet",O_RDONLY); //apro la pipe 'capolet' in rd only
	if (fd_capolet<0) xtermina("errore apertura pipe capo lettore",QUI);
	int indice_capolet=0;//indice buffer prod/cons
	while(true){
		int len; //valore in cui memorizzo lunghezza sequenza
		ssize_t e=readn(fd_capolet,&len,sizeof(int)); //read della lunghezza sequenza
		if (e==0) break; 
		char *stringa=malloc((len+1)*sizeof(char));	 //alloco memoria per la sequenza da ricevere  (+1 perchè metto 0 in fondo)
		e=readn(fd_capolet,stringa,len*sizeof(char)); //read della sequenza
		if (e==0) break; 
		assert(e!=0);
		stringa[len]=0;  //metto 0 in fondo alla sequenza
		char *saveptr; //puntatore per ricordarsi dove si era arrivati nella strtok_r
		char *s=strtok_r(stringa,".,:; \n\r\t",&saveptr);//stringa la cui copia è da memorizzare nel buffer
		while(s!=NULL){
			char *str_buf=strdup(s); //creo copia della stringa ottenuta con strtok_r
		    xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
			d->buffer[indice_capolet%PC_buffer_len]=str_buf; //scrivo la stringa nel buffer
			xsem_post(d->sem_data_items,QUI); //comunico che c'è un item in più
			indice_capolet++;
			s=strtok_r(NULL,".,:; \n\r\t",&saveptr);

			}
		free(stringa); 
		}
	xclose(fd_capolet,QUI); 
	for(int i=0;i<d->nr_lettori;i++){
	xsem_wait(d->sem_free_slots,QUI); //mi metto in attesa che ci siano slot liberi
	d->buffer[indice_capolet%PC_buffer_len]="-1"; //valore di terminazione
	xsem_post(d->sem_data_items,QUI);
	indice_capolet++;
		}
	return NULL;
}

void *fun_lettori(void *v){
	lettori_struct *d=(lettori_struct *)v;
	char *stringa;
	int n;
	do{
	xsem_wait(d->sem_data_items,QUI); //mi metto in attesa che ci siano items
	xpthread_mutex_lock(d->mutex,QUI);
	stringa=d->buffer[*(d->let_dati)%PC_buffer_len]; //prendo stringa dal buffer
		  *(d->let_dati)+=1;
	xpthread_mutex_unlock(d->mutex,QUI);
    xsem_post(d->sem_free_slots,QUI);
    if (strcmp(stringa,"-1")==0) break; //se è il valore di terminazione usciamo dal do while
	read_lock(d->read_write);
	n=conta(stringa); 
	read_unlock(d->read_write);
	xpthread_mutex_lock(d->mutex_file,QUI);
	fprintf(d->f,"%s %d\n",stringa,n);
	fflush(d->f);	//per flushare immediatamente sul file
	xpthread_mutex_unlock(d->mutex_file,QUI);
	free(stringa);
		}while(true);	 
	return NULL;
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
