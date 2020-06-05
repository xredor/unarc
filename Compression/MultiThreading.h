#include "LZMA/Windows/Thread.h"
#include "LZMA/Windows/Synchronization.h"

using namespace NWindows;
using namespace NWindows::NSynchronization;

// Shorter names
typedef CCriticalSection     Mutex;
typedef CCriticalSectionLock Lock;
typedef CAutoResetEvent      Event;
typedef CManualResetEvent    ManualEvent;
typedef CSemaphore           Semaphore;
#define Signal Set
//#define Wait   Lock

// *************************************************************************************************
// *** Queue with thread-safe Put/Get operations ***************************************************
// *************************************************************************************************
template <class T>
class SyncQueue
{
    Mutex mutex;               // Protects operations on queue
    ManualEvent ObjectAdded;   // Signals that object was added to queue
    T  *a;                     // Array of objects
    int size;                  // Size of array
    int head, tail;            // Indices of queue head and tail

    // Number of objects currently in queue
    int ObjectsInQueue()  {return (tail-head+size) % size;};

public:
    SyncQueue(): a(NULL) {};
    void Close() { FreeAndNil(a); }
    ~SyncQueue() { Close(); }

    // Set queue size - the class relies on assumption that client code
    // will never try to put excessive elements to the queue
    void SetSize (int max_elements)
    {
        Close();
        head = tail = 0;
        size = max_elements+1;   // +1 simplifies distinguishing between full and empty queues
        a = (T*) malloc(size * sizeof(T));
    }

    // Add object to the queue
    void Put (T o)
    {
        Lock _(mutex);
        if (ObjectsInQueue() == 0)
            ObjectAdded.Signal();
        a[tail] = o;
        tail = (tail+1) % size;
    }

    // Remove object from the queue
    T Get()
    {
        for(;;)
        {
            {
                Lock _(mutex);
                if (ObjectsInQueue() > 0)
                {
                    int tmp = head;
                    head = (head+1) % size;
                    if (ObjectsInQueue() == 0)
                        ObjectAdded.Reset();
                    return a[tmp];
                }
            }
            // Sleep until new object will be added to the queue
            ObjectAdded.Lock();
        }
    }
};


// *************************************************************************************************
// *** Multithreading block-wise compressor or decompressor ****************************************
// *************************************************************************************************

// Basic task structure
struct BaseTask
{
    CALLBACK_FUNC*   callback;        // I/O callback
    void*            auxdata;         // and its additional parameter
    volatile int     errcode;         // Error code to return or 0
    int              NumThreads;      // Total amount of threads, including extra threads for I/O buffering
    Semaphore        LimitThreads;    // Limits number of threads doing compression
    Event            Finished;        // Signals that all compressed data was written

    // Update operation errcode
    virtual int SetErrCode (int e)  {if (e<0 && errcode==0)  errcode = e;  return e;}

    virtual ~BaseTask() {};
};

// One worker thread
struct WorkerThread
{
    BaseTask *task;                   // Task to which this job belongs
    Event  StartOperation;            // Signals that data are ready to be processed
    Event  OperationFinished;         // Signals that data were processed and need to be flushed
    Event  Finished;                  // Signals that all compressed data was written
    char*  InBuf;                     // Buffer pointing to input (original) data
    char*  OutBuf;                    // Buffer pointing to output (processed) data
    volatile int InSize;              // Amount of data in inbuf
    volatile int OutSize;             // Amount of data in outbuf

    virtual int  init()        {return 0;}    // Initialize job
    virtual int  process()     {return 0;}    // Perform one operation
    virtual int  after_write() {return 0;}    // Cleanup after processed data was written
    virtual int  done()        {return 0;}    // Final cleanup
    virtual void run()                        // Thread function performing process() on each input block
    {
        for(;;)
        {
            StartOperation.Lock();                    // wait for data to process
            ////int InSize = job->InSize;  - the same insize should be inside process()
            if (InSize <= 0)  break;                  // signal to finish thread execution and release resources
            task->LimitThreads.Lock();                // wait for compression slot
            task->SetErrCode (OutSize = process());   // process data and save errcode/outsize
            task->LimitThreads.Release();             // release compression slot
            OperationFinished.Signal();               // signal to Writer thread
        }
        done();
        Finished.Signal();
    }
    virtual ~WorkerThread() {};
};

template <class Job>
struct MTCompressor : BaseTask
{
    Job*             jobs;            // Full list of jobs created
    SyncQueue<Job*>  WriterJobs;      // Queue of jobs for Writer thread, ordered by input chunks
    SyncQueue<Job*>  FreeJobs;        // Queue of free jobs

    virtual int  run();               // Main function performing m/t work
    virtual int  main_cycle() = 0;    // Main user cycle
    virtual void WriterThread();      // Thread that saves compressed data to disk
    virtual void WaitJobsFinished();  // Wait until all data are written and compression resources are freed
    virtual void CreateJobs();        // Create all required threads
    virtual ~MTCompressor() { WriterJobs.Close(); FreeJobs.Close(); };
};

static DWORD WINAPI RunWorkerThread (void *param)  {((WorkerThread*)               param) -> run();          return 0;}
static DWORD WINAPI RunWriterThread (void *param)  {((MTCompressor<WorkerThread>*) param) -> WriterThread(); return 0;}

template <class Job>
int MTCompressor<Job>::run()
{
    errcode = 0;
    CreateJobs();
    if (errcode == 0)
    {
        CThread t;  t.Create(RunWriterThread, this);
        // Perform (de)compression cycle
        SetErrCode(main_cycle());
        // Wait for Writer thread to finish
        WriterJobs.Put(NULL);
        Finished.Lock();
    }
    WaitJobsFinished();
    LimitThreads.Close();
    delete [] jobs;
    return errcode;  // return error code or 0
}

template <class Job>
void MTCompressor<Job>::WriterThread()
{
    for(;;)
    {
        // Acquire next writer job
        Job *job = WriterJobs.Get();
        // Break on EOF/error
        if (job==NULL  ||  job->InSize <= 0)
            break;
        // Wait until (de)compression will be finished
        job->OperationFinished.Lock();
        // Записать сжатый блок и выйти, если при записи произошла ошибка/больше данных не нужно
        if (SetErrCode(callback("write", job->OutBuf, job->OutSize, auxdata)) < 0)
            break;
        // After-write cleanup
        if (SetErrCode(job->after_write()) < 0)
            break;
        // Make thread available for next compression job
        FreeJobs.Put(job);
    }
    Finished.Signal();
}

template <class Job>
void MTCompressor<Job>::WaitJobsFinished()
{
    for (int i=0; i < NumThreads; i++)
    {
        jobs[i].InSize = 0;
        jobs[i].StartOperation.Signal();
        jobs[i].Finished.Lock();
    }
}

template <class Job>
void MTCompressor<Job>::CreateJobs ()
{
    int CompressionThreads = GetCompressionThreads();
    // Semaphore limiting amount of threads doing (de)compression
    LimitThreads.Create (CompressionThreads, CompressionThreads);

    // Total amount of threads, including extra threads for I/O buffering
    NumThreads = CompressionThreads + CompressionThreads/2 + 1;
    WriterJobs.SetSize(NumThreads+1);      // +1 for NULL job at EOF
    FreeJobs.  SetSize(NumThreads+1);
    jobs = new Job[NumThreads];

    int threads = 0;
    for (int i=0; i < NumThreads; i++)
    {
        Job *job = &jobs[i];
        job->task = this;
        if (job->init() >= 0)
        {
            threads++;
            CThread t;  t.Create (RunWorkerThread, job);
            FreeJobs.Put(job);
        }
        else
        {   // Free memory because thread (that will free memory before exit) was not created
            SetErrCode (job->done());
        }
    }

    if (threads==0)  SetErrCode (FREEARC_ERRCODE_NOT_ENOUGH_MEMORY);   ////
}

