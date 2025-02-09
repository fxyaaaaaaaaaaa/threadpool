#include"threadpool.h"
#include<chrono>
#include<thread>
class MyTask:public Task
{
public:
    MyTask(int begin_,int end_)
        :begin_(begin_)
        ,end_(end_)
        {}
    Any run()
    {
        std::cout<<"tid:"<<std::this_thread::get_id()<<"begin!"<<std::endl;

        int sum=0;
        for(int i=begin_;i<=end_;i++)
        {
            sum = sum + i;
        }
        std::cout<<"tid:"<<std::this_thread::get_id()<<"end!"<<std::endl;
        return sum;
    }

private:
    int begin_;
    int end_;
};
int main()
{
    ThreadPool pool;
    pool.start(3);

    std::shared_ptr<MyTask> t1(new MyTask(1,10));
    std::shared_ptr<MyTask> t2(new MyTask(11,20));
    std::shared_ptr<MyTask> t3(new MyTask(21,30));

    Result r1=pool.submitTask(t1);
    Result r2=pool.submitTask(t2);
    Result r3=pool.submitTask(t3);

    std::cout<<"sum = "<<(r1.get().cast_<int>() + r2.get().cast_<int>() + r3.get().cast_<int>())<<std::endl;
    getchar();
    return 0;
}
