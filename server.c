#include "util.h"
#define CLIENT_BUFF_SIZE 8

/*
	Server.
	Il server è strutturato:multithreaded (uno per client).

	thread main (server).
	Il ciclo principale del main thread (server -i) consiste nella preparazione di 
	una socket con indirizzo OOB-server-i.
	I file descriptor ritornati dalla accept vengono passati ad un thread client 
	dedicato al client connesso alla socket.

	thread client_server (server).
	Ogni client viene servito da un thread dedicato che stima il tempo 
	che intercorre fra due invii dello stesso client al server -i.
*/

int server_id;
int pipe_sv;
char* sock_path;

void exit_routine(void);

void* client_server(void *);

uint64_t ntoh64 (uint64_t num);

int main(int argc, char const *argv[])
{
	struct sockaddr_un sk_a;
	int fd_client,fd_sock;
	char *endptr1,*endptr2;
	
	/* Gestione degli input */
	if(argc>3)
	{fprintf(stderr, "Usage: server [id] [FILEDESC] \n"); exit(EXIT_FAILURE);}

	server_id=(int) strtol( argv[1], &endptr1, 10);
	
	if (errno == ERANGE || (errno != 0 && server_id == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr1 == argv[1])
	{fprintf(stderr, "Usage: server [id] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}	
	if (*endptr1 != '\0')     
	{fprintf(stderr, "Usage: server [id] : passed is no decimal number entirely\n"); 
		exit(EXIT_FAILURE);}

	pipe_sv=(int) strtol( argv[2], &endptr2, 10); 

	if (errno == ERANGE || (errno != 0 && pipe_sv == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr1 == argv[1])
	{fprintf(stderr, "Usage: server [FILEDESC] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}
	if (*endptr2 != '\0')     
	{fprintf(stderr, "Usage: server [FILEDESC] : passed is no decimal number entirely\n"); 
		exit(EXIT_FAILURE);}

	/* 	set del buffer stdout linebufferd di lunghezza BUFSIZ. Assicura che la redirezione
 	 	a tempo di esecuzione di stdio e stderr su file sia line buffered.	*/		
   	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set,SIGTERM);
	pthread_sigmask(SIG_UNBLOCK,&set,NULL);	

	if((sock_path=(char *)malloc((1+strlen("OOB-server-")+strlen(argv[1]))*sizeof(char)) )==NULL)
		{perror("server:malloc\n");exit(EXIT_FAILURE);}
    
    sprintf(sock_path,"OOB-server-%s",argv[1]);
    
    (void)unlink(sock_path);

    printf("SERVER %s ACTIVE\n", argv[1]);
  
    strcpy(sk_a.sun_path, sock_path);
    sk_a.sun_family=AF_UNIX;
    if((fd_sock=socket(AF_UNIX, SOCK_STREAM,0))== -1)
    	{perror("server:creazione socket\n"); exit(errno);}
    if(bind(fd_sock, (struct sockaddr *)&sk_a, sizeof(sk_a))== -1)
     	{perror("server:bind a socket\n"); exit(errno);}
    if(listen(fd_sock,SOMAXCONN)==-1)
      	{perror("server:listen su socket\n"); exit(errno);}

	/*	INIZIO CLICLO PRINCIPALE: attendo che mi contatti un client e smisto la sua chiamata
		ad un thread client_server dedicato. */
	while( (fd_client=accept(fd_sock,NULL,0))!= -1 )
    {
        printf("SERVER %d CONNECT FROM CLIENT\n", server_id); 
        pthread_t clt_srv_tid;
        int* arg=(int*)malloc(sizeof(int));
        *arg=fd_client;
       
        if( pthread_create( &clt_srv_tid , NULL , client_server , (void*)arg) != 0)
        	{perror("server:creazione thread\n"); exit(EXIT_FAILURE);}
	}
	if (fd_client == -1)
		{perror("server:accept\n"); exit(errno);}

	return 0;
}

void* client_server(void *arg)
{
	int fd_client=*((int*)arg);
	free(arg);
	int n_read,nocontact=0;
	struct rec_mess client_message;
	uint64_t Client_ID;
	uint64_t Saved_Client_ID;
	unsigned long long int ta,ti,stima;
	struct timeval tval;

	/*	Read bloccante: La read si blocca e termina una volta che ha letto CLIENT_BUFF_SIZE byte
		dal buffer.	*/
	n_read=read( fd_client, (void*)&Client_ID ,CLIENT_BUFF_SIZE );

	/*	Dato che i client inviano messaggi ai server contattati in ordine casuale, può capitare che un
		server contattato non riceva mai un messaggio da client. */
	if( n_read==CLIENT_BUFF_SIZE )
		Saved_Client_ID=ntoh64(Client_ID);
	else
		nocontact=1;
	
	/*	Implemento la strategia documentata nella relazione con la funzione gettimeoftheday per avere
		un riferimeto temporale assoluto. */
	while(gettimeofday(&tval,NULL)==-1)
		{perror("supervisor:get time\n");}
	ta=(((unsigned long long int)tval.tv_sec)*1000)+(((unsigned long long int)tval.tv_usec)/1000);
	ti=ta;
	stima=ta;
	
	printf("SERVER %d INCOMING FROM %"PRIX64" @ %llu\n", server_id , Saved_Client_ID , ta );

	while((n_read = read(fd_client, (void*)&Client_ID, CLIENT_BUFF_SIZE)) == CLIENT_BUFF_SIZE)
	{	
		while(gettimeofday(&tval,NULL)==-1)
			{ perror("supervisor:get time\n"); }
		ta=(((unsigned long long int)tval.tv_sec)*1000)+(((unsigned long long int)tval.tv_usec)/1000);
		ti=ta-ti;
		if(ti<stima)
			stima=ti;
		ti=ta;
		
		printf("SERVER %d INCOMING FROM %"PRIX64" @ %llu\n", server_id , Saved_Client_ID , ta );
	}
	/*	Il client ha riagganciato. */
	close(fd_client);
	
	printf("SERVER %d CLOSING %"PRIX64" ESTIMATE %llu\n", server_id , Saved_Client_ID , stima );
	
	/*	Invio i dati al Supervisor. L'ordine di scrittura è irrilevante, tutti
		i thread scrivono senza mutua esclusione sulla pipe_sv,. La write assicura che non ci siano 
		scritture interrotte o parziali. */
	client_message.ID=Saved_Client_ID;
	client_message.secret=stima;
	if(!nocontact)
		write( pipe_sv, (void*)&client_message, PROTOCOL_STRUCT_SIZE );
	
	pthread_exit(0);
}

uint64_t ntoh64 (uint64_t num)
{
	int big=0;
	uint64_t tmpsw,tmp;
	uint32_t high, low;
	/*	Endian Awareness: Se l'ordine dei byte che risiedono in memoria è big endian
		la htnol restituirà l'intero in network byte order ovvero big endian, la risultante sarà
		l'identità. Altrimenti se i byte che risiedono in memoria sono salvati per indirizzi
		crescenti dal meno significativo al più significativo ovvero little endian, l'argomento e il
		risultato non saranno uguali. Tengo a specificare che questa strategia non è valida per macchine con endianess
		non standard. */
	high=1;
	low=ntohl(high);
	tmp=num;
	if(high==low)
		big=1;
	if (!big)
	{
		tmp = (num << 32) >> 32;
		high = (int)(num >> 32);
		low = (int)(tmp);
		tmpsw = low;
		tmpsw = (tmpsw << 32) + high;
		tmp = ntohl(low);
		tmp = (tmp << 32) + ntohl(high);
		#ifdef DEBUG
		printf("host: %"PRIX64" swap: %"PRIX64" net: %"PRIX64"\n", tmp , tmpsw , num);
		#endif
	}
	return tmp;
}









