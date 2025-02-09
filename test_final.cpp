#include"threadpool_finall.h"
/*
1. 如何可以让我们的线程池使用起来更加方便
pool.submintTask(sum,10,20);
    submitTask:可变参模板编程
2.我们自己造了一个Result代码挺多,可以使用c++11的packaged_task(function函数对象) async   
    使用future来代替Result来节省线程池代码
*/
int sum(int a, int b)
{
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return a+b;
}   
int main()
{
    
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);
    std::future<int> r1 =pool.submitTask(sum,10,20);
    std::future<int> r2 =pool.submitTask(sum,10,20);


    std::future<int> r3 =pool.submitTask(sum,10,20);
    std::future<int> r4 =pool.submitTask(sum,10,20);
    std::future<int> r5 =pool.submitTask(sum,10,20);
    
    std::future<int> r6 =pool.submitTask(sum,10,20);
    std::future<int> r8 =pool.submitTask(sum,10,20);
    std::future<int> r9=pool.submitTask(sum,10,20);
    std::cout<<r1.get()<<std::endl;
    std::cout<<r2.get()<<std::endl;
    std::cout<<r3.get()<<std::endl;
    std::cout<<r4.get()<<std::endl;
    std::cout<<r5.get()<<std::endl;
    std::cout<<r6.get()<<std::endl;
    return 0;
}