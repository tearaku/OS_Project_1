#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

/* Debugging code segment from Jonathan Leffler on StackOverflow */
#ifdef DEBUG
#define DEBUG_FLAG 1
#else
#define DEBUG_FLAG 0
#endif
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
void dbg_printf(const char *fmt, ...);
// Usage: TRACE(("msg with %d", var));
#define TRACE(x) do {if (DEBUG_FLAG) dbg_printf x;} while (0)

#include "processNStuff.h"
#define MAX_INT 4294967295
#define ONE_RR_SLICE 500
// Custom syscall IDs
#define GET_SYS_TIME 334
#define PRINT_TIME 335

void sched_FIFO();
void sched_RR();
void sched_SJF();
void sched_PSJF();
void FIFO_child();
void RR_child();
void SJF_child();
void PSJF_child();
void child_ini(cpu_set_t *childMask, int taskTbl_offset, childTimeInfo *pTime);
void child_fin(childTimeInfo *pTime, int taskTbl_offset);
void sig_setup(int sigNum);
void pipe_setup(int child_offset);
void pipe_cleanup(int childMode, int child_offset);
void raw_Input_Process(int numOfInput);

process *taskTbl = NULL;
list taskPool;
sigset_t sigSet; sigset_t waitSet;
struct sigaction sigAct;
char sched_policy[5];
// Used for child to easily (&lazily) index their process name; AKA: legacy of laziness biting my butt later on QQ
int taskTbl_offset = 0;
// Pipes for PSJF scheduling
pipeInfo childPipe[ONE_RR_SLICE];

int main() {
	/* RECORD THE BEGINNING OF MAIN() !!!! For own testing la */
#ifdef DEBUG
long main_sec; long main_nsec;
syscall(GET_SYS_TIME, &main_sec, &main_nsec);
TRACE(("%smain(): begun at %ld.%ld%s\n", KYEL, main_sec, main_nsec, KNRM));
#endif
	INIT_LIST_HEAD(&taskPool);
	int numOfTask = -1;
	scanf("%s", &sched_policy);
	scanf("%d", &numOfTask);
	// Set scheduler to core #2 --> for running task (maks inherits)
	cpu_set_t schedMask;
	CPU_ZERO(&schedMask);
	CPU_SET(2, &schedMask);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &schedMask) == -1) {
TRACE(("%smain(): sched_setaffinity() failed!%s\n", KRED, KNRM));
		exit(EXIT_FAILURE);
	}
	// Setup signal masks and handlers (both scheduler & tasks use the same)
	// Blocks SIGUSR1 & sets handler for it
	sig_setup(SIGUSR1);
TRACE(("Finished setting up signals!\n"));
	// (Raw)-Process Input Data
	taskTbl = (process *)malloc(sizeof(process)*numOfTask);
	raw_Input_Process(numOfTask);
TRACE(("raw_Input_Process: DONE!\n"));
	// fork() all tasks & suspend them first
	list *pBucketIter;
	list *pListIter;
	processBucket *bucketEntry;
	processList *listEntry;
	list_for_each(pBucketIter, &taskPool) {
		bucketEntry = list_entry(pBucketIter, processBucket, next);
		list_for_each(pListIter, &(bucketEntry->pList_head)) {
			listEntry = list_entry(pListIter, processList, next);
			// Setup pipes if PSJF scheduling
			if (sched_policy[0] == 'P') {
				if (pipe(childPipe[taskTbl_offset]._pipeID) == -1) {
TRACE(("%spipe error!%s\n", KRED, KNRM));
					exit(EXIT_FAILURE);
				}
			}
			// Fork them child! But do not eat them, we ain't Zeus's dad
			if ((listEntry->task->pID = fork()) == 0) {
				switch(sched_policy[0]) {
					case 'F':
						FIFO_child(); break;
					case 'R':
						RR_child(); break;
					case 'S':
						// FIFO & SJF --> both child executes all at once
						FIFO_child(); break;
					case 'P':
						PSJF_child();
						// Parent: close read end
						pipe_cleanup(0, taskTbl_offset); break;
					default:
						printf("Get off my damn lawn!, says the grumpy old man.\n");
				}
			}
			listEntry->task->tTbl_offset = taskTbl_offset;
TRACE(("%s[%s||%d] gets pipeIDs of: {%d} and {%d}%s ||| taskTbl_offset=%d\n", KBLU, listEntry->task->name, listEntry->task->pID, childPipe[taskTbl_offset]._pipeID[0], childPipe[taskTbl_offset]._pipeID[1], KNRM, listEntry->task->tTbl_offset));
TRACE(("[bucket = %d] child [%d] is processed...\n", bucketEntry->arrival, listEntry->task->pID));
			taskTbl_offset++;
		}
	}
	// Set scheduler to core #1
	CPU_ZERO(&schedMask);
	CPU_SET(1, &schedMask);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &schedMask) == -1) {
TRACE(("%smain(): sched_setaffinity() failed!%s\n", KRED, KNRM));
		exit(EXIT_FAILURE);
	}
	// Begin scheduler
	switch(sched_policy[0]) {
		case 'F':
			sched_FIFO(); break;
		case 'R':
			sched_RR(); break;
		case 'S':
			sched_SJF(); break;
		case 'P':
			sched_PSJF(); break;
		default:
			printf("Get off my damn lawn!, says the grumpy old man.\n");
	}
