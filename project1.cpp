#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <queue>
#include <condition_variable>
#include <functional>

using namespace std;

class ThreadPool{
public:
    ThreadPool(int numThreads) :stop(false){
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

    template<class F,class... Args>
    void enqueue(F &&f,Args&&... args){
        function<void()>task = 
            bind(forward<F>(f),forward<Args>(args)...);
            {
                unique_lock<mutex> lock(mtx);
                tasks.emplace(move(task));
            }
            condition.notify_one();
    }


private:
    vector<thread> threads;
    queue<function<void()>> tasks;

    mutex mtx;
    condition_variable condition;

    bool stop;
};
int main() {
    ThreadPool pool(4);

    for(int i = 0;i < 10;i++){
        pool.enqueue([i]{
            cout << "task : " << i << "is runing " << endl;
        this_thread::sleep_for(chrono::seconds(1));
        cout << "task : " << i << "is done" << endl;
        });
    }
    return 0;
}