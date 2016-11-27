#include "util.h"

/*
	Supervisor.
	Il supervisor è strutturato: 
	multiprocess(k server).
	multithreaded(k receiver + 1 gestore stampa).

	thread main (supervisor).
	Il ciclo principale del main thread consiste nel lancio di k server
	e di k thread receiver e di un thread printer.
	Dopo questo ciclo si mette in attesa  con una chiamata alla phtread_join 
	per ogni tid dei k thread reciever.

	thread receiver (supervisor).
	Il thread -i receiver legge in maniera bloccante dall’estremità di lettura 
	della pipe condivisa con il server -i 
	un messaggio, questo comprende due campi: ID e secret.
	!!!OSS:
	Il padre "retro eredita" la responabilità di cancellare le socket dei 		figli.
*/

int *PIPES_R;
int *PIPES_W;
int *server_pid;
int num_server;
pthread_t printer_t;
struct rec_mess *CLIENT_list = NULL;
pthread_mutex_t mtx;
volatile sig_atomic_t sem = 0;

struct rec_mess *merge_id(struct rec_mess *CLIENT_list, uint64_t ID, uint32_t secret );

void *receiver(void *arguments);

void STAMPA(struct rec_mess* list, FILE* out);

void *printer(void *arguments);

void clean_up(void);

#ifdef DEBUG
int TEST_NUMBER_CLIENT=0;
#endif

