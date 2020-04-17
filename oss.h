#include "virt_clock.h"

//quantum in nanoseconds
#define QUANTUM 1000000

#define SHARED_PATH "/home"
#define SEMAPHORE_KEY 1234
#define MEMORY_KEY 5678


#define MAX_RUNNING 18
#define RMAX	20
#define RMAX_VALUE 10

enum pstate	{ DEAD=0, READY, WAITING, TERMINATED, PROC_STATES};
enum raction { REQUEST=0, RELEASE};       //resource action
enum rstate { WAITFOR=0, GRANTED, BLOCKED, DENIED};  //request state

struct request{
  int r;  //index in system:resouces[]
  int val;  //[1-10]
  enum raction op;
  enum rstate  res;
};

struct proc_info{	//process information
	int pid;
	int id;
	int state;
  int usage[RMAX];    //resource usage
  struct request rq;
};

struct system{
  struct proc_info procs[MAX_RUNNING];
  struct virt_clock oss_clock;

  int resources[RMAX];  //R1, R2 ...
  int maxres[RMAX];
};

union semun {
		int val;
		struct semid_ds *buf;
		unsigned short  *array;
};
