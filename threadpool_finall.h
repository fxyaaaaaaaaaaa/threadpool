#ifndef THREADPOOL_FINALL
#define THREADPOOL_FINALL
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
#include<future>
#define TASK_MAX_SIZE 2
#define THREAD_MAX_SIZE 5
#define THREAD_MAX_FREE_TIME 20 // 单位:秒
enum class PoolMode
{
    MODE_FIXED,     // 线程数量固定
    MODE_CACHED,    // 线程数量可以增长
};

class Thread
{
public:
    //线程所执行的线程函数对象
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func)
        :func_(func)
        ,threadId_(generateId_++)
        {};
    ~Thread() = default;

    //启动线程
    void start()
    {
        // 创建一个线程并执行线程函数
        std::thread t(func_,threadId_);
        // 这里一定要做detach操作,否则出了函数作用域thread t 对象析构,正在执行线程函数func的线程会出现core dump错误
        t.detach();
    };

    //获取线程id
    int getThreadId(){return threadId_;};
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; 
};
int Thread::generateId_ = 0;
class ThreadPool
{
public:
    ThreadPool()
    : initThreadSize_(0)
    , isRuning_(false)
    , maxThreadSisze_(THREAD_MAX_SIZE)
    , taskSize_(0)
    , curThreadSize_(0)
    , freeThreadSize_(0)
    , taskQueMaxSize_(TASK_MAX_SIZE)
    , PoolMode_(PoolMode::MODE_FIXED) 
    {};
    ~ThreadPool()
    {
        isRuning_ = false;
        std::unique_lock<std::mutex>lock(taskQueMtx_);
        notEmpty_.notify_all();
        recycle_.wait(lock,[&]()->bool{return threads_.size() == 0;});
    };
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    void setMode(PoolMode mode)
    {
        if (poolState())
            return;
        PoolMode_ = mode;
    }; 
    //设置线程池的工作模式
    //处理器核心数作为线程数量
    void start(int size = std::thread::hardware_concurrency())
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
            int thtreadId = ptr->getThreadId();
            // unique_ptr 删除了拷贝构造函数 只保留了移动构造函数 因此需要使用move做资源转移

            threads_.emplace(thtreadId,std::move(ptr));
        }
        // 启动线程
        for (int i = 0; i < initThreadSize_; i++)
        {
            std::cout<<"start:"<<i<<std::endl;
            threads_[i]->start();
            curThreadSize_++;
        }
    } //开启线程池
    //设置任务队列任务上限值
    void settaskQueMaxSize_(int taskQueMaxSize_)
    {
        if (poolState())
            return;
        taskQueMaxSize_ = taskQueMaxSize_;
    };
    //给线程池提交任务
    //使用可变参模板编程,让submitTask可以接收任意任务函数和任意数量的参数
    //pool.submitTask(sum1,10,20);
    //返回值 future<x> 使用decltype来推导类型
    template<typename Func,typename... Args>
    auto submitTask(Func&& func,Args&&... args) -> std::future<decltype(func(args...))>
    {
        using RType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func),std::forward<Args>(args)...));
        std::future<RType> result = task->get_future();

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

            auto task = std::make_shared<std::packaged_task<RType()>>(
                []()->RType{ return RType(); }
            );
            (*task)();
            return task->get_future();
        };

        // 放入任务

        //taskQue_.push(sp);
        //十分经典的处理！！！因为我们在定义taskQue时并不知道任务函数对象的返回值会是什么所以我们使用了void中间层
        //在实际传递的时候通过lambda表达式来传递真正要执行的函数对象
        taskQue_.emplace([task](){ (*task)();  });
        
        taskSize_++;

        // 因为新放了任务，所以任务队列肯定不满 ,因此可以通过notEmpty通知，进行分配执行任务
        notEmpty_.notify_all();

        // 如果线程池的模式为cache(该模式适用于解决任务数量多且快的状态) && 任务数量多余线程池的线程数量 && 线程池中线程数量未达到上限
        if (PoolMode_ == PoolMode::MODE_CACHED && taskSize_ > freeThreadSize_ && curThreadSize_ < maxThreadSisze_)
        {
            std::cout << ">>> create new thread..." << std::endl;
            // c++11 提供make_shared c++14 提供make_unique
            auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
            int threadId= ptr->getThreadId();
            threads_.emplace(threadId,std::move(ptr));
            threads_[threadId]->start();
            curThreadSize_++;
            freeThreadSize_++;
        }
        return result;
    };
    void setMaxThreadSisze_(size_t count);
    
private:
    void threadFunc(int threadid)
    {
        auto lastTime = std::chrono::high_resolution_clock().now();
        for (;;)
        {
            // 先获取锁
            Task task;
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
               task();//执行function<void()>
            freeThreadSize_++;
            lastTime = std::chrono::high_resolution_clock().now();
        }    
        std::this_thread::sleep_for(std::chrono::seconds(2));
    };
    bool poolState()
    {
         return isRuning_;
    };
    //std::vector<std::unique_ptr<Thread>> threads_; 线程数组
    std::unordered_map<int,std::unique_ptr<Thread>>threads_;
    size_t initThreadSize_; //初始的线程数量
    size_t maxThreadSisze_; //线程池线程数量上限
    std::atomic_int curThreadSize_;//记录当前线程池中线程总数量
    std::atomic_int freeThreadSize_;//记录空闲线程数量

    using Task = std::function<void()>;
    std::queue<Task> taskQue_; //任务队列
    
    std::atomic_int8_t taskSize_;   // 任务的数量
    int taskQueMaxSize_; //任务队列最大上限
    std::mutex taskQueMtx_;
    std::condition_variable notFull_;   //表示任务队列不满
    std::condition_variable notEmpty_;  //表示任务队列不空  
    std::condition_variable recycle_;   //线程池析构回收子线程资源

    PoolMode   PoolMode_;   //当前线程池的工作模式
    std::atomic_bool isRuning_;
};


#endif //THREADPOOL_FINALL