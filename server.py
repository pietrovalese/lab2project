#! /usr/bin/env python3
Description ="""Server che gestisce connessioni di tipo A e B"""
import struct,socket,argparse,concurrent.futures,subprocess,signal,pipes,os,threading,logging

HOST = "127.0.0.1"  
PORT =  58888
Max_sequence_length = 2048 # massima lunghezza di una sequenza che viene inviata attraverso un socket o pipe

logging.basicConfig(filename='server.log',
                    level=logging.DEBUG, filemode="w", datefmt='%d/%m/%y %H:%M:%S',
                    format='%(asctime)s - %(levelname)s - %(message)s')


def main(max_threads,r,w,v,host=HOST,port=PORT):
	assert max_threads > 0, "Il numero di thread deve essere maggiore di 0"
	if not os.path.exists(os.path.abspath("capolet")):
		os.mkfifo("capolet",0o666)  #creo pipe capolet, se non c'è già
	if not os.path.exists(os.path.abspath("caposc")):
		os.mkfifo("caposc",0o666)#	#creo pipe caposc, se non c'è già
	if v is None: #non è stato dato -v, lancio l'eseguibile archivio senza valgrind
		p=subprocess.Popen(["./archivio",f"{w}",f"{r}"])  
	else:
		p = subprocess.Popen(["valgrind","--leak-check=full", 
                      "--show-leak-kinds=all", 
                      "--log-file=valgrind-%p.log", "-s",
                      "./archivio", f"{w}",f"{r}"])
	print("Ho lanciato il processo:", p.pid)	
	capolet=os.open("capolet",os.O_WRONLY) 	#apro capolet in scrittura
	caposc=os.open("caposc",os.O_WRONLY)	#apro caposc in scrittura
	with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as s:
		try:
			s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1) #per riusare l'indirizzo(evita il address already use)
			s.bind((host,port))
			s.listen()
			with concurrent.futures.ThreadPoolExecutor(max_workers=max_threads) as executor: #crea e gestisce multithreads
				while True:
					print("Sono in attesa di un client:")
					connection,addr=s.accept()
					executor.submit(connessioni_fun,connection,addr,capolet,caposc)#faccio gestire ai thread la connessione
		except KeyboardInterrupt:		
			pass
		s.shutdown(socket.SHUT_RDWR)	#chiudo socket
		os.close(capolet) 				#chiudo capolet in scrittura
		os.close(caposc)  				#chiudo caposc in scittura
		os.unlink("capolet") 			#cancello capolet
		os.unlink("caposc")  			#cancello caposc
		os.kill(p.pid,signal.SIGTERM)	#sigterm ad archivio
  
def connessioni_fun(conn,addr,capolet,caposc):
	with conn:
		tipo_di_client=recv_all(conn,4) #ricevo byte che mi dice se client è di tipo 1 o 2
		valore=struct.unpack("!i",tipo_di_client)[0]
		logging.debug(f"Client collegato:{addr}")
		if valore==0:
			logging.debug(f"{threading.current_thread().name} contattato da {addr}, client di tipo A")
			contatore_bytes=0 #contatore numero bytes da scrivere sul log
			lunghezza_stringa_fs=recv_all(conn,4) #ricevo lunghezza stringa
			lunghezza_stringa=struct.unpack("!i",lunghezza_stringa_fs)[0]
			assert lunghezza_stringa<Max_sequence_length, "lunghezza stringa maggiore di 2048 bytes"
			string=recv_all(conn,lunghezza_stringa) #ricevo la stringa
			p_len_stringa=struct.pack("<i",lunghezza_stringa)   #prima devo convertire la len in little endian
			os.write(capolet,p_len_stringa+string) 			#scrivo len e string nella pipe capolet
			contatore_bytes+=len(p_len_stringa)+len(string)
			logging.debug(f"Connessione di tipo A: scritti {contatore_bytes} bytes nella pipe capolet")
			logging.debug(f"{threading.current_thread().name} finito con {addr}")
		elif valore==1:
			logging.debug(f"{threading.current_thread().name} contattato da {addr}, client di tipo B")
			contatore_bytes=0 #contatore numero bytes da scrivere sul log
			numero_sequenze=0
			while True:
				lunghezza_stringa_fs=recv_all(conn,4) #ricevo lunghezza stringa
				lunghezza_stringa=struct.unpack("!i",lunghezza_stringa_fs)[0]
				if lunghezza_stringa==0:#condizione di terminazione
					break
				assert lunghezza_stringa<Max_sequence_length, "lunghezza stringa maggiore di 2048 bytes"    
				string=recv_all(conn,lunghezza_stringa)	#ricevo stringa		
				little_endian_len_stringa=struct.pack("<i",lunghezza_stringa)   # prima devo convertire la len in little endian
				os.write(caposc,little_endian_len_stringa+string) 			#scrivo len e string nella pipe capolsc
				contatore_bytes+=len(little_endian_len_stringa)+len(string)#incremento contatore_bytes
				numero_sequenze+=1    
			conn.sendall(struct.pack("!i",numero_sequenze))
			logging.debug(f"Connessione di tipo B: scritti {contatore_bytes} bytes nella pipe caposc")
			logging.debug(f"{threading.current_thread().name} finito con {addr}")
	
def recv_all(conn,n):
  chunks = b'' #accumulatore byte
  bytes_recd = 0 #byte totali
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 1024)) #prendere pezzi più piccoli possibili per evitare di intasare il codice
    if len(chunk) == 0:
      raise RuntimeError("socket connection broken")
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
  return chunks

if __name__ == '__main__': 
	parser = argparse.ArgumentParser(description=Description, formatter_class=argparse.RawTextHelpFormatter)
	parser.add_argument('t', help='numero thread', type = int)    #numero thread
	parser.add_argument('-r', help='thread lettori', type = str, default=3) #numero thread lettori
	parser.add_argument('-w', help='thread scrittori', type = str, default=3) #numero thread scrittori
	parser.add_argument('-v', help='run archivio.c con valgrind', nargs='?', const='call_-v')    #nargs indefinito, assume il valore di const se viene scritto -v senza parametri
	args = parser.parse_args()
	main(args.t,args.r,args.w,args.v)