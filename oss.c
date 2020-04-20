#include <string.h>
#include <stdarg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include "oss.h"
#include "bit_vector.h"

//maximum started/running processes
#define MAX_STARTED 50

//maximum runtime in realtime seconds
#define MAX_RUNTIME 3

struct pstats{  //process stats
  unsigned int started;
  unsigned int terminated;
} pstat = {0,0};

struct rstats{  //resoucre stats
  int requested;
  int granted;
  int blocked;
  int denied;
  int released;
} rstat = {0,0,0,0,0};

struct dstats{  //deadlock stats
  int deadlocks_checks;
  int procs_terminated;
} dstat = {0,0};

static int shmid = -1, semid = -1; //memory and semaphore identifiers
static struct system * sys = NULL;
static struct sembuf sop;
static unsigned int verbose = 0;

static void show_stats(){
  printf("### Simulation Stats ###\n");
  printf("Total time: %i:%i\n", sys->oss_clock.seconds, sys->oss_clock.nanoseconds);

  printf("Processes:\n");
  printf("\tStarted: %d\n", pstat.started);
  printf("\tTerminated: %d\n", pstat.terminated);

  printf("Resources:\n");
  printf("\tRequests: %d\n", rstat.requested);
  printf("\tGranted: %d\n", rstat.granted);
  printf("\tBlocked: %d\n", rstat.blocked);
  printf("\tDenied: %d\n", rstat.denied);
  printf("\tReleased: %d\n", rstat.released);

  printf("Deadlocks:\n");
  printf("\tChecks: %d\n", dstat.deadlocks_checks);
  printf("\tKilled: %d\n", dstat.procs_terminated);

}

unsigned int num_lines = 0;

