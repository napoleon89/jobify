#include <xmmintrin.h>
#include <queue>



struct SpinLock {
	std::atomic<u32> locker;
	
	void lock() {
		while(true) {
	        if (locker.load() == 0) {
	            u32 expected = 0;
	            u32 store = 1;
	            if (locker.compare_exchange_strong(expected, store))
	                break;
	        }
	        _mm_pause();
	    }
	    
	    // printf("Locked\n");
	}	
	
	void unlock() {
		while(true) {
	        if (locker.load() == 1) {
	            u32 expected = 1;
	            u32 store = 0;
	            if (locker.compare_exchange_strong(expected, store))
	                break;
	        }
	        _mm_pause();
	    }
	    
	    // printf("Unlocked\n");
	}
};

// 1st u64 is param pointer
// 23rd u64 is function pointer
// struct Fiber {
// 	void *fiber_handle;
// };


internal_func void yield(bool free = true);
struct Fiber;

typedef void (JobFunction)(Fiber *fiber, void *);

struct Counter {
private:
	std::atomic<int> value;
	int expected;
	
public:
	void setExpected(int v) { 
		// spin_lock.lock();
		expected = v; 
		// spin_lock.unlock();	
	}
	
	void setValue(int v) { 
		// spin_lock.lock();
		value.store(v);
		// value = v; 
		// spin_lock.unlock();
	}
	
	int getValue() {
		int v = value.load();
		return v;
	}
	
	bool finished() {
		// spin_lock.lock();
		int v = value.load();
		bool result = v == expected;		
		// spin_lock.unlock();
		return result;
	}
	
	void sub() {
		// spin_lock.lock();
		value.fetch_sub(1);
		// value--;
		// spin_lock.unlock();
	}
};

#define JOB_GRAPH_MAX 500

struct JobPipe {
	void *functions[JOB_GRAPH_MAX];
	f64 timestamps[JOB_GRAPH_MAX];
	u32 index;
	f64 start_time;
};

struct JobGraph {
	JobPipe pipes[WORKER_COUNT];
	
	void reset(int pipe_id) {
		f64 time = getWallClockMS();
		pipes[pipe_id].start_time = time;
	}
	
	void pushCall(void *ptr) {
		
		ThreadData *thread_data = (ThreadData *)TlsGetValue(g_tls_index);
		
		// u32 thread_id = GetCurrentThreadId();
		// printf("Thread id %d -- tls id %d\n", thread_id, thread_data->id);
		
		JobPipe *pipe = &pipes[thread_data->id];
		pipe->timestamps[pipe->index] = getWallClockMS() - pipe->start_time;
		pipe->functions[pipe->index] = ptr;
		pipe->index++;
	}
};

global_variable JobGraph *g_job_graph = new JobGraph();


struct Fiber;
void fiberFunc(void *);

struct Fiber {
private:
	u64 *fiber_handle; // NOTE(nathan): its u64 for access convinience
	void *param;
	JobFunction *func;	
	Counter *counter = 0;
	u64 stack_size;
	
	void create() {
		fiber_handle = (u64 *)CreateFiber(stack_size, &fiberFunc, this);
	}
	
public:
	void initialCreate(u64 new_stack_size) {
		this->stack_size = new_stack_size;
		// fiber_handle = (u64 *)CreateFiber(stack_size, &fiberFunc, this);
		create();
	}
	
	void assign(JobFunction *new_function, void *new_param, Counter *new_counter) {
		// create();
		this->param = new_param;
		this->func = new_function;
		this->counter = new_counter;
		// fiber_handle[22] = (u64)function;	
	}
	
	JobFunction *getFunc() { return func; }
	void *getParam() { return param; }
	Counter *getCounter() { return counter; }

	void reset() {
		// if(fiber_handle) DeleteFiber(fiber_handle);
		// create();
	}
	
	void run() {
		// log to debugger
		g_job_graph->pushCall(func);
		// printf("Running on %d\n", thread_data->id);
		SwitchToFiber(fiber_handle);
	}
};

internal_func Fiber *getFiber() {
	Fiber *fiber = (Fiber *)GetFiberData();
	return fiber;
}

void fiberFunc(void *data) {
	while(g_running) {
		Fiber *fiber = (Fiber *)data;
		fiber->getFunc()(fiber, fiber->getParam());
		if(fiber->getCounter() != 0) fiber->getCounter()->sub();
		yield();
	}
}


#define FiberDebug { 	\
}

struct WaitItem {
	Counter *counter;
	Fiber *fiber;	
};

struct WaitList {
	std::vector<WaitItem> items;
	SpinLock spin_lock;
	
	void add(Fiber *fiber, Counter *counter) {
		spin_lock.lock();
		WaitItem item = {counter, fiber};
		items.push_back(item);
		spin_lock.unlock();
	}
	
	Fiber *grabFiber() {
		spin_lock.lock();
		s32 index = -1;
		for(s32 i = 0; i < items.size(); i++) {
			WaitItem &item = items[i];
			if(item.counter->finished()) {
				index = i;
				break;
			}
		}
		
		Fiber *result = 0;
		
		if(index == -1) result = 0;
		else {
			WaitItem &item = items[index];
			result = item.fiber;
			items.erase(items.begin() + index);
		}
		spin_lock.unlock();
		return result;
		
	}
};


#define FIBER_COUNT 160

enum class FiberState {
	Free,
	Taken,	
};

struct FiberPool {
	FiberState masks[FIBER_COUNT];
	Fiber fibers[FIBER_COUNT];
	SpinLock spin_lock;
	
	void init() {
		for(int i = 0; i < FIBER_COUNT; i++) {
			fibers[i].initialCreate(Kilobytes(64));
		}
	}
	
	Fiber *grabFreeFiber() {
		spin_lock.lock();
		s32 index = -1;
		for(s32 i = 0; i < FIBER_COUNT; i++) {
			FiberState fs = masks[i];
			if(fs == FiberState::Free) {
				index = i;
				break;	
			}
		}
		Fiber *result = 0;
		// Assert(index != -1);
		if(index != -1) {
			result = &fibers[index];
			masks[index] = FiberState::Taken;
		}
		spin_lock.unlock();
		return result;
	}
	
	s32 getIndex(Fiber *fiber) {
		u8 *start = (u8 *)&fibers[0];
		u8 *end = (u8 *)fiber;
		u64 diff = end - start;
		u64 count = diff / sizeof(Fiber);
		return count;
	}
	
	void free(Fiber *fiber) {
		spin_lock.lock();
		s32 index = getIndex(fiber);
		masks[index] = FiberState::Free;
		fibers[index].reset();
		spin_lock.unlock();
	}
};

global_variable FiberPool g_fiber_pool;

struct Job {
	JobFunction *func;
	void *param;
	Counter *counter = 0;
};

struct JobQueue {
	SpinLock spin_lock;
	std::queue<Job> queue;
	
	void push(Job job) {
		spin_lock.lock();
		queue.push(job);
		spin_lock.unlock();
	}
	
	bool popAndAssignFiber(Job &job, Fiber **fiber_ptr) {
		bool result = false;
		spin_lock.lock();
		if(!queue.empty()) {
			Fiber *new_fiber = g_fiber_pool.grabFreeFiber();
			if(new_fiber != 0) {
				job = queue.front();
				*fiber_ptr = new_fiber;
				queue.pop();	
				result = true;
			}
		}
		spin_lock.unlock();
		return result;
	}
};
