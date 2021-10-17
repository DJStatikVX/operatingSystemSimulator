#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "ProcessorBase.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

// Functions prototypes
void OperatingSystem_PrepareDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_MoveToTheBLOCKEDState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
int OperatingSystem_ExtractFromSleeping();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_HandleClockInterrupt();
int OperatingSystem_GetExecutingProcessID();
void OperatingSystem_ReleaseMemory();

// Process states struct declaration
char * statesNames [5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID=PROCESSTABLEMAXSIZE-1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};

// Heap with blocked processes sort by when to wakeup
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;

char * queueNames [NUMBEROFQUEUES]={"USER","DAEMONS"};

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// Variable containing the number of raised clock interrupts
int numberOfClockInterrupts = 0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);

	// Operation for filling programs arrival time queue
	ComputerSystem_FillInArrivalTimeQueue();
	OperatingSystem_PrintStatus();

	// Initialization of the partition table
	OperatingSystem_InitializePartitionTable();

	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	if (numberOfProgramsInArrivalTimeQueue == 0 && numberOfNotTerminatedUserProcesses == 0) {
		OperatingSystem_ReadyToShutdown();
	}
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_PrepareDaemons(int programListDaemonsBase) {
  
	// Include a entry for SystemIdleProcess at 0 position
	programList[0]=(PROGRAMS_DATA *) malloc(sizeof(PROGRAMS_DATA));

	programList[0]->executableName="SystemIdleProcess";
	programList[0]->arrivalTime=0;
	programList[0]->type=DAEMONPROGRAM; // daemon program

	sipID=initialPID%PROCESSTABLEMAXSIZE; // first PID for sipID

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	baseDaemonsInProgramList=programListDaemonsBase;

}


// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// command lineand daemons programs
int OperatingSystem_LongTermScheduler() {

	int PID, i,
	numberOfSuccessfullyCreatedProcesses=0;

	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER && OperatingSystem_IsThereANewProgram() == YES; i++) {
		PID=OperatingSystem_CreateProcess(Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue));
		if (PID == NOFREEENTRY) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(103,ERROR,programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->executableName);
		} else if (PID == PROGRAMDOESNOTEXIST) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104,ERROR,programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->executableName,"--- it does not exist ---");
			OperatingSystem_ShowTime(ERROR);
		} else if (PID == PROGRAMNOTVALID) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104,ERROR,programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->executableName,"--- invalid priority or size ---");
		} else if (PID == TOOBIGPROCESS) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(105,ERROR,programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->executableName);
		} else if (PID == MEMORYFULL) {
			ComputerSystem_DebugMessage(144,ERROR,programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->executableName);
		} else {
			numberOfSuccessfullyCreatedProcesses++;

			if (programList[Heap_getFirst(arrivalTimeQueue, numberOfProgramsInArrivalTimeQueue)]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
		// Extract program from arrival time queue
		Heap_poll(arrivalTimeQueue,QUEUE_ARRIVAL,&numberOfProgramsInArrivalTimeQueue);
	}

	if (numberOfSuccessfullyCreatedProcesses > 0) {
		OperatingSystem_PrintStatus();
	}

	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {

	int PID;
	int processSize;
	int selectedPartition;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	if (PID == NOFREEENTRY) {
		return NOFREEENTRY;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(&programFile, executableProgram->executableName);	
	if (processSize == PROGRAMDOESNOTEXIST) {
		return PROGRAMDOESNOTEXIST;
	} else if (processSize == PROGRAMNOTVALID) {
		return PROGRAMNOTVALID;
	}

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	if (priority == PROGRAMNOTVALID) {
		return PROGRAMNOTVALID;
	}

	// Obtain enough memory space
	selectedPartition=OperatingSystem_ObtainMainMemory(processSize, PID, indexOfExecutableProgram);
	if (selectedPartition == TOOBIGPROCESS) {
		return TOOBIGPROCESS;
	} else if (selectedPartition == MEMORYFULL) {
		return MEMORYFULL;
	}

	// Load program in the allocated memory
	if (OperatingSystem_LoadProgram(programFile, partitionsTable[selectedPartition].initAddress, processSize) == TOOBIGPROCESS) {
		return TOOBIGPROCESS;
	}

	// Show message "Process [PID - executableName] moving to the [NEW] state\n"
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(111,SYSPROC,PID,programList[indexOfExecutableProgram]->executableName,statesNames[0]);

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, partitionsTable[selectedPartition].initAddress, processSize, priority, indexOfExecutableProgram);

	// Show partitions table before allocating memory
	OperatingSystem_ShowPartitionTable("before allocating memory");

	// Assign selected memory partition to process
	partitionsTable[selectedPartition].PID = PID;

	// Show assigned memory partition message after successful process creation
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(143,SYSMEM,selectedPartition,partitionsTable[selectedPartition].initAddress,partitionsTable[selectedPartition].size,PID,programList[indexOfExecutableProgram]->executableName);

	// Show partitions table after allocating memory
	OperatingSystem_ShowPartitionTable("after allocating memory");

	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);

	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID, int indexOfExecutableProgram) {
	int difference = INT_MAX;
	int bestPartition = -1;
	int maxSize = 0;
	int i;

	// Show message that process requests memory
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142,SYSMEM,PID,programList[indexOfExecutableProgram]->executableName,processSize);

	// Iterate through partitions
	for (i = 0; i < PARTITIONTABLEMAXSIZE && partitionsTable[i].initAddress != -1; i++){
		// Check only free partitions
		if (partitionsTable[i].PID == NOPROCESS) {
			// Check only partitions with enough space for process
			if (partitionsTable[i].size >= processSize) {
				if ((partitionsTable[i].size - processSize) < difference) {
					difference = partitionsTable[i].size - processSize;
					bestPartition = i;
				}
			}
		}
		if (partitionsTable[i].size > maxSize) {
			maxSize = partitionsTable[i].size;
		}
	}

	if (processSize > maxSize) {
		return TOOBIGPROCESS;
	}

	// Assign process to best adjusted partition, if found
	if (bestPartition != -1) {
		return bestPartition;
	}

	// If there isn't any proper partition
	if (processSize > MAINMEMORYSECTIONSIZE)
		return MEMORYFULL;

	// In other case, memory is full
	return MEMORYFULL;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].state=NEW;
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
		processTable[PID].queueID = DAEMONSQUEUE;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
		processTable[PID].copyOfAccumulatorRegister=0;
		processTable[PID].queueID = USERPROCESSQUEUE;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[processTable[PID].queueID] ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[processTable[PID].state],statesNames[1]);
		processTable[PID].state=READY;
	}
	// OperatingSystem_PrintReadyToRunQueue();
}

// Move a process to the BLOCKED state: it will be inserted, depending on its priority, in
// a queue of identifiers of BLOCKED processes
void OperatingSystem_MoveToTheBLOCKEDState(int PID) {
	processTable[PID].whenToWakeUp = abs(Processor_GetAccumulator()) + numberOfClockInterrupts + 1;
	if (Heap_add(PID, sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[processTable[PID].state],statesNames[3]);
		processTable[PID].state=BLOCKED;
	}
	// OperatingSystem_PrintReadyToRunQueue();
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;

	selectedProcess=OperatingSystem_ExtractFromReadyToRun();
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun() {
  
	int selectedProcess=NOPROCESS;
	int i;

	for (i = 0; i < NUMBEROFQUEUES; i++) {
		if (numberOfReadyToRunProcesses[i] > 0) {
			selectedProcess=Heap_poll(readyToRunQueue[i],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[i]);
			break;
		}
	}
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

// Return PID of more priority process in the BLOCKED queue
int OperatingSystem_ExtractFromSleeping() {
  
	int selectedProcess=NOPROCESS;

	selectedProcess=Heap_poll(sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses);
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	processTable[PID].state=EXECUTING;
	// Show message about state change to EXECUTING
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[1],statesNames[2]);
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	Processor_SetAccumulator(processTable[PID].copyOfAccumulatorRegister);

	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	
	// Load Accumulator saved for interrupt manager
	processTable[PID].copyOfAccumulatorRegister=Processor_GetAccumulator();

}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	OperatingSystem_ShowTime(INTERRUPT);

	switch (Processor_GetRegisterB()) {
		case DIVISIONBYZERO:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"division by zero");
			break;
		case INVALIDPROCESSORMODE:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid processor mode");
			break;
		case INVALIDADDRESS:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid address");
			break;
		case INVALIDINSTRUCTION:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid instruction");
			break;
	}

	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
  	
	// Show message about process leaving CPU state change
	processTable[executingProcessID].state=EXIT;

	// Show partitions table before releasing memory
	OperatingSystem_ShowPartitionTable("before releasing memory");

	// Release memory partition associated to terminating process
	OperatingSystem_ReleaseMemory();

	// Show partitions table after releasing memory
	OperatingSystem_ShowPartitionTable("after releasing memory");

	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,statesNames[EXECUTING],statesNames[EXIT]);

	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0 && numberOfProgramsInArrivalTimeQueue == 0) {
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN);
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	// Next process to be executed after SYSCALL_YIELD
	int processToExecuteID;
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;

		case SYSCALL_YIELD:
			// Show message: "Process [executingProcessID - executingProgramName] will transfer the control of the processor to process [PID - programName]\n"
			if (numberOfReadyToRunProcesses[processTable[executingProcessID].queueID] > 0 && processTable[Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID],numberOfReadyToRunProcesses[processTable[executingProcessID].queueID])].priority == processTable[executingProcessID].priority) {
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(115,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID],numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]),programList[processTable[Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID],numberOfReadyToRunProcesses[processTable[executingProcessID].queueID])].programListIndex]->executableName);
				OperatingSystem_PreemptRunningProcess();
				processToExecuteID = OperatingSystem_ShortTermScheduler();
				OperatingSystem_Dispatch(processToExecuteID);
				OperatingSystem_PrintStatus();
			}
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
			
		case SYSCALL_SLEEP:
			processToExecuteID = OperatingSystem_ShortTermScheduler();
			OperatingSystem_SaveContext(executingProcessID);
			OperatingSystem_MoveToTheBLOCKEDState(executingProcessID);
			OperatingSystem_Dispatch(processToExecuteID);
			OperatingSystem_PrintStatus();
			break;

		default:
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,systemCallID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT=9
			OperatingSystem_HandleClockInterrupt();
			break;
	}

}