int main(int argc, char const *argv[])
{
	char *endptr;
	int k,i;
	k=(int) strtol( argv[1], &endptr, 10);
	
	/* Gestione degli input */
	if(argc>2 )
		{perror("Usage: supervisor [#server]\n"); exit(1);}

	if (errno == ERANGE || (errno != 0 && k == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr == argv[1])
	{fprintf(stderr, "Usage: supervisor [#server] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}
	if (*endptr != '\0')     
	{fprintf(stderr, "Usage: supervisor [#server] : passed is no decimal number entirely\n");
		exit(EXIT_FAILURE);}

	num_server=k;

	char arg_serverid[20];
	char arg_pipe_descr[20];
	
	pthread_mutex_init(&mtx,NULL);

	PIPES_R=(int*)malloc(sizeof(int)*k);
	PIPES_W=(int*)malloc(sizeof(int)*k);
	server_pid=(int*)malloc(sizeof(int)*k);
	pthread_t thread_[k];
	int piped[2];

	/* 	set del buffer stdout linebufferd di lunghezza BUFSIZ. Assicura che la redirezione
 	 	a tempo di esecuzione di stdio e stderr su file sia line buffered.	*/
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

	pthread_mutex_lock(&mtx);

	sigset_t set, oldset;
	sigemptyset(&set);
	sigaddset(&set,SIGINT);
	sigaddset(&set,SIGTERM);
	sigaddset(&set,SIGCHLD);
	pthread_sigmask(SIG_BLOCK,&set,&oldset);
	
	if(atexit(clean_up)!=0)
		{fprintf(stderr, "supvis: atexit: pushing della routine.\n"); exit(EXIT_FAILURE);}
	/*
		INIZIO CLICLO PRINCIPALE: creo k server e k thread receiver dedicato ai k server. */
	for(i=0;i<k;i++)
	{
		if((pipe(piped))==-1)
			{perror("supvis:creating pipe\n");exit(errno);}
		server_pid[i]=fork();
		if (server_pid[i]<0)
			{perror("supvis:creating_server\n");exit(errno);}
		else if(server_pid[i]==0)
		{	
			/* SONO IL FIGLIO: preparo gli arogmenti da dare al server */

			close(piped[0]);
			snprintf( arg_serverid, sizeof(arg_serverid), "%d", i+1);
			snprintf( arg_pipe_descr, sizeof(arg_pipe_descr), "%d", piped[1]);	
			if(execlp("./server", "server" , arg_serverid, arg_pipe_descr,(char*)NULL)==-1)
				{ perror("supvis:exec codice server\n"); exit(errno);}	
		}
		else
		{
			/* SONO IL PADRE: creo il receiver dedicato al server */

			close(piped[1]);
			PIPES_R[i]=piped[0];	
			int* arg=(int*)malloc(sizeof(int));
			*arg=i;
			if( pthread_create( thread_+i , NULL , receiver , (void*)arg ) != 0 )
       			{perror("supvis:creazione thread\n");exit(1);}	
       		#ifdef DEBUG	
			printf("creo il thread: %d\n", i );
			#endif
		}

	}
	/* 	FINE CLICLO PRINCIPALE: creo il thread printer dedicato alla stampa
		mi metto in attesa della temrinazione dei reciver. */

	if(pthread_create( &printer_t , NULL , printer , NULL ) != 0)
       			{perror("supvis:creazione printer_t\n");exit(1);}	
	
	pthread_mutex_unlock(&mtx);

	for(i=0;i<k;i++)
		pthread_join(thread_[i],NULL);

	return 0;
}


void *receiver(void *arguments)
{
	int* local_arg=(int*) arguments;
	int i= *local_arg;
	struct rec_mess buffer;
	int nread;

	/*	Read bloccante: La read si blocca e termina una volta che ha letto CLIENT_BUFF_SIZE byte
		dal buffer.	*/
	while((nread = read(PIPES_R[i],&buffer,PROTOCOL_STRUCT_SIZE))==PROTOCOL_STRUCT_SIZE)
	{
		pthread_mutex_lock(&mtx);
		#ifdef DEBUG
		printf("SUPERVISOR riceve: ID: %"PRIX64" secret: %d\n", buffer.ID, buffer.secret );
		#endif
		CLIENT_list=merge_id(CLIENT_list,buffer.ID,buffer.secret);	

		pthread_mutex_unlock(&mtx);
	}
	if(nread==-1)
		{perror("supvis: reciever: read: errore\n");exit(1);}
	
	/*	leggo eof , il server ha teminato, termino. */
	return (void*)0;
}

struct rec_mess *merge_id ( struct rec_mess *CLIENT_list, uint64_t ID , uint32_t secret )
{
	struct rec_mess *Nav= CLIENT_list;	

	if( CLIENT_list==NULL)
	{
		Nav=(struct rec_mess*)malloc(sizeof(struct rec_mess));
		if(Nav==NULL)
			{perror("supvis: receiver: malloc error\n"); exit(EXIT_FAILURE);}
		Nav->ID=ID;
		Nav->secret=secret;
		Nav->based_on=1;
		Nav->next=NULL;
		#ifdef DEBUG
		TEST_NUMBER_CLIENT++;
		#endif	
		return Nav;
	}
	else
	{
		while(Nav->next!=NULL && Nav->ID!=ID )
		Nav=Nav->next;
		if(Nav->ID==ID)
		{
			Nav->based_on++;
				if(Nav->secret>secret)
				{
					Nav->secret = secret; 	
				}
		}
		else
		{
			struct rec_mess *nuovo=(struct rec_mess*)malloc(sizeof(struct rec_mess));
			if(nuovo==NULL)
				{perror("supvis: receiver: malloc error\n"); exit(1);}
			nuovo->ID=ID;
			nuovo->secret=secret;
			nuovo->based_on=1;
			nuovo->next=NULL;
			Nav->next=nuovo;
			#ifdef DEBUG
			TEST_NUMBER_CLIENT++;
			#endif	
		}
		return CLIENT_list;
	}
}

void STAMPA(struct rec_mess* list, FILE *out)
{
	struct rec_mess * next=list;
	#ifdef DEBUG
	fprintf(out,"I CLIENT SONO IN TOTALE: %d\n", TEST_NUMBER_CLIENT);
	#endif
	while(next!=NULL)
	{
		fprintf(out, "SUPERVISOR ESTIMATE %d FOR %"PRIX64" BASED ON %d\n", next->secret , next->ID, next->based_on );		
		next=next->next;;
	}
	#ifdef DEBUG
	fprintf(out,"----------------\n");
	#endif
}

void *printer(void *arguments)
{
	int wait,i,sig,termination=0;
	sigset_t p_set;
	sigemptyset(&p_set);
	sigaddset(&p_set,SIGINT);
	struct timespec tval;
	tval.tv_sec=1;
	
	/*	Blocco sigint per mettermici in attesa. */
	if(pthread_sigmask(SIG_BLOCK,&p_set,NULL) ==-1)
		{perror("supvis:printer:sigmask error\n");exit(1);}
	while((wait=sigwait(&p_set,&sig))==0 && !termination )
	{
		if(sig==SIGINT)
		{	
			#ifdef DEBUG
			printf("arrivato il primo sigint\n");
			#endif
			STAMPA(CLIENT_list, stderr);
			if( sigtimedwait(&p_set,NULL,&tval) == -1)
			{	
				#ifdef DEBUG
				printf("passato un sec dal primo sigint\n");
				#endif
			}
			else
			{
				/* 	la lettura non avviene in mutua esclusione. */
				STAMPA(CLIENT_list, stdout);
				termination=1;
				//sem=1;
				/*	uccido i server. */
				for(i=0;i<num_server;i++)
					kill(server_pid[i],SIGTERM);
				#ifdef DEBUG
				printf("non passato un sec dal primo sigint\n");
				#endif
			}
		}
	}
	if(wait>0)
		{perror("supvis:printer:sigwait error\n"); exit(1);}

	return (void*)0;
}

void clean_up(void)
{
	int i;
	char sock_path[20];

	for(i=0;i<num_server;i++)
	{
		sprintf(sock_path,"OOB-server-%d",i+1);
		if (unlink(sock_path) == -1) 
			{perror("server:unlink_socket\n");}
	}
	
}










