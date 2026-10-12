#include "thread_pool.h"

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std;


int add(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool(4);
    int x = 1;
    auto referenceResult = pool.enqueue([] (int& n){
            return ++n;
        },
        std::ref(x)
    );
    cout << referenceResult.get() << endl;

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