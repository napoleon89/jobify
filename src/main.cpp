#include <stdio.h>
#include <windows.h>
#include "std.h"
#include <atomic>
global_variable bool g_running = false;
struct ThreadData {
	u32 id;
	u8 core;	
};
#define WORKER_COUNT 5

global_variable u64 g_perf_freq = 0;

internal_func f64 getWallClockMS() {
	LARGE_INTEGER li = {};
	QueryPerformanceCounter(&li);
	return ((f64)li.QuadPart / (f64)g_perf_freq) * 1000.0f;
}

global_variable u32 g_tls_index = 0;

char *g_function_names[];
void *g_function_ptrs[];
int g_func_count;

#define FuncDebug(ptr) { \
	int index = __COUNTER__; \
	g_function_names[index] = __FUNCTION__; \
	g_function_ptrs[index] = ptr; \
}

#include "async.cpp"

const DWORD MS_VC_EXCEPTION = 0x406D1388;  
#pragma pack(push,8)  
typedef struct tagTHREADNAME_INFO  
{  
    DWORD dwType; // Must be 0x1000.  
    LPCSTR szName; // Pointer to name (in user addr space).  
    DWORD dwThreadID; // Thread ID (-1=caller thread).  
    DWORD dwFlags; // Reserved for future use, must be zero.  
 } THREADNAME_INFO;  
#pragma pack(pop)  
void SetThreadName(DWORD dwThreadID, const char* threadName) {  
    THREADNAME_INFO info;  
    info.dwType = 0x1000;  
    info.szName = threadName;  
    info.dwThreadID = dwThreadID;  
    info.dwFlags = 0;  
	#pragma warning(push)  
	#pragma warning(disable: 6320 6322)  
    __try{  
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);  
    }  
    __except (EXCEPTION_EXECUTE_HANDLER){  
    }  
	#pragma warning(pop)  
}

global_variable std::atomic<u32> g_counter = 0;
global_variable JobQueue g_job_queue;


global_variable ThreadData g_thread_data[WORKER_COUNT];
global_variable WaitList g_wait_list;

internal_func void animateObjects(Fiber *fiber, void *input);

internal_func void yield(bool free) {
	FuncDebug(&yield);
	g_job_graph->pushCall(&yield);
	void *data = GetFiberData();
	Fiber *fiber = (Fiber *)data;
	while(g_running) {
		Fiber *execute_fiber = 0;
		{
			Fiber *new_fiber = g_wait_list.grabFiber();
			if(new_fiber != 0) {
				if(new_fiber->getFunc() == &animateObjects) {
					printf("\n");
				}
				execute_fiber = new_fiber; 
			}
		}
		
		if(execute_fiber == 0) {
			Job job;
			Fiber *new_fiber = 0;
			if(g_job_queue.popAndAssignFiber(job, &new_fiber)) {		
				execute_fiber = new_fiber;
				new_fiber->assign(job.func, job.param, job.counter);
			}
		}
		
		if(execute_fiber != 0) {
			if(free && fiber != 0) g_fiber_pool.free(fiber);
			execute_fiber->run();
			break;
		}
	}
}

internal_func void waitForCounterAndFree(Counter *counter, s32 value) {
	Fiber *fiber = getFiber();
	counter->setExpected(value);
	g_wait_list.add(fiber, counter);
	yield(false);
	// delete counter;
}

internal_func void runJobs(Job *jobs, u32 job_count, Counter **counter) {
	Fiber *fiber = getFiber();
	
	*counter = new Counter();
	(*counter)->setValue(job_count);
	
	for(u32 i = 0; i < job_count; i++) {
		jobs[i].counter = *counter;
		g_job_queue.push(jobs[i]);
	}
}

internal_func int threadProc(void *input) {
	ThreadData *data = (ThreadData *)input;
	TlsSetValue(g_tls_index, data);
	FuncDebug(&threadProc);
	g_job_graph->reset(data->id);
	
	g_job_graph->pushCall(&threadProc);
	
	printf("Executing thread %d on core %d\n", data->id, data->core);
	ConvertThreadToFiber(0);
	yield();
	return 0;
}

struct Thread {
	HANDLE handle;
	DWORD thread_id;	
};

internal_func Thread createWorkerThread(u8 index, u8 core) {
	Thread result = {};
	g_thread_data[index].core = core;
	g_thread_data[index].id = index;
	result.handle = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&threadProc, &g_thread_data[index], CREATE_SUSPENDED, &result.thread_id);
	Assert(result.handle != NULL);
	SetThreadName(result.thread_id, formatString("Worker thread %d", index));
	
	SetThreadPriority(result.handle, THREAD_PRIORITY_HIGHEST);
	u32 mask = 1 << core;
	SetThreadAffinityMask(result.handle, mask);
	ResumeThread(result.handle);
	
	
	return result;
}

struct Entity {
	f32 x, y;	
};

internal_func void calcCollisionDelta(Fiber *fiber, void *input) {
	// printf("%s\n", __FUNCTION__);
	FiberDebug;
	FuncDebug(&calcCollisionDelta);
	f32 *data = (f32 *)input;
	*data = 0.5f;
}

