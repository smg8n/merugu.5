
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#define THRESHOLD 10
#define BOUND 500

struct timer
{
	unsigned int seconds;
	unsigned int ns;
};

struct resource
{
	unsigned int maxAmt;
	unsigned int available;
	unsigned int request;
	unsigned int allocation;
	unsigned int release;
	unsigned int reqArray[18];
	unsigned int allArray[18];
	unsigned int relArray[18];
	int shared;
};

int errno;
int myIndex;
pid_t pid;
char errmsg[200];
struct timer *shmTime;
int *shmChild;
int *shmTerm;
struct resource *shmRes;
sem_t * semDead;
sem_t * semTerm;
sem_t * semChild;

/* Insert other shmid values here */

void sigIntHandler(int signum)
{
	int i;
	
	snprintf(errmsg, sizeof(errmsg), "USER %d: Caught SIGINT! Killing process #%d.", pid, myIndex);
	perror(errmsg);	
	
	for(i = 0; i < 20; i++)
	{
		shmRes[i].relArray[myIndex] = shmRes[i].allArray[myIndex];
		shmRes[i].reqArray[myIndex] = 0;
	}
	
	sem_wait(semTerm);
	shmTerm[myIndex] = 1;
	shmTerm[19]++;
	sem_post(semTerm);
	
	sem_post(semDead);
	
	errno = shmdt(shmTime);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "USER %d: shmdt(shmTime)", pid);
		perror(errmsg);	
	}

	errno = shmdt(shmChild);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "USER %d: shmdt(shmChild)", pid);
		perror(errmsg);	
	}
	
	errno = shmdt(shmTerm);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "USER %d: shmdt(shmTerm)", pid);
		perror(errmsg);	
	}
	
	errno = shmdt(shmRes);
	if(errno == -1)
	{
		snprintf(errmsg, sizeof(errmsg), "USER %d: shmdt(shmRes)", pid);
		perror(errmsg);	
	}
	exit(signum);
}

int main (int argc, char *argv[]) {
int o;
int i;
int terminate = 0;
struct timer termTime;
struct timer reqlTime;
int timeKey = atoi(argv[1]);
int childKey = atoi(argv[2]);
int index = atoi(argv[3]);
myIndex = index;
int termKey = atoi(argv[4]);
int resKey = atoi(argv[5]);
key_t keyTime = 8675;
key_t keyChild = 5309;
key_t keyTerm = 1138;
key_t keyRes = 8311;
signal(SIGINT, sigIntHandler);
pid = getpid();
unsigned int nextRes;

/* Seed random number generator */
srand(pid * time(NULL));

/* snprintf(errmsg, sizeof(errmsg), "USER %d: Slave process started!", pid);
perror(errmsg); */

/********************MEMORY ATTACHMENT********************/
/* Point shmTime to shared memory */
shmTime = shmat(timeKey, NULL, 0);
if ((void *)shmTime == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmat(shmidTime)");
	perror(errmsg);
    exit(1);
}

/* Point shmChild to shared memory */
shmChild = shmat(childKey, NULL, 0);
if ((void *)shmChild == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmat(shmidChild)");
	perror(errmsg);
    exit(1);
}

/* Point shmTerm to shared memory */
shmTerm = shmat(termKey, NULL, 0);
if ((void *)shmTerm == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmat(shmidTerm)");
	perror(errmsg);
    exit(1);
}

/* Point shmRes to shared memory */
shmRes = shmat(resKey, NULL, 0);
if ((void *)shmRes == (void *)-1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmat(shmidRes)");
	perror(errmsg);
    exit(1);
}
/********************END ATTACHMENT********************/

/********************SEMAPHORE CREATION********************/
/* Open Semaphore */
semDead=sem_open("semDead", 1);
if(semDead == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "USER %d: sem_open(semDead)...", pid);
	perror(errmsg);
    exit(1);
}

semTerm=sem_open("semTerm", 1);
if(semTerm == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "USER %d: sem_open(semTerm)...", pid);
	perror(errmsg);
    exit(1);
}

semChild=sem_open("semChild", 1);
if(semTerm == SEM_FAILED) {
	snprintf(errmsg, sizeof(errmsg), "USER %d: sem_open(semChild)...", pid);
	perror(errmsg);
    exit(1);
}
/********************END SEMAPHORE CREATION********************/

/* Calculate First Request/Release Time */
reqlTime.ns = shmTime->ns + rand()%(BOUND);
reqlTime.seconds = shmTime->seconds;
if (reqlTime.ns > 1000000000)
{
	reqlTime.ns -= 1000000000;
	reqlTime.seconds += 1;
}

while(!terminate)
{
	if(rand()%100 <= THRESHOLD)
	{
		terminate = 1;
	}
	/* else
	{
		snprintf(errmsg, sizeof(errmsg), "USER %d: Slave process continuing!", pid);
		perror(errmsg);
	} */
	
	/* Calculate Termination Time */
	termTime.ns = shmTime->ns + rand()%250000000;
	termTime.seconds = shmTime->seconds;
	if (termTime.ns > 1000000000)
	{
		termTime.ns -= 1000000000;
		termTime.seconds += 1;
	}
	
	

	if(reqlTime.seconds <= shmTime->seconds)
	{
		if(reqlTime.ns <= shmTime->ns || reqlTime.seconds < shmTime->seconds)
		{
			/********************ENTER CRITICAL SECTION********************/
			/* sem_wait(semChild); */	/* P operation */
			nextRes = rand()%20;
			if(shmRes[nextRes].allArray[index] == 0)
			{
				shmRes[nextRes].reqArray[index]++;
				while(shmRes[nextRes].reqArray[index]);
			}
			else
			{
				if(rand()%10)
				{
					shmRes[nextRes].reqArray[index]++;
					while(shmRes[nextRes].reqArray[index]);
				}
				else
				{
					shmRes[nextRes].relArray[index] = shmRes[nextRes].allArray[index];
				}
			}
			/* Calculate Next Request/Release Time */
			reqlTime.ns = shmTime->ns + rand()%(BOUND);
			reqlTime.seconds = shmTime->seconds;
			if (reqlTime.ns > 1000000000)
			{
				reqlTime.ns -= 1000000000;
				reqlTime.seconds += 1;
			}
			/* sem_post(semChild); */ /* V operation */  
			/********************EXIT CRITICAL SECTION********************/
		}
	}
	
	
	/* Wait for the system clock to pass the time */
	while(termTime.seconds > shmTime->seconds);
	while(termTime.ns > shmTime->ns);
	
}
/* snprintf(errmsg, sizeof(errmsg), "USER %d: Slave process sleeping!", pid);
perror(errmsg);
sleep(1); */
/* signal the release the process from the running processes */
sem_wait(semTerm);
shmTerm[index] = 1;
sem_post(semTerm);
snprintf(errmsg, sizeof(errmsg), "USER %d: Slave process terminating!", pid);
perror(errmsg);
sem_post(semDead);

/********************MEMORY DETACHMENT********************/
errno = shmdt(shmTime);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmdt(shmTime)");
	perror(errmsg);	
}

errno = shmdt(shmChild);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmdt(shmChild)");
	perror(errmsg);	
}

errno = shmdt(shmTerm);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmdt(shmTerm)");
	perror(errmsg);	
}

errno = shmdt(shmRes);
if(errno == -1)
{
	snprintf(errmsg, sizeof(errmsg), "USER: shmdt(shmRes)");
	perror(errmsg);	
}
/********************END DETACHMENT********************/
exit(0);
return 0;
}
 
