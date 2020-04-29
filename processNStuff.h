#ifndef PROCESS_N_STUFF_H
#define PROCESS_N_STUFF_H
#include "linux_list.h"
#define MAX_P_NAME_LEN 32
struct processBlock{
	char name[MAX_P_NAME_LEN];
	int startT, exeT, remainT;
	pid_t pID;
	// Andddd now I have to change EVERYTHING. Nice. fml
	int tTbl_offset;
} typedef process;

// All processes that belongs in the same bucket (same arrival time)
struct processBucketList{
	process *task;
	list next;
} typedef processList;

struct processBucketTree{
	int arrival;
	list pList_head;
	list next;
} typedef processBucket;

struct childProcessTimeInfo{
    long start_Sec, start_nSec;
    long end_Sec, end_nSec;
} typedef childTimeInfo;

struct pipeCollection{
	int _pipeID[2];
} typedef pipeInfo;

#define EXECUTE_ONE_T() \
	volatile unsigned long i; for (i=0; i<1000000UL; i++);

int qsort_cmp(const void *ptr1, const void *ptr2) {
	// If processes arrive @ same time: order first-comer first
	if (((process *)ptr1)->startT == ((process *)ptr2)->startT) {
		return (ptr1-ptr2);
	}
	return (((process *)ptr1)->startT - ((process *)ptr2)->startT);
}

// Debugging code segment from Jonathan Leffler on StackOverflow
void dbg_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    return;
}

void processList_add(list *pList_head, process *toAdd) {
	// Create new processList entry & add to its list
	processList *newProcess = NULL;
	newProcess = (processList *)malloc(sizeof(processList));
    INIT_LIST_HEAD(&(newProcess->next));
	list_add_tail(&(newProcess->next), pList_head);
	// Initialize that new entry
	newProcess->task = toAdd;
	return;
}

// Adds new bucket to existing list of buckets
void processBucket_add(list *target, int arriveT) {
	// Create new processBucket entry & add to its list
	processBucket *newBucket = NULL;
	newBucket = (processBucket *)malloc(sizeof(processBucket));
    INIT_LIST_HEAD(&(newBucket->next));
	list_add_tail(&(newBucket->next), target);
	// Create new, empty processList field
	INIT_LIST_HEAD(&(newBucket->pList_head));
	newBucket->arrival = arriveT;
	return;
}

#endif