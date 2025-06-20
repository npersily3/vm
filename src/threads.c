#include "windows.h"
#include "stdio.h"

ULONG NumberOfThreadsCompleted;
ULONG TotalNumbers;

volatile ULONG GlobalThreadCounter;

CRITICAL_SECTION GlobalCriticalSection;

#define DEFAULT_SECURITY        ((LPSECURITY_ATTRIBUTES) NULL)
#define DEFAULT_STACK_SIZE      0
#define DEFAULT_CREATION_FLAGS  0

#define AUTO_RESET              FALSE

typedef struct _THREAD_INFO {

    ULONG ThreadNumber;

    ULONG ThreadId;
    HANDLE ThreadHandle;

    volatile ULONG ThreadCounter;

    HANDLE WorkDoneHandle;

#if 1

    // // way faster, now everything consumes 1 cache line
    // What effect would consuming extra space here have ?
    //

    volatile UCHAR Pad[32];

#endif

} THREAD_INFO, *PTHREAD_INFO;

#define MAXIMUM_NUMBER_OF_THREADS 4

ULONG
CacheContentionWorker (
    _In_ PVOID Context
    )
{
	ULONG i;
    ULONG ThreadNumber;

    PTHREAD_INFO ThreadContext;

    ThreadContext = (PTHREAD_INFO) Context;

    ThreadNumber = ThreadContext->ThreadNumber;

	for (i = 0; i < 100000000; i += 1) {

        //
        // No synchronization needed here because each thread that runs
        // in this function is accessing his own data.
        //

		ThreadContext->ThreadCounter += 1;

#if 1

        //
        // What effect would disabling this counter have ?
        // It makes it way slower because it is constantly pinponging arround (about 13 times (one test))

        GlobalThreadCounter += 1;

#endif
		// EnterCriticalSection (&GlobalCriticalSection);
		//
		// TotalNumbers += 1;
		//
		// LeaveCriticalSection (&GlobalCriticalSection);

	}

    //
    // Synchronization is needed here because all threads will access
    // the same global data cell below.
    //

    EnterCriticalSection (&GlobalCriticalSection);

    NumberOfThreadsCompleted += 1;
	
	LeaveCriticalSection (&GlobalCriticalSection);

    SetEvent (ThreadContext->WorkDoneHandle);

    //
    // Thread exit issues a SetEvent which will wake anyone waiting on
    // the thread's handle.
    //

	return 0;
}

VOID
UseThreads (
    LPTHREAD_START_ROUTINE ThreadFunction
    )
{
	ULONG i;
	BOOL ReturnValue;
	ULONG NumberOfThreads;
    LPVOID ThreadParameter;
    PTHREAD_INFO ThreadContext;
    HANDLE Handle;
    ULONG StartTime;
    ULONG EndTime;
    THREAD_INFO ThreadInfo[MAXIMUM_NUMBER_OF_THREADS] = {0};
	
    GlobalThreadCounter = 0;

    NumberOfThreadsCompleted = 0;
	TotalNumbers = 0;

    StartTime = GetTickCount ();

	NumberOfThreads = MAXIMUM_NUMBER_OF_THREADS;

    for (i = 0; i < NumberOfThreads; i += 1) {

        ThreadContext = &ThreadInfo[i];

        ThreadContext->ThreadNumber = i;

        ThreadContext->WorkDoneHandle = CreateEvent (NULL,
                                                     AUTO_RESET,
                                                     FALSE,
                                                     NULL);

        if (ThreadContext->WorkDoneHandle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create work event %x\n", ReturnValue);
            return;
        }

	    Handle = CreateThread (DEFAULT_SECURITY,
                               DEFAULT_STACK_SIZE,
                               ThreadFunction,
                               ThreadContext,
                               DEFAULT_CREATION_FLAGS,
                               &ThreadContext->ThreadId);

	    if (Handle == NULL) {
            ReturnValue = GetLastError ();
            printf ("could not create thread %x\n", ReturnValue);
            return;
        }

	    ThreadContext->ThreadHandle = Handle;
    }

    //
    // Wait for each of our threads to finish his work.
    //

		// can do wait for multiple
    for (i = 0; i < NumberOfThreads; i += 1) {

        ThreadContext = &ThreadInfo[i];

        WaitForSingleObject (ThreadContext->WorkDoneHandle, INFINITE);
    }

    //
    // Note we are the parent thread.  If we just go ahead and return (exit)
    // then the operating system will terminate all our child threads wherever
    // they are executing and without giving them any chance to clean up.
    //
    // So we'll wait for them here so they can finish the work we assigned them.
    //



    for (i = 0; i < NumberOfThreads; i += 1) {

        ThreadContext = &ThreadInfo[i];

        ReturnValue = WaitForSingleObject (ThreadContext->ThreadHandle,
                                           INFINITE);

        if (ReturnValue == 0) {

	        ULONG ExitCode;

            ReturnValue = GetExitCodeProcess (ThreadContext->ThreadHandle,
                                              &ExitCode);
        }
    }

    EndTime = GetTickCount ();

    printf ("Cache contention elapsed time : %u milliseconds using %d threads\n",
            EndTime - StartTime,
            NumberOfThreadsCompleted);

    for (i = 0; i < NumberOfThreads; i += 1) {

        ThreadContext = &ThreadInfo[i];

        printf ("Thread %u final counter value  : %u\n",
                i,
                ThreadContext->ThreadCounter);
    }

    printf ("Final aggregate counter value : %u\n Final other thing. %u", GlobalThreadCounter, TotalNumbers);

	return;
}

__cdecl
main (
    _In_ int argc,
    _In_ PCHAR argv[]
    )
{
	ULONG i;
	BOOL ReturnValue;
	ULONG NumberOfThreads;
    LPVOID ThreadParameter;
    PTHREAD_INFO ThreadContext;
    ULONG StartTime;
    ULONG EndTime;
	
    UNREFERENCED_PARAMETER (argc);
    UNREFERENCED_PARAMETER (argv);

	InitializeCriticalSection (&GlobalCriticalSection);

    UseThreads (CacheContentionWorker);

	return 0;
}