TRACE(("scheduler done!\n"));
	return 0;
}

/*** First In First Out (FIFO) ***/
void FIFO_child() {
	cpu_set_t childMask;
	childTimeInfo pTime;
	child_ini(&childMask, taskTbl_offset, &pTime);
	for (volatile int t = 0; t < (taskTbl+taskTbl_offset)->exeT; t++) {
		EXECUTE_ONE_T();
	}
	// Signal scheduler and exit
	kill(getppid(), SIGUSR1);
	child_fin(&pTime, taskTbl_offset);
}

void sched_FIFO() {
TRACE(("FIFO_scheduler: begin!\n"));
	list *pBucketIter;
	list *pListIter;
	processBucket *bucketEntry;
	processList *listEntry;
	volatile int sched_Timer = 0;
	// Remember to makeup for time difference --> since task arrival is measured from beginning of main()
	// ...
	list_for_each(pBucketIter, &taskPool) {
		bucketEntry = list_entry(pBucketIter, processBucket, next);
		// If no jobs are avaliable yet, continue execution (AKA idling)
		if (sched_Timer < bucketEntry->arrival) {
			for (sched_Timer; sched_Timer < bucketEntry->arrival; sched_Timer++) {
				EXECUTE_ONE_T()
			}
		}
		list_for_each(pListIter, &(bucketEntry->pList_head)) {
			listEntry = list_entry(pListIter, processList, next);
TRACE(("Waking child [%d] up...\n", listEntry->task->pID));
			// Wake the child up and suspend self until signaled by child
			kill(listEntry->task->pID, SIGUSR1);
			sched_Timer += listEntry->task->exeT;
			sigsuspend(&waitSet);
TRACE(("[FIFO] child done! scheduler seeking next job...\n"));
		}
	}
	return;
}

/*** Round Robin (RR) ***/
void RR_child() {
	cpu_set_t childMask;
	childTimeInfo pTime;
	child_ini(&childMask, taskTbl_offset, &pTime);
	do {
		const int t_curRound = ((taskTbl+taskTbl_offset)->remainT > ONE_RR_SLICE) ? ONE_RR_SLICE: (taskTbl+taskTbl_offset)->remainT;
		for (volatile int t = 0; t < t_curRound; t++) {
			EXECUTE_ONE_T();
		}
		(taskTbl+taskTbl_offset)->remainT = (taskTbl+taskTbl_offset)->remainT - ONE_RR_SLICE;
		// Signal scheduler & suspend again
		kill(getppid(), SIGUSR1);
		if ((taskTbl+taskTbl_offset)->remainT > 0)
			sigsuspend(&waitSet);
	} while ((taskTbl+taskTbl_offset)->remainT > 0);
	// If task is now finished, exitto!
	child_fin(&pTime, taskTbl_offset);
	return;
}

