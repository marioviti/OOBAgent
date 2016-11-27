#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <inttypes.h>
#define PROTOCOL_STRUCT_SIZE 12

struct rec_mess
{
	uint64_t ID;
	int secret;
	int based_on;
	struct rec_mess* next;
};