internal_func void checkCollisions(Fiber *fiber, void *input) {
	// printf("%s\n", __FUNCTION__);
	FiberDebug;
	FuncDebug(&checkCollisions);
	Entity *entity = (Entity *)input;

	f32 delta = 0.0f;
	Job job = {&calcCollisionDelta, &delta};
	Counter *counter = 0;
	runJobs(&job, 1, &counter);
	waitForCounterAndFree(counter, 0);
	
	for(int i = 0; i < 300; i++) {
		entity->x += delta;
	}
}

#define ENTITY_COUNT 64
global_variable Entity g_entities[ENTITY_COUNT];
global_variable std::atomic<u32> entity_counter = 0;

internal_func void animateEntity(Fiber *fiber, void *input) {
	// printf("%s\n", __FUNCTION__);
	FiberDebug;
	FuncDebug(&animateEntity);
	Entity *entity = (Entity *)input;
	entity_counter.fetch_add(1);
	
	Job job = {&checkCollisions, entity};
	
	Counter *counter = 0;
	runJobs(&job, 1, &counter);
	waitForCounterAndFree(counter, 0);
	// entity->x += 100;
	entity->y += 10;
}

global_variable bool g_ran = false;

internal_func void animateObjects(Fiber *fiber, void *input) {
	// printf("%s\n", __FUNCTION__);
	FiberDebug;
	FuncDebug(&animateObjects);
	
	Job jobs[ENTITY_COUNT];
	for(int i = 0; i < ENTITY_COUNT; i++) {
		jobs[i].func = &animateEntity;
		jobs[i].param = &g_entities[i];
	}
	Counter *counter = 0;
	runJobs(&jobs[0], ENTITY_COUNT, &counter);
	waitForCounterAndFree(counter, 0);
	printf("%s\n", __FUNCTION__);
	printf("Counter was %d\n", counter->getValue());
	for(int i = 0; i < ENTITY_COUNT; i++) {
		printf("\n");
	}
	
	g_ran = true;
}

internal_func char *getFunctionName(void *ptr) {
	if(ptr == 0) return 0;
	char *result = 0;
	for(int i = 0; i < __COUNTER__; i++) {
		if(g_function_ptrs[i] == ptr) {
			result = g_function_names[i];
			break;
		}
	}
	// NOTE(nathan): the function called was note put into the name map
	Assert(result != 0);
	return result;
}

internal_func void printPipe(JobPipe *pipe) {
	for(int i = 0; i < JOB_GRAPH_MAX-1; i++) {
		f64 time = pipe->timestamps[i];
		if(pipe->functions[i+1] == 0) {
			continue;
		}
		f64 next_time = pipe->timestamps[i+1];
		f64 delta = next_time - time;
		printf("%-18s %.4fms - %.4fms\n", getFunctionName(pipe->functions[i]), time, delta);
	}
}

int main(int arg_count, char *args[]) {
	printf("Welcome to jobify!\n");
	printf("%s\n", __FUNCTION__);
	u32 proc_affinity = 0;
	u32 sys_affinity = 0;
	bool result = GetProcessAffinityMask(GetCurrentProcess(), (PDWORD_PTR)&proc_affinity, (PDWORD_PTR)&sys_affinity);
	LARGE_INTEGER li = {};
	QueryPerformanceFrequency(&li);
	g_perf_freq = li.QuadPart;
	
	g_tls_index = TlsAlloc();
	if(g_tls_index == TLS_OUT_OF_INDEXES) {
		printf("Out of tls indices\n");
	}
	
	g_running = true;
	int param = 0;
	
	g_fiber_pool.init();
	
	Job job;
	job.func = &animateObjects;
	job.param = 0;
	g_job_queue.push(job);
	
	
	
	for(u32 i = 0; i < WORKER_COUNT; i++) {
		createWorkerThread(i, i+1);	 // NOTE(nathan): ignore first core
	}
	
	while(g_running) {
		Sleep(10);
		// char c = getchar();
		// switch(c) {
		// 	case 'q': {
		// 		g_running = false;
		// 	} break;
				
		// 	case 'p': {
		// 		// u32 counter = g_counter.load();
		// 		// printf("%d\n", counter);
		// 	} break;
		// }
		if(g_ran) {
			// for(int i = 0; i < JOB_GRAPH_MAX-1; i++) {
			// 	bool did_write = false;
			// 	for(int j = 0; j < WORKER_COUNT; j++) {
					
			// 	}
			// 	if(did_write) printf("\n");
			// }
			
			f64 highest = 0.0f;
			int highest_index = 0;
			for(int i = 0; i < WORKER_COUNT; i++) {
				JobPipe *pipe = &g_job_graph->pipes[i];
				f64 time = pipe->timestamps[pipe->index-1];
				if(time > highest) {
					highest = time;
					highest_index = i;
				}
				printf("Worker %d took %.3fms \n", i, time);
			}
			
			JobPipe *pipe = &g_job_graph->pipes[highest_index];
			printPipe(pipe);
			printf("Highest was thread %d which took %.3fms and last task was %s\n", highest_index, highest, getFunctionName(pipe->functions[pipe->index-2]));
			
			break;
		}
	}
	
	
	getchar();
	
	
	return 0;
}

// global_variable const int g_job_count = __COUNTER__;
 char *g_function_names[__COUNTER__];
 void *g_function_ptrs[__COUNTER__-1];