void sched_RR() {
TRACE(("%sRR_scheduler: begin!%s\n", KCYN, KNRM));
	list *pBucketIter;
	list *pListIter;
	processBucket *bucketEntry;
	processList *listEntry;
	volatile int sched_Timer = 0;
	// Remember to makeup for time difference --> since task arrival is measured from beginning of main()
	// ...
	// Keep looping until no more entries / processes are left in taskPool
	int firstTime = 1;
	while (!list_empty(&taskPool)) {
		list_for_each(pBucketIter, &taskPool) {
			bucketEntry = list_entry(pBucketIter, processBucket, next);
			// Test if empty of jobs
			if (list_empty(&(bucketEntry->pList_head))) {
TRACE(("%sdeleting%s bucket [arrival = %d] from bucketList...\n", KRED, KNRM, bucketEntry->arrival));
				list *bucket_toDel = pBucketIter;
				pBucketIter = pBucketIter->prev;
				list_del(bucket_toDel);
				continue;
			}
			// If no jobs are avaliable yet, continue execution (AKA idling); only for first loop (jobs haven't arrived / no pending jobs)
			if (firstTime && (sched_Timer < bucketEntry->arrival)) {
				for (sched_Timer; sched_Timer < bucketEntry->arrival; sched_Timer++) {
					EXECUTE_ONE_T()
				}
			}
			list_for_each(pListIter, &(bucketEntry->pList_head)) {
				listEntry = list_entry(pListIter, processList, next);
TRACE(("Waking child [%d] up...\n", listEntry->task->pID));
				// Wake the child up and suspend self until signaled by child
				kill(listEntry->task->pID, SIGUSR1);
				sigsuspend(&waitSet);
				// Update process's remainT value
				listEntry->task->remainT = listEntry->task->remainT - ONE_RR_SLICE;
				// Update scheduler timer
				sched_Timer += ONE_RR_SLICE;
				// Delete task from taskPool if its done (remainT <= 500 this round)
				if (listEntry->task->remainT <= 0) {
TRACE(("%sdeleting%s child [%s || %d] from processList\n", KRED, KNRM, listEntry->task->name, listEntry->task->pID));
					// Re-adjust sched_Timer
					sched_Timer -= -(listEntry->task->remainT);
					list *list_toDel = pListIter;
					pListIter = pListIter->prev;
					list_del(list_toDel);
					continue;
				}
			}
		}
		firstTime = 0;
	}
	return;
}

void sched_SJF() {
TRACE(("%sSJF_scheduler: begin!%s\n", KCYN, KNRM));
	list *pBucketIter = (&taskPool)->next;
	list *pListIter = NULL;
	processBucket *bucketEntry;
	processList *listEntry;
	volatile int sched_Timer = 0;
	list queue;
	INIT_LIST_HEAD(&queue);
	list *queueIter;
	processList *queueEntry;
	// Brute force the seeking of min: search for all arrived tasks with smallest exeT (record only 1st matching one)
	while ((pBucketIter != (&taskPool)) || !list_empty(&queue)) {
		// If there are still tasks to be feteched into queue
		if (pBucketIter != (&taskPool)) {
			// Keep advancing (bucket-wise) until all schedulable tasks are scanned
			do {
				// If idling is needed before tasks arrive, or, more tasks have arrived as of now
				bucketEntry = list_entry(pBucketIter, processBucket, next);
				if (list_empty(&queue) || (sched_Timer >= bucketEntry->arrival)) {
					// Add to queue
					list_for_each(pListIter, &(bucketEntry->pList_head)) {
						listEntry = list_entry(pListIter, processList, next);
TRACE(("%sadding [%s || %d] to queue...%s\n", KYEL, listEntry->task->name, listEntry->task->pID, KNRM));
						processList_add(&queue, listEntry->task);
					}
					// Idle the difference away (if any)
					for (sched_Timer; sched_Timer < bucketEntry->arrival; sched_Timer++) {
						EXECUTE_ONE_T()
					}
					// Advance to next bucket
					pBucketIter = pBucketIter->next;
				}
				// Manually break out if reached end
				if (pBucketIter == (&taskPool))
					break;
				else
					bucketEntry = list_entry(pBucketIter, processBucket, next);
			} while (sched_Timer >= bucketEntry->arrival);
		}
		// If can execute a task right now
		processList *shortestJob = NULL;
		list_for_each(queueIter, &queue) {
			queueEntry = list_entry(queueIter, processList, next);
			if ((shortestJob == NULL) || (queueEntry->task->exeT < shortestJob->task->exeT))
				shortestJob = queueEntry;
		}
		// Execute shortest task!
TRACE(("Waking child [%d] up...\n", shortestJob->task->pID));
		// Wake the child up
		kill(shortestJob->task->pID, SIGUSR1);
		// Update sched_Timer
		sched_Timer += shortestJob->task->exeT;
		sigsuspend(&waitSet);
		// Remove task from queue
TRACE(("%sdeleting%s child [%s || %d] from queue\n", KRED, KNRM, shortestJob->task->name, shortestJob->task->pID));
		list_del(&((*shortestJob).next));
	}
	return;
}

