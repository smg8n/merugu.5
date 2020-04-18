#include <string.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>

#include "oss.h"
#include "bit_vector.h"

//maximum values for:
#define B 75

static int shmid = -1, semid = -1;

struct arguments{
	int pi;
	int verbose;
} args = {0, 0};

static struct system* attach_shm(){
	key_t key = ftok(SHARED_PATH, MEMORY_KEY);  //get a key for the shared memory
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	shmid = shmget(key, sizeof(struct system), 0);
	if(shmid == -1){
		perror("shmget");
		return NULL;
	}

	struct system *addr = (struct system *) shmat(shmid, NULL, 0);
	if(addr == NULL){
		perror("shmat");
		return NULL;
	}

	key = ftok(SHARED_PATH, SEMAPHORE_KEY);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	semid = semget(key, 1 + MAX_RUNNING, 0);
	if(semid == -1){
		perror("msgget");
		return NULL;
	}

	return addr;
}

int main(const int argc, char * const argv[]){

	if(argc < 2){
		fprintf(stderr, "Error: Missing pi argument\n");
		return 1;
	}

	args.pi = atoi(argv[1]);
	if((argc == 3) && (strcmp("verbose", argv[2]) == 0))
		args.verbose = 1;

	//we don't use the shared memory, but we attach it
	struct system *sys = attach_shm();
	if(sys == NULL){
		return 1;
	}
	struct proc_info * proc = &sys->procs[args.pi];

	srand(getpid() * args.pi);	//init rand()

	if(!args.verbose){
		char buf[10];
		snprintf(buf, sizeof(buf), "%d.log", proc->id);
		stdout = freopen(buf, "w", stdout);
		stderr = freopen(buf, "w", stderr);
	}else{
		stdout = freopen("/dev/null", "w", stdout);
		stderr = freopen("/dev/null", "w", stderr);
	}

	printf("PID=%i, pi=%d, ID=%d\n", getpid(), args.pi, proc->id);
	fflush(stdout);

	struct sembuf sop;
	struct request rq;

	proc->state = READY;


	sop.sem_num =	sop.sem_flg = 0;

	int num_requests = (rand() % 10),
			alive = 10,	//we will make 10 request to oss
	 	  using = 0;
  while(alive || using){

		if(--num_requests < 0)
			alive = 0;

		const int x = (alive) ? (rand() % 100) : B + 1;	//if we are alive, request or release, otherwise only release
		if(x <= B){	//[0; B] == REQUEST

			rq.r   = rand() % RMAX;				//generate random resource to take
			rq.val = 1 + rand() % (RMAX_VALUE-1);
			rq.op  = REQUEST;

			if(is_shareable(rq.r) == 0){   //if resource is not shareable
		    rq.val = sys->maxres[rq.r];  //update request to try to take whole resource
		  }

			printf("Will ask for R%d:%d\n", rq.r, rq.val);

		}else{	// (B; 100] == RELEASE
			rq.r = -1;
			//check what we have and release some of it
			int i;
			for(i=0; i < RMAX; i++){
				if(proc->usage[i] > 0){	//if we are using this resource
					rq.r = i;
					rq.val = proc->usage[i];	//release all of it or part ?
					rq.op = RELEASE;
					printf("Will give back R%d:%d\n", rq.r, rq.val);
					break;
				}
			}

			if(rq.r == -1){	//if we are not using anything
				printf("Nothing to release\n");
				using = 0;
				continue;	//restart the loop
			}
		}
		rq.res = WAITFOR;	//we are waiting for result
		fflush(stdout);

		//lock sys to "send" request into shared memory
		sop.sem_num = 0;
		sop.sem_op = -1;
		if (semop(semid, &sop, 1) == -1) {
			 perror("semop");
			 break;
		}

		proc->rq = rq;		//save request in shared memory

		sop.sem_num = 0;
		sop.sem_op = 1;	//unlock
		if (semop(semid, &sop, 1) == -1) {
			 perror("semop");
			 break;
		}


		//wait on our semaphore, to be unblocked by master, when resource is ready
		sop.sem_num = args.pi + 1;
		sop.sem_op = -1;
		if (semop(semid, &sop, 1) == -1) {
			 perror("semop");
			 break;
		}

		//check if we have a reply
		switch(proc->rq.res){
			case GRANTED:
				printf("Granted\n");
				if(rq.op == REQUEST)
					using += rq.val;	//sum used resources
				else
					using -= rq.val;	//sum used resources
				break;
			case BLOCKED:	//resource is not available
				//we wont get here, because we wait on the semaphore
				printf("Error: BLOCKED after sem\n");
				break;
			case DENIED:	//we are in a deadlock
				printf("Denied\n");
				alive = 0;
				using = 0;	//parent clears the resources
				break;
			case WAITFOR:
				alive = 0;
				using = 0;	//parent clears the resources
				//we wont get here, because we wait on the semaphore
				printf("Error: WAITFOR after sem\n");
				break;
		}


		//check if we have to terminate
		if(proc->state == TERMINATED){
			alive = 0;	//stop the while
			printf("Terminated by parent...\n");
		}
		fflush(stdout);
  }

	//lock sys to "send" request into shared memory
	sop.sem_num = 0;
	sop.sem_op = -1;
	if (semop(semid, &sop, 1) == -1) {
		 perror("semop");
	}

	proc->state = TERMINATED;

	sop.sem_num = 0;
	sop.sem_op = 1;	//unlock
	if (semop(semid, &sop, 1) == -1) {
		 perror("semop");
	}


	printf("Done\n");
  shmdt(sys);

	return EXIT_SUCCESS;
}