//	Print ready to run processes in queue
void OperatingSystem_PrintReadyToRunQueue() {
	int i;
	int j;
	
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE);

	for (i = 0; i < NUMBEROFQUEUES; i++) {
		ComputerSystem_DebugMessage(109,SHORTTERMSCHEDULE,queueNames[i]);

		for (j = 0; j < numberOfReadyToRunProcesses[i]; j++) {
			if (j == 0) {
				ComputerSystem_DebugMessage(113, SHORTTERMSCHEDULE, readyToRunQueue[i][j].info, processTable[readyToRunQueue[i][j].info].priority);
			} else {
				ComputerSystem_DebugMessage(114, SHORTTERMSCHEDULE, readyToRunQueue[i][j].info, processTable[readyToRunQueue[i][j].info].priority);
			}
		}
		printf("\n");
	}
}

void OperatingSystem_HandleClockInterrupt() {
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120,INTERRUPT,numberOfClockInterrupts);

	int i;
	int processToWakeUp;
	int previousNumberOfSleepingProcesses = numberOfSleepingProcesses;

	if (numberOfSleepingProcesses > 0) {
		for (i = 0; i < numberOfSleepingProcesses; i++) {
			if (processTable[Heap_getFirst(sleepingProcessesQueue, numberOfSleepingProcesses)].whenToWakeUp == numberOfClockInterrupts) {
				processToWakeUp = OperatingSystem_ExtractFromSleeping();
				OperatingSystem_MoveToTheREADYState(processToWakeUp);
			} else {
				break;
			}
		}
	}

	if (numberOfSleepingProcesses < previousNumberOfSleepingProcesses) {
		OperatingSystem_PrintStatus();
	}

	OperatingSystem_LongTermScheduler();
	if (numberOfProgramsInArrivalTimeQueue == 0 && numberOfNotTerminatedUserProcesses == 0) {
		OperatingSystem_ReadyToShutdown();
	}

	int j;
	int executingProcessPriority;
	int processToExecuteID;
	executingProcessPriority = processTable[executingProcessID].priority;

	for (j = 0; j < NUMBEROFQUEUES; j++) {
		if (numberOfReadyToRunProcesses[j] > 0) {
			if (processTable[Heap_getFirst(readyToRunQueue[j], numberOfReadyToRunProcesses[j])].priority < executingProcessPriority) {
				processToExecuteID = OperatingSystem_ShortTermScheduler();
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,processToExecuteID,programList[processTable[processToExecuteID].programListIndex]->executableName);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(processToExecuteID);
				OperatingSystem_PrintStatus();
				break;
			}
		}
	}
}

int OperatingSystem_GetExecutingProcessID() {
	return executingProcessID;
}

void OperatingSystem_ReleaseMemory() {
	int i;
	for (i = 0; i < PARTITIONTABLEMAXSIZE && partitionsTable[i].initAddress != -1; i++) {
		if (partitionsTable[i].PID == executingProcessID) {
			partitionsTable[i].PID = NOPROCESS;
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(145,SYSMEM,i,partitionsTable[i].initAddress,partitionsTable[i].size,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;
		}
	}
}