/*** Pre-emptive Shortest Job First (PSJF) ***/
void PSJF_child() {
	cpu_set_t childMask;
	childTimeInfo pTime;
	pipe_cleanup(1, taskTbl_offset);
	child_ini(&childMask, taskTbl_offset, &pTime);
	do {
		int t_curRound;
		if (read(childPipe[taskTbl_offset]._pipeID[0], &t_curRound, sizeof(t_curRound)) == -1) {
TRACE(("%schild [%s || %d] read from pipe, failed!%s\n", KRED, (taskTbl+taskTbl_offset)->name, (taskTbl+taskTbl_offset)->pID, KNRM));
			exit(EXIT_FAILURE);
		}
TRACE(("%schild [%s||%d]: read {%d} from pipe%s\n", KBLU, (taskTbl+taskTbl_offset)->name, (taskTbl+taskTbl_offset)->pID, t_curRound, KNRM));
		for (volatile int t = 0; t < t_curRound; t++) {
			EXECUTE_ONE_T();
		}
		(taskTbl+taskTbl_offset)->remainT = (taskTbl+taskTbl_offset)->remainT - t_curRound;
TRACE(("%schild [%s||%d]: remainT = %d%s\n", KBLU, (taskTbl+taskTbl_offset)->name, (taskTbl+taskTbl_offset)->pID, (taskTbl+taskTbl_offset)->remainT, KNRM));
		// Signal scheduler & suspend again
		kill(getppid(), SIGUSR1);
		if ((taskTbl+taskTbl_offset)->remainT > 0)
			sigsuspend(&waitSet);
	} while ((taskTbl+taskTbl_offset)->remainT > 0);
	// If task is now finished, exitto!
	child_fin(&pTime, taskTbl_offset);
	return;
}

void sched_PSJF() {
TRACE(("%sPSJF_scheduler: begin!%s\n", KCYN, KNRM));
	list *pBucketIter = (&taskPool)->next;
	list *pListIter = NULL;
	processBucket *bucketEntry;
	processList *listEntry;
	volatile int sched_Timer = 0;
	list queue;
	INIT_LIST_HEAD(&queue);
	list *queueIter;
	processList *queueEntry;
	// Brute force the seeking of min: search for all arrived tasks with smallest remainT (record only 1st matching one)
	while ((pBucketIter != (&taskPool)) || !list_empty(&queue)) {
		if (pBucketIter != (&taskPool)) {
			bucketEntry = list_entry(pBucketIter, processBucket, next);
			// Only idles if queue is empty
			if (list_empty(&queue)) {
				for (sched_Timer; sched_Timer < bucketEntry->arrival; sched_Timer++) {
					EXECUTE_ONE_T()
				}
			}
			// Add to queue
			list_for_each(pListIter, &(bucketEntry->pList_head)) {
				listEntry = list_entry(pListIter, processList, next);
	TRACE(("%sadding [%s || %d] to queue...%s\n", KYEL, listEntry->task->name, listEntry->task->pID, KNRM));
				processList_add(&queue, listEntry->task);
			}
			// Advance to next bucket
			pBucketIter = pBucketIter->next;
			bucketEntry = list_entry(pBucketIter, processBucket, next);
		}
		// If can execute a task right now
		processList *shortestJob = NULL;
		list_for_each(queueIter, &queue) {
			queueEntry = list_entry(queueIter, processList, next);
			if ((shortestJob == NULL) || (queueEntry->task->remainT < shortestJob->task->remainT))
				shortestJob = queueEntry;
		}
		// Find how long this task can be executed without being pre-empted
		int cont_exeT;
		if (((sched_Timer + shortestJob->task->remainT) < bucketEntry->arrival) || (pBucketIter == (&taskPool))) {
			// If no more tasks to be fetched, just execute for remainder of time left
			cont_exeT = shortestJob->task->remainT;
		} else {
			cont_exeT = bucketEntry->arrival - sched_Timer;
		}
		// Execute shortest task!
TRACE(("Waking child [%d] up...\n", shortestJob->task->pID));
		// Wake the child up until either: a) exeT is up, or b) next batch arrives
TRACE(("%sattempt to write {%s%d%s} to fd = [%d]...%s\n", KCYN, KYEL, cont_exeT, KCYN, childPipe[shortestJob->task->tTbl_offset]._pipeID[1], KNRM));
		if (write(childPipe[shortestJob->task->tTbl_offset]._pipeID[1], &cont_exeT, sizeof(cont_exeT)) == -1) {
TRACE(("%sparent: failed to write to pipe!%s\n", KRED, KNRM));
			exit(EXIT_FAILURE);
		}
		kill(shortestJob->task->pID, SIGUSR1);
		// Update sched_Timer & task's remainT
		sched_Timer += cont_exeT;
		shortestJob->task->remainT -= cont_exeT;
		sigsuspend(&waitSet);
TRACE(("%stask [%s||%d]: remainT = %d%s\n", KYEL, shortestJob->task->name, shortestJob->task->pID, shortestJob->task->remainT, KNRM));
		// Remove task from queue (if its done)
		if (shortestJob->task->remainT == 0) {
TRACE(("%sdeleting%s child [%s || %d] from queue\n", KRED, KNRM, shortestJob->task->name, shortestJob->task->pID));
			list_del(&((*shortestJob).next));
		}
	}
	return;
}

