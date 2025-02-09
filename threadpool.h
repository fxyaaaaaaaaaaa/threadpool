#ifndef THREADPOOL
#define THREADPOOL
#include<functional>
#include<vector>
#include<iostream>
#include<queue>
#include<memory>
#include<atomic>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<unordered_map>
enum class PoolMode
{
    MODE_FIXED,     // 线程数量固定
    MODE_CACHED,    // 线程数量可以增长
};
using UINT = unsigned int;
//实现一个Any类型 可以接收任何数据类型
class Any
{
public:
    Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;
    
    template<typename T>
    Any(T data):ptr(std::make_unique<Derive<T>>(data)){}

    template<typename T>
    T cast_()
    {
        Derive<T>* dp=dynamic_cast<Derive<T> * >(ptr.get());
        if(dp == nullptr)
        {   
            throw "type is unsafe!";
        }
        return dp->data_;
    }
private:
    class Base
    {
        public:
        virtual ~Base() = default;
        private:
    };
    template<typename T>
    class Derive : public Base
    {
        public:
        T data_;
    };
    std::unique_ptr<Base> ptr;
};

//实现一个信号量类
class Semaphore
{
public:
    Semaphore(int limit = 0)
            :resLimit_(limit)
            ,isExit_(false)
            {}

    ~Semaphore(){isExit_ = true;};
    
    //获取一个信号量资源
    void wait()
    {
        if(isExit_)
            return ;
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock,[&]()->bool{return resLimit_ > 0 ;});
        resLimit_--;
    }
    //增加一个信号量资源
    void post()
    {
        if(isExit_)
            return ;
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_ ++ ;
        //linux下的析构函数什么也没做 导致这里的状态失效,无故阻塞
        cond_.notify_all();
    }
private:
    std::atomic_bool isExit_;
    std::mutex mtx_;
    std::condition_variable cond_;
    int resLimit_;
};
class Task;
class Result
{
public:
    Result(std::shared_ptr<Task>task,bool isVaild = true);
    ~Result() = default;
    //当子线程执行完task之后将task的返回结果设置到Result中!!!
    void setAny(Any any);
    Any get();
private:
    Semaphore sem_;
    std::shared_ptr<Task> task_;
    Any any_;
    std::atomic_bool isVaild;
};

//任务函数
class Task
{
public:
    Task()
        :result_(nullptr)
        {}
    void exec();
    //用户可以自定义任务
    virtual Any run()=0;
    void setResult(Result*r){result_ =r; }
private:
    Result * result_;
};
class Thread
{
public:
    //线程所执行的线程函数对象
    using ThreadFunc = std::function<void(UINT)>;
    Thread(ThreadFunc func);
    ~Thread(){};

    //启动线程
    void start();

    //获取线程id
    UINT getThreadId();
private:
    ThreadFunc func_;
    static UINT generateId_;
    UINT threadId_; 
};
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    void setMode(PoolMode mode); //设置线程池的工作模式
    //处理器核心数作为线程数量
    void start(int8_t size = std::thread::hardware_concurrency()); //开启线程池
    //设置任务队列任务上限值
    void settaskQueMaxSize_(int8_t taskQueMaxSize_);
    //给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);
    void setMaxThreadSisze_(size_t count);
    
private:
    void threadFunc(int threadid);
    bool poolState();
    //std::vector<std::unique_ptr<Thread>> threads_; 线程数组
    std::unordered_map<UINT,std::unique_ptr<Thread>>threads_;
    size_t initThreadSize_; //初始的线程数量
    size_t maxThreadSisze_; //线程池线程数量上限
    std::atomic_int curThreadSize_;//记录当前线程池中线程总数量
    std::atomic_int freeThreadSize_;//记录空闲线程数量 
    std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
    std::atomic_int8_t taskSize_;   // 任务的数量
    int8_t taskQueMaxSize_; //任务队列最大上限
    
    std::mutex taskQueMtx_;
    std::condition_variable notFull_;   //表示任务队列不满
    std::condition_variable notEmpty_;  //表示任务队列不空  
    std::condition_variable recycle_;   //线程池析构回收子线程资源

    PoolMode   PoolMode_;   //当前线程池的工作模式
    std::atomic_bool isRuning_;
};


#endif //THREADPOOL