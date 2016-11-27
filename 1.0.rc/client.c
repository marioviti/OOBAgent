#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#define SECRETSPN 3000
#define CLIENT_BUFF_SIZE 8

/*
	client.
	Il client è strutturato: single process.
	Il client genera ID da 8 byte e il secret da 4byte pseudocasualmente.
	Si connette a p socket (1=<p<k). A questi invia ID un numero w (w>3k) di volte in ordine casuale.
*/

int ismember(int num, int *arr, int dim);

uint64_t hton64( uint64_t num );

int main(int argc, char const *argv[])
{
	/* 	parametri client: p = #subset : 1 <= p < k , k = #server, w =  numero invii , w > 3p. */
	int p,k,w,secret,i,tmp,conn;
	char *endptr1,*endptr2,*endptr3;
	struct timespec req;
	uint64_t ID,netID;

	/*	gestione dell'input. */
	if(argc>4)
	{fprintf(stderr, "Usage: client [SUBSET] [#SERVER] [#MESSAGE] \n"); exit(EXIT_FAILURE);}
	
	p=(int) strtol( argv[1], &endptr1, 10);

	if (errno == ERANGE || (errno != 0 && p == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr1 == argv[1])
	{fprintf(stderr, "Usage: client [SUBSET] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}
	if (*endptr1 != '\0')     
	{fprintf(stderr, "Usage: client [SUBSET] : passed is no decimal number entirely\n"); 
		exit(EXIT_FAILURE);}

	k=(int) strtol( argv[2], &endptr2, 10);

	if (errno == ERANGE || (errno != 0 && k == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr2 == argv[1])
	{fprintf(stderr, "Usage: client [#SERVER] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}
	if (*endptr2 != '\0')     
	{fprintf(stderr, "Usage: client [#SERVER] : passed is no decimal number entirely\n"); 
		exit(EXIT_FAILURE);}
	if(p>=k)
	{fprintf(stderr, "Usage: client [#SERVER] must be > [SUBSET]\n");
		exit(EXIT_FAILURE);}

	w=(int) strtol( argv[3], &endptr3, 10);

	if (errno == ERANGE || (errno != 0 && w == 0))
	{perror("strtol"); exit(EXIT_FAILURE);}
	if (endptr3 == argv[1])
	{fprintf(stderr, "Usage: client [#INVII] : passed is no decimal number \n");
		exit(EXIT_FAILURE);}
	if (*endptr3 != '\0')     
	{fprintf(stderr, "Usage: client [#INVII] : passed is no decimal number entirely\n"); 
		exit(EXIT_FAILURE);}
	if(w<=(3*p))
	{fprintf(stderr, "Usage: client [#INVII] must be > 3 * [SUBSET]\n");
		exit(EXIT_FAILURE);}

	int contacts_id[p];
 	int contacts_fd[p];

 	struct sockaddr_un sk_a;
 	char *s= (char*)malloc(sizeof(char)*108);
 	
 	/* 	set del buffer stdout linebufferd di lunghezza BUFSIZ. Previene che la redirezione
 	 	a tempo di esecuzione di stdio su file sia block buffered.	*/
 	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
 	
 	/*	generazione del secret e impostazione sveglia. */
 	srand(getpid() + time(NULL));
	secret=1 + rand()%SECRETSPN;

	req.tv_sec=secret/1000;
	req.tv_nsec=(secret%1000)*1000000;
	
	/*	generazione del ID in network byte order. */
	ID=(uint64_t)(rand()) << 32;
	ID=(uint64_t)(ID+rand());
	netID=hton64(ID);
	
	/*	Il client stampa il suo ID in ordine. */
	printf("CLIENT %"PRIX64" SECRET %d\n", ID , secret);
	
	/*	Ciclo di generazione della "rubrica" per contattare il subset. */
	for(i=0;i<p;i++)
	{
		tmp=1+(rand()%(k));
       	if(!ismember(tmp,contacts_id,i))
       	{
      		contacts_id[i]=tmp;
	      	sprintf( s, "OOB-server-%d", contacts_id[i] );
		  	strcpy( sk_a.sun_path , s);
		  	sk_a.sun_family=AF_UNIX;
		  	
		  	#ifdef DEBUG
		  		printf("client %"PRIX64" connette a %s server-%d:\n",ID,s,tmp);
			#endif
	      	
	      	/*	contatto lo i-esimo server del subset */
	      	if((contacts_fd[i]=socket(AF_UNIX,SOCK_STREAM,0))==-1)
				{perror("client: creazione socket\n"); exit(errno);}

			conn=0;

			while ((connect(contacts_fd[i], (struct sockaddr*)&(sk_a), sizeof(sk_a))==-1))
			{
				if (errno==ENOENT)
				{
					/* 	gestione minimale della congestione. */
					printf("client: tentativo contatto a %s\n", sk_a.sun_path);
					if(conn<10) 
					{
						conn++;
						sleep(conn);
					}
					else
					{perror("client: connect\n"); exit(errno);}
				}
				else
				{ perror("client: connect\n"); exit(errno);}
			}
		}
		else
			i--;
	}

	/*	INIZIO cliclo invii. */
	for (i=0;i<w;i++)
	{
		tmp=rand()%p;
		write(contacts_fd[tmp], (void*)&netID, CLIENT_BUFF_SIZE);
		nanosleep(&req,NULL);
	}
	for(i=0;i<p;i++)	
		close(contacts_fd[i]);

	printf("CLIENT %"PRIX64" DONE\n", ID );

	return 0;
}

int ismember(int num, int *arr, int dim)
{
    int i, trovato;
    i=0;
    trovato=0;
       while(!trovato && i<dim)
       {
            if(arr[i] == num)               
                trovato = 1;
            i++;
       }
    return trovato;
}

uint64_t hton64( uint64_t num  )
{
	int big=0;
	uint32_t high, low;
	high=1;
	/*	Endian Awareness: Se l'ordine dei byte che risiedono in memoria è big endian
		la htnol restituirà l'intero in network byte order ovvero big endian, la risultante sarà
		l'identità. Altrimenti se i byte che risiedono in memoria sono salvati per indirizzi
		crescenti dal meno significativo al più significativo ovvero little endian, l'argomento e il
		risultato non saranno uguali. Tengo a specificare che questa strategia non è valida per macchine con endianess
		non standard. */
	low=htonl(high);
	if(high==low)
		big=1;
	uint64_t tmp, tmpsw;
	tmp=num;
	if(!big)
	{
		tmp = (num << 32) >> 32;
		high = (int)(num >> 32);
		low = (int)(tmp);
		tmpsw = low;
		tmpsw = (tmpsw << 32) + high;
		tmp = htonl(low);
		tmp = (tmp << 32) + htonl(high);
		#ifdef DEBUG
		printf("host: %"PRIX64" swap: %"PRIX64" net: %"PRIX64"\n", num , tmpsw , tmp);
		#endif
	}	
	return tmp;

}