void child_ini(cpu_set_t *childMask, int taskTbl_offset, childTimeInfo *pTime) {
	// Switch CPU
	CPU_ZERO(childMask);
	CPU_SET(2, childMask);
	if (sched_setaffinity(0, sizeof(cpu_set_t), childMask) == -1) {
TRACE(("%s[%d]: %ssched_setaffinity() failed!%s\n", KCYN, KRED, KNRM));
		exit(EXIT_FAILURE);
	}
TRACE(("child [%s||%d] first in! going to standby mode...\n", (taskTbl+taskTbl_offset)->name, getpid()));
	// Suspend until signaled
	sigsuspend(&waitSet);
	/* Awaken! */
TRACE(("child [%s||%d] awakens!\n", (taskTbl+taskTbl_offset)->name, getpid()));
	// Start timer!
	syscall(GET_SYS_TIME, &(pTime->start_Sec), &(pTime->start_nSec));
	return;
}

void child_fin(childTimeInfo *pTime, int taskTbl_offset) {
	syscall(GET_SYS_TIME, &(pTime->end_Sec), &(pTime->end_nSec));
	syscall(PRINT_TIME, getpid(), &(pTime->start_Sec), &(pTime->start_nSec), &(pTime->end_Sec), &(pTime->end_nSec));
TRACE(("%s%s [%d]: done!%s\n", KGRN, (taskTbl+taskTbl_offset)->name, getpid(), KNRM));
	// Print out pid & name
	printf("%s %d\n", (taskTbl+taskTbl_offset)->name, getpid());
	exit(EXIT_SUCCESS);
}

void raw_Input_Process(int numOfInput) {
TRACE(("Raw input processing: in!\n"));
	// Sort input in ascending order (arrival time)
	for (int i = 0; i < numOfInput; i++) {
		scanf("%s %d %d", (taskTbl + i)->name, &((taskTbl + i)->startT), &((taskTbl + i)->exeT));
		// Initialize remaining time (AKA = execution time)
		(taskTbl + i)->remainT = (taskTbl + i)->exeT;
	}
	qsort(taskTbl, numOfInput, sizeof(process), qsort_cmp);
	// Add bucket w/ earliest arrival time
	processBucket_add(&taskPool, taskTbl->startT);
	// Points to current bucket being worked on here
	processBucket *curBucket = list_entry((&taskPool)->next, processBucket, next);
TRACE(("curBucket->arrival: %d\n", curBucket->arrival));
	for (int i = 0; i < numOfInput; i++) {
		// It does NOT belong in current bucket, generate new one & label it
		if (curBucket->arrival < (taskTbl + i)->startT) {
TRACE(("adding a new bucket...\n"));
			processBucket_add(&taskPool, (taskTbl + i)->startT);
			// Seek tail list (taskPool.prev) contained by recently added bucket
TRACE(("updating curBucket to the new bucket...\n"));
			curBucket = list_entry(taskPool.prev, processBucket, next);
		}
TRACE(("adding to existing bucket...\n"));
		// Insert a process into the processList
		processList_add(&(curBucket->pList_head), (taskTbl+i));
TRACE(("for-loop: %d...done!\n", i));
	}
	return;
}

void context_switch() {
TRACE(("process [%d] caught SIGUSR1!\n", getpid()));
	return;
}

void sig_setup(int sigNum) {
	// Block SIGUSR1 in sigSet
	sigemptyset(&sigSet);
	sigaddset(&sigSet, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigSet, NULL);
	// Setup waitSet to be used in sigsuspend()
	sigfillset(&waitSet);
	sigdelset(&waitSet, SIGUSR1);
	sigAct.sa_handler = context_switch;
	// Might have to block SIGUSR1 here...
	sigemptyset(&sigAct.sa_mask);
	sigAct.sa_flags = 0;
	sigaction(sigNum, &sigAct, NULL);
	return;
}

void pipe_cleanup(int childMode, int child_offset) {
	close(childPipe[child_offset]._pipeID[childMode]);
	return;
}