static void log_line(const char *fmt, ...){
  if (!verbose)
    return;

  if(num_lines == 100000){  //if max lines reached
    stdout = freopen("/dev/null", "w", stdout); //discard lines to null device
  }
  num_lines++;


  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static void log_line_always(const char *fmt, ...){

  if(num_lines == 10000){  //if max lines reached
    stdout = freopen("/dev/null", "w", stdout); //discard lines to null device
  }
  num_lines++;


  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static void clean_exit(const int rc){
  int i;

  show_stats();

  for(i=0; i < MAX_RUNNING; i++)
    if(sys->procs[i].pid > 0)
      kill(sys->procs[i].pid, SIGTERM);


  shmdt(sys);

  shmctl(shmid, IPC_RMID, NULL);
  semctl(semid, 0, IPC_RMID, NULL);

  exit(rc);
}

static struct system* attach_shm(){
	key_t key = ftok(SHARED_PATH, MEMORY_KEY);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	shmid = shmget(key, sizeof(struct system), IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(shmid == -1){
		perror("shmget");
		return NULL;
	}

	struct system* addr = (struct system*) shmat(shmid, NULL, 0);
  if(addr == (void*)-1){
    perror("shmat");
    return NULL;
  }

  key = ftok(SHARED_PATH, SEMAPHORE_KEY);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

  semid = semget(key, 1 + MAX_RUNNING, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  if(semid == -1){
  	perror("semget");
  	return NULL;
  }

  return addr;
}

static void signal_handler(const int sig){
  switch(sig){
    case SIGTERM:
    case SIGALRM:
      printf("OSS: Caught signal at %i:%i\n", sys->oss_clock.seconds, sys->oss_clock.nanoseconds);
      clean_exit(0);
      break;
    default:
      break;
  }
}

static int start_user(){
  static unsigned int PID = 1;
  char id_buf[10];

  if(pstat.started >= MAX_STARTED)
      return EXIT_SUCCESS;


  const int pi = search_bitvector(MAX_RUNNING); //find free bit
  if(pi == -1){  //no process info available
    //fprintf(stderr, "Master: No space for new processes\n");
    return EXIT_SUCCESS;
  }

  snprintf(id_buf, 10, "%i", pi);


  sys->procs[pi].id = PID++;
	sys->procs[pi].state = READY;	 //change our status to READY

  pid_t pid = fork();

  switch(pid){

    case -1:
      perror("fork");
      return EXIT_FAILURE;
      break;

    case 0: //child process
      sys->procs[pi].pid = getpid();
      if(verbose)
        execl("./user", "./user", id_buf, "verbose", NULL);
      else
        execl("./user", "./user", id_buf, NULL);
      perror("execl");
      exit(EXIT_FAILURE);
      break;

    default:
      sys->procs[pi].pid = pid;
      pstat.started++;
      log_line("OSS: Generating process P%u at time %lu:%lu\n", sys->procs[pi].id, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);
      break;
  }
  return EXIT_SUCCESS;
}

static void proc_cleanup(){
  int i;

  sop.sem_num = 0;
	sop.sem_op = -1;
	if (semop(semid, &sop, 1) == -1) {
		 perror("semop");
	}

  for(i=0; i < MAX_RUNNING; i++){
    if( (sys->procs[i].pid > 0) &&
        (sys->procs[i].state == TERMINATED)){

        pstat.terminated++;
        //printf("OSS: Cleanup pi=%d, id=%d. terminated=%d\n", i, sys->procs[i].id, pstat.terminated);

        bzero(&sys->procs[i], sizeof(struct proc_info));
        sys->procs[i].state = DEAD;


        unset_bit(i);
    }
  }

  sop.sem_num = 0;
	sop.sem_op = 1;	//unlock
	if (semop(semid, &sop, 1) == -1) {
		 perror("semop");
	}

}

static void show_allocated_resources(){
  int i,j;
  if(verbose){

  
  puts("OSS: Current system resources");
  printf("    ");
  for(i=0; i < RMAX; i++)
    printf("R%-2d ", i);
  printf("\n");
  for(i=0; i < MAX_RUNNING; i++){
    struct proc_info * proc = &sys->procs[i];

    if(proc->pid > 0){ //if process is alive
      printf("P%-2d ", proc->id);
      for(j=0; j < RMAX; j++){
        if(j == 10)
          printf(" ");
        printf(" %-3d", proc->usage[j]);
      }
      printf("\n");
    }
  }
  }
}

static void user_request(struct proc_info * proc){

  struct request * rq = &proc->rq;
  int r = proc->rq.r;

  if(rq->res != BLOCKED){
    log_line("Master has detected Process P%d requesting R%d=%d at time %lu:%lu\n",
        proc->id, rq->r, rq->val, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);

    rstat.requested++;
  }

  if(sys->resources[r] >= rq->val) {  //if we have that amount of resource
    proc->usage[r]     += rq->val;
    sys->resources[r]  -= rq->val;

    if(rq->res == BLOCKED){ //if request was blocked
      log_line("Master unblocking P%d and granting it R%d=%d at time %lu:%lu\n",
      proc->id, rq->r, rq->val, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);
    }else{
      log_line("Master granting P%d request R%d=%d at time %lu:%lu\n",
        proc->id, rq->r, rq->val, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);
    }

    rstat.granted++;
    rq->res = GRANTED;	//no deadlock, grant
    rq->val = 0; //clear request, after its processed
  }else if(rq->res != BLOCKED){ //if its not already blocked
    log_line("Master blocking P%d for requesting R%d=%d at time %lu:%lu\n",
      proc->id, rq->r, rq->val, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);

    rq->res = BLOCKED;
    rstat.blocked++;
  }
}

static void user_release(struct proc_info * proc){

  struct request * rq = &proc->rq;

  log_line("Master has acknowledged Process P%d releasing R%d=%d at time %lu:%lu\n",
      proc->id, rq->r, rq->val, sys->oss_clock.seconds, sys->oss_clock.nanoseconds);

  rstat.released++;

  int r = proc->rq.r;

  proc->usage[r]    -= rq->val;
  sys->resources[r] += rq->val;
  rq->res = GRANTED;
  rq->val = 0; //clear request, after its processed
}

static int find_first_dead(int * finish){
  int i;
  for(i=0; i < MAX_RUNNING; i++){
		if(finish[i] == 0){	//if not finised
        return i;
    }
  }
  return -1;
}

static void print_all_dead(int * finish){
  log_line_always("\tProcesses ");

  int i;
  for(i=0; i < MAX_RUNNING; i++){
    if(finish[i] == 0){	//if not finised
      log_line_always("P%i ", sys->procs[i].id);
    }
  }
  log_line_always(" deadlocked.\n");
}

static int find_finish_states(int * finish, int * work){
  int i,j;
  for(i=0; i < MAX_RUNNING; i++){
    struct proc_info * proc = &sys->procs[i];

    if(finish[i] == 1)
      continue;

    if(  (proc->pid == 0) ||          //no process
         (proc->rq.res == DENIED) ||  //killed process, due to deadlock, not yet exited!
         (proc->state == TERMINATED)){  //terminated on his own, not cleared yet
      finish[i] = 1;  //all terminated processes are marked as finished

    //if request can be satisfied or process is releasing
    }else if(	(proc->rq.val  <= work[proc->rq.r]) || (proc->rq.op == RELEASE)){
      finish[i] = 1;	//process is finished

      //add the finished proc allocation to work
      for(j=0; j < RMAX; j++)
        work[j] += proc->usage[j];

      break;  //work[] available has changed, restart the find by break and return
    }
  }
  return i;
}

static int dead_state(){

  dstat.deadlocks_checks++; //how many times, we checked for a deadlock

	int i, work[RMAX];
	int finish[MAX_RUNNING];

  //all not finished
  for(i=0; i < MAX_RUNNING; i++)
    finish[i] = 0;

  //copy available work
  for(i=0; i < RMAX; i++)
    work[i] = sys->resources[i];

  i=0;
	while(i != MAX_RUNNING){
    i = find_finish_states(finish, work);
	}

  const int first_dead = find_first_dead(finish);
  //if(verbose && (first_dead >= 0))
  if((first_dead >= 0))
    print_all_dead(finish);

	return first_dead;  //return how many processes are dead
}

static void kill_dead(int pi){
  struct proc_info * proc = &sys->procs[pi];

  //if(verbose)
    printf("\tKilling P%i deadlocked on R%d:%d\n", proc->id, proc->rq.r, proc->rq.val);

  //proc->state = TERMINATED;
  proc->rq.res = DENIED;
  rstat.denied++;

  //if(verbose){
    log_line_always("\t\tResources released are as follows:");

    int i;
    for(i=0; i < RMAX; i++){
      if(proc->usage[i] > 0){
  		    log_line_always("R%i:%d ", i, proc->usage[i]);
      }
    }
  	log_line_always("\n");
  //}
  dstat.procs_terminated++;

  //return allocated to system
  for(int j=0; j < RMAX; j++){
    sys->resources[j] += proc->usage[j];  //return used to system
    proc->usage[j] = 0; //clear usage
  }

  //unblock the waiting process
  sop.sem_num = pi + 1;
  sop.sem_op = 1;  //unlock process
  if (semop(semid, &sop, 1) == -1) {
     perror("semop");
     return;
  }
}

void deadlock_detection(){

  log_line("Master running deadlock detection at time %li:%li\n", sys->oss_clock.seconds, sys->oss_clock.nanoseconds);

  int p = -1;  //id of first deadlocked process
  //loop until we clear the deadlocked processes
  while((p = dead_state()) >= 0){
    //if(verbose)
      log_line("\tAttempting to resolve deadlock...\n");
    kill_dead(p);  //terminate first deadlocked process
  }

  if(p >= 0){ //if there was a deadlock
    //if(verbose){
      log_line("System is no longer in deadlock\n");
    //}
  }
}

//Process all requests from users
static int proc_requests(){
  int i, count=0;
  static int last_detectiong = 0; //last virtual second, a deadlock detection was done

  sop.sem_num = 0;
  sop.sem_op = -1;  //lock all requests
  if (semop(semid, &sop, 1) == -1) {
     perror("semop");
     return 0;
  }

  //before processing requests, check for deadlocked processes every 10 virt seconds
  if((last_detectiong+10) < sys->oss_clock.seconds){
    deadlock_detection();
    last_detectiong = sys->oss_clock.seconds;
  }

  for(i=0; i < MAX_RUNNING; i++){
    struct proc_info * proc = &sys->procs[i];
    if( (proc->pid == 0) ||   //no process
        (proc->state == TERMINATED) ||
        (proc->rq.res == DENIED) ||
        (proc->rq.val == 0) ){ //no request
      continue;
    }

      //process the requests
      switch(proc->rq.op){
        case REQUEST: user_request(proc);  break;
        case RELEASE: user_release(proc);  break;
      }

      if( (proc->rq.res != BLOCKED) &&  //if request is not blocked
          (proc->rq.res != DENIED) ){   //DENIED is released from kill_dead!

        count++;
        //unblock the waiting process
        sop.sem_num = i + 1;
        sop.sem_op = 1;  //lock all requests
        if (semop(semid, &sop, 1) == -1) {
           perror("semop");
           return 0;
        }
      }
  }

  sop.sem_num = 0;
  sop.sem_op = 1; //unlock
  if (semop(semid, &sop, 1) == -1) {
     perror("semop");
     return 0;
  }

  return count;
}

static void run_simulator(){
  int req_count = 0;
  while(pstat.terminated < MAX_STARTED){ //loop until all procs are done
    if(clock_fork_check(&sys->oss_clock) == 1){ //advance the oss  clock
      start_user();  //make a new process
    }
    req_count += proc_requests();  //process requests
    proc_cleanup();   //clean terminated processes

    //show usage on 20 Requests
    if( req_count > 20){
        show_allocated_resources();
        req_count = 0;
    }
  }
}

int init_simulator(){
  srand(getpid());


  bzero(sys, sizeof(struct system));  //clear the shared memory

  //system semaphore is unlocked
  union semun un;
  un.val = 1;
  if(semctl(semid, 0, SETVAL, un) ==-1){ //set the semaphore to unlocked state
    perror("semid");
    return -1;
  }

  //user semaphores are locked at start, so user can block, waiting for resource
  int i;
  un.val = 0;
  for(i=1; i < 1 + MAX_RUNNING; i++){
  	if(semctl(semid, i, SETVAL, un) ==-1){ //set the semaphore to unlocked state
  		perror("semid");
  		return -1;
  	}
  }


  sys->oss_clock.seconds = sys->oss_clock.nanoseconds = 0;

  //init the system resources
  for(i=0; i < RMAX; i++){
    sys->resources[i] =
    sys->maxres[i]    = 1 + rand() % (RMAX_VALUE-1);
  }

  //set which of them are shareable
  const int share_percentage = 15 + (rand() % 10);  // [15; 25] of the resources will be shared
  int shared_count = RMAX / ( 100 / share_percentage);

  printf("Marking %d resources as shared: ", shared_count);

  i = 0;
  while(i < shared_count){
    int r = rand() % RMAX;
    if(is_shareable(r) == 0){
      i++;
      set_shareable(r);
      printf("R%d ", r);
    }
  }
  printf("\n");

  return 0;
}

int main(const int argc, char * const argv[]){

  if(argc == 2){
    if(strcmp(argv[1], "-v") == 0){
      verbose = 1;
    }else{
      fprintf(stderr, "Usage: ./oss <-v>\n");
      return EXIT_FAILURE;
    }
  }

  signal(SIGTERM, signal_handler);
  signal(SIGALRM, signal_handler);
	signal(SIGCHLD, SIG_IGN);

  //alarm(MAX_RUNTIME);
  stdout = freopen("log.txt", "w", stdout);

  sys = attach_shm();
  if(sys == NULL)
    return EXIT_FAILURE;

  init_simulator();
  run_simulator();

  clean_exit(EXIT_SUCCESS);
}
