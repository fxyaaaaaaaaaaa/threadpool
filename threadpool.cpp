#include "threadpool.h"
#define TASK_MAX_SIZE 1024
#define THREAD_MAX_SIZE 10
#define THREAD_MAX_FREE_TIME 60 // 单位:秒
ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , isRuning_(false)
    , maxThreadSisze_(THREAD_MAX_SIZE)
    , taskSize_(0)
    , curThreadSize_(0)
    , freeThreadSize_(0)
    , taskQueMaxSize_(TASK_MAX_SIZE)
    , PoolMode_(PoolMode::MODE_FIXED) 
    {};
ThreadPool::~ThreadPool() 
{
    isRuning_ = false;
    std::unique_lock<std::mutex>lock(taskQueMtx_);
    notEmpty_.notify_all();
    recycle_.wait(lock,[&](){return threads_.size() == 0;});
};
void ThreadPool::setMode(PoolMode mode)
{
    if (poolState())
        return;
    PoolMode_ = mode;
}; // 设置线程池的工作模式
void ThreadPool::start(int8_t size)
{
    // 设置线程的运行状态
    isRuning_ = true;

    // 记录初始化线程个数
    initThreadSize_ = size;
    // 空闲线程个数
    freeThreadSize_ = size;
    // 创建线程
    for (int i = 0; i < initThreadSize_; i++)
    {
        // c++11 提供make_shared c++14 提供make_unique
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
        UINT thtreadId = ptr->getThreadId();

        // unique_ptr 删除了拷贝构造函数 只保留了移动构造函数 因此需要使用move做资源转移
        threads_.emplace(thtreadId,std::move(ptr));
    }
    // 启动线程
    for (int i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start();
        curThreadSize_++;
    }
};
// 设置任务队列任务上限值
void ThreadPool::settaskQueMaxSize_(int8_t taskQueMaxSize_)
{
    if (poolState())
        return;
    taskQueMaxSize_ = taskQueMaxSize_;
};
// 给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // wait 是条件不满足就一直等待
    // wait_for是条件满足返回true向下执行，条件不满足但是到达定时时间之后会返回false
    // wait_until
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                           { return taskQue_.size() < (size_t)taskQueMaxSize_; }))
    {
        // 表示notFull_等待1s仍然没有满足 tashQue_.size() < tashQueMaxSize_ 的条件
        std::cerr << "tash queue is full, submit tash fail." << std::endl;
        return Result(std::move(sp), false);
    };

    // 放入任务

    taskQue_.push(sp);
    taskSize_++;

    // 因为新放了任务，所以任务队列肯定不满 ,因此可以通过notEmpty通知，进行分配执行任务
    notEmpty_.notify_all();

    // 如果线程池的模式为cache(该模式适用于解决任务数量多且快的状态) && 任务数量多余线程池的线程数量 && 线程池中线程数量未达到上限
    if (PoolMode_ == PoolMode::MODE_CACHED && taskSize_ > curThreadSize_ && curThreadSize_ < maxThreadSisze_)
    {
        // c++11 提供make_shared c++14 提供make_unique
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
        UINT threadId= ptr->getThreadId();
        threads_.emplace(threadId,std::move(ptr));
        ptr->start();
        curThreadSize_++;
        freeThreadSize_++;
    }
    return Result(sp);
};
void ThreadPool::threadFunc(int threadid)
{
    auto lastTime = std::chrono::high_resolution_clock().now();
    for (;;)
    {
        // 先获取锁
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            // cached模式下，有可能创建了很多线程，但空闲时间超过60s,应该把超过initThteadSize数量的线程进行回收掉
           
           while(taskQue_.size() ==0)
           {
                if(!isRuning_)
                {
                    threads_.erase(threadid);
                    recycle_.notify_all();
                    return ;
                }
                if (curThreadSize_ > initThreadSize_)
                {
                    
                        // 每一秒返回一次 如何区分是超时返回？还是有任务执行返回？
                    if (std::cv_status::timeout ==
                        notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() > THREAD_MAX_FREE_TIME)
                        {
                            // 回收线程--记录线程数量相关的变量修改 && 把线程对象从线程列表中删除
                            curThreadSize_--;
                            freeThreadSize_--;
                            threads_.erase(threadid);
                            return;
                        }
                    }
                }
                else
                {
                    // 等待notEmpty条件
                    notEmpty_.wait(lock);
                }
           }
           

            freeThreadSize_--;
            // 从任务队列中取出一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            //  如果依然有剩余任务,继续通知其他线程执行任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }
            //  取出一个任务进行通知
            notFull_.notify_all();
        }
        // 释放锁
        // 当前线程负责执行任务
        if (task != nullptr)
            task->exec();
        freeThreadSize_++;

        lastTime = std::chrono::high_resolution_clock().now();
    }
        
        
};
bool ThreadPool::poolState()
{
    return isRuning_;
}
void ThreadPool::setMaxThreadSisze_(size_t count)
{
    if (poolState())
        return;
    if (PoolMode_ == PoolMode::MODE_CACHED)
    {
        maxThreadSisze_ = count;
    }
}
/////////////////      Task        ////////////////////////////
void Task::exec()
{
    if (result_)
        result_->setAny(run());
}
///////////////////     Thread   ///////////////////////////////

void Thread::start()
{
    // 创建一个线程并执行线程函数
    std::thread t(func_,threadId_);
    // 这里一定要做detach操作,否则出了函数作用域thread t 对象析构,正在执行线程函数func的线程会出现core dump错误
    t.detach();
};
Thread::Thread(ThreadFunc func)
        :func_(func_)
        ,threadId_(generateId_++)
{};
UINT Thread::generateId_ = 0;
UINT Thread::getThreadId()
{
    return threadId_;
}
//////////////////////  Result  //////////////////////////////

Result::Result(std::shared_ptr<Task> task, bool isVaild = true)
    : task_(task), isVaild(isVaild)
{
    task_->setResult(this);
};
Any Result::get()
{
    if (!isVaild)
    {
        return "";
    }
    sem_.wait();

    return std::move(any_);
};
void Result::setAny(Any any)
{
    // 获取task的返回值
    this->any_ = std::move(any);

    sem_.post();
};
