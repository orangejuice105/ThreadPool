#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <queue>
#include <condition_variable>
#include <functional>
#include <vector>
#include <chrono>
#include <future>

using namespace std;

class ThreadPool{
public:
    ThreadPool(int numThreads) :stop(false){
        if(numThreads <= 0){
            throw invalid_argument("numThreads must be greater than 0");
        }

        for(int i = 0;i < numThreads;i++){
            threads.emplace_back([this]{
                while(1){
                    unique_lock<mutex> lock(mtx);
                    condition.wait(lock,[this]{
                        return !tasks.empty() || stop;
                    });

                    if(stop && tasks.empty()){
                        return;
                    }

                    function<void()> task(move(tasks.front()));
                    tasks.pop();
                    lock.unlock();
                    task();
                }
            });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool(){
        {
            unique_lock<mutex> lock(mtx);
            stop = true;
        }

        condition.notify_all();
        for(auto& t : threads){
            t.join();
        }
    }
    //class F: 声明一个类型模板参数F，class... Args: 可变参数模板，Args是一个类型包，表示可接受任意个类型
    template<class F,class... Args> //template<..>: 表示接下来定义的是一个模板
    auto enqueue(F&& f, Args&&... args)//F&& :转发引用，可同时接受左右值，Args&&同理可接任意数量参数
    {
        using ReturnType =
            std::invoke_result_t<F, Args...>; //invoke_result<>: 推导返回值类型，如int add(int,int)则using ReturnType=int;（即函数前的类型）


        auto task =
            //std::make_shared 创建的是一个 shared_ptr 智能指针，
            //这个指针指向一个动态创建的 packaged_task 对象，让后续代码可以通过这个指针访问同一个 packaged_task
            std::make_shared<
                std::packaged_task<ReturnType()> //packaged_task<>: 保存函数，并保存函数返回值的任务包装器,最后由future获取
            >(
                [
                    //std::forward<T>(x) 的作用是：在模板函数中，把参数按它原来的值类别（左值 / 右值）继续传递下去，保持完美转发
                    func = std::forward<F>(f), //把用户函数保存为func
                    
                    //c++14中初始化不需要声明
                    args_tuple = std::make_tuple( //把参数包展开、decay、装进 tuple 
                        std::forward<Args>(args)...
                    )
                ]() mutable {//mutable: 允许修改捕获的成员

                    return std::apply( //std::apply: 把一个 tuple 拆开，作为参数传给一个可调用对象(func)。
                        func,
                        std::move(args_tuple)
                    ); //相当于返回func(第一个参数, 第二个参数, ..., 第N个参数);结果被packaged_task捕获，调用 future.get() 时拿到

                }
            );


        std::future<ReturnType> result =
            task->get_future();         // packaged_task保存结果后由future得到结果


        {
            std::unique_lock<std::mutex> lock(mtx); // 加锁保护任务队列


            if(stop) // 检查线程池状态，如果线程池已关闭则不能添加任务
            {
                throw std::runtime_error(
                    "ThreadPool has stopped"
                );
            }


            tasks.emplace( // 加入任务队列
                // [task]: 按值捕获,把shared_ptr 拷贝一份到 lambda 内部
                [task]() 
                {
                    (*task)();  /*  task 是 shared_ptr；
                                    *task 解引用，得到 packaged_task<ReturnType()>&；
                                    (*task)() 调用 packaged_task::operator()执行内部lambda */
                }
            );
            
        }


        condition.notify_one(); // 通知线程


        return result; // 返回future
    }


private:
    vector<thread> threads;
    queue<function<void()>> tasks;

    mutex mtx;
    condition_variable condition;

    bool stop;
};


int add(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool(4);

    // 1. 普通任务
    pool.enqueue([] {
        cout << "Hello from thread pool" << endl;
    });


    // 2. 测试 int 返回值
    auto result1 = pool.enqueue(add, 10, 20);

    cout << "add result = "
         << result1.get()
         << endl;


    // 3. 测试 string 返回值
    auto result2 = pool.enqueue([] {
        return string("Hello ThreadPool");
    });

    cout << "string result = "
         << result2.get()
         << endl;


    // 4. 测试 move-only 参数
    auto result3 = pool.enqueue(
        [](unique_ptr<int> p) {
            return *p * 2;
        },
        make_unique<int>(21)
    );

    cout << "move-only result = "
         << result3.get()
         << endl;


    // 5. 测试异常传递
    auto result4 = pool.enqueue([]() -> int {
        throw runtime_error("task error");
    });

    try
    {
        cout << result4.get() << endl;
    }
    catch(const exception& e)
    {
        cout << "caught exception: "
             << e.what()
             << endl;
    }


    // 6. 测试多个任务
    vector<future<int>> results;

    for(int i = 0; i < 10; i++)
    {
        results.push_back(
            pool.enqueue([i] {
                this_thread::sleep_for(
                    chrono::milliseconds(100)
                );

                return i * i;
            })
        );
    }

    for(auto& f : results)
    {
        cout << "square = "
             << f.get()
             << endl;
    }

    return 0;
}