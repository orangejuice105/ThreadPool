# ThreadPool

一个使用 C++17 实现的固定线程数线程池学习项目。调用者提交任务，工作线程
从共享队列取出并执行，通过 `std::future` 获取返回值或任务异常。

## 编译与运行

需要支持 C++17 的编译器和 CMake 3.13 或更新版本。当前已在 Linux / WSL
环境验证构建，不依赖第三方库。

在 `project1` 目录执行：

```bash
cmake -S . -B build
cmake --build build
./build/main
```

修改源码后，通常只需要重新执行 `cmake --build build`。CMake 将线程池编译
成 `thread_pool` 库，示例程序 `main` 链接该库，并获取头文件路径和线程依赖。
`build/` 保存生成的构建文件和可执行文件，已加入 Git 忽略规则。

## 项目结构

```text
project1/
├── CMakeLists.txt          C++17、线程依赖及编译目标
├── include/
│   └── thread_pool.h       类声明、成员变量、enqueue 模板实现
├── src/
│   ├── thread_pool.cpp     构造、工作线程循环和析构
│   └── main.cpp            使用示例
└── README.md
```

`enqueue()` 是函数模板，调用者可能传入不同类型的函数、lambda 和参数。
编译器生成对应实现时需要看到模板定义，因此它保留在头文件中；构造函数和
析构函数的定义放在 `.cpp` 中。

## 基本使用

```cpp
#include "thread_pool.h"
#include <iostream>

int main()
{
    ThreadPool pool(4);

    auto result = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);

    std::cout << result.get() << '\n'; // 30
}
```

线程数必须大于 0，否则构造函数抛出 `std::invalid_argument`。线程池不能
复制，也不支持移动；创建后线程数量保持不变。

`enqueue()` 提交任务并返回 `std::future<返回类型>`，不等待任务执行完成。
`future.get()` 等待结果就绪并取出结果，同一个 future 的 `get()` 只能调用
一次。无返回值的任务对应 `std::future<void>`，也可以用 `get()` 等待并检查异常。

## 参数保存与引用

任务保存函数及参数：普通左值一般被复制，右值可以被移动。执行时，保存的
普通参数作为右值传给函数，因此支持 `std::unique_ptr` 等只能移动的参数：

```cpp
auto result = pool.enqueue([](std::unique_ptr<int> p) {
    return *p * 2;
}, std::make_unique<int>(21));

std::cout << result.get() << '\n'; // 42
```

需要引用原变量时显式使用 `std::ref()`，只读引用可以使用 `std::cref()`：

```cpp
int value = 1;
auto result = pool.enqueue([](int& n) {
    return ++n;
}, std::ref(value));

result.get(); // 等待任务完成后再读取 value
std::cout << value << '\n'; // 2
```

直接传 `value` 会让 `std::make_tuple()` 保存整数副本。执行时这个副本作为
右值传入，不能绑定到上例要求的 `int&`，因此该调用需要 `std::ref(value)`。

引用、裸指针和 lambda 的引用捕获不会延长原对象的生命周期。对象必须活到
任务结束；多个任务或外部线程同时访问共享对象时，调用者需要自行同步。

保存的函数对象以左值形式调用，所以只有 `operator()() &&` 的函数对象不受
支持。任务返回引用时会保留引用类型，例如得到 `std::future<int&>`；返回的
引用所指向的对象也必须有效，不能返回局部变量或任务内部短期对象的引用。

## 异常传递

```cpp
auto result = pool.enqueue([]() -> int {
    throw std::runtime_error("task error");
});

try {
    result.get();
} catch (const std::exception& e) {
    std::cout << e.what() << '\n';
}
```

`std::packaged_task` 保存用户任务的异常，随后由 `future.get()` 重新抛出，
工作线程可以继续处理后面的任务。如果丢弃 future，任务仍会执行，但任务
异常不会自动打印。构造、分配内存或提交任务失败时，异常则直接从相应调用抛出。

## 工作流程

1. 构造函数检查线程数，预留线程容器容量，再启动工作线程。
2. `enqueue()` 将函数和参数保存到 `boundTask`，根据实际任务推导返回类型，
   然后创建 `packaged_task` 并取得 future。
3. 提交者在互斥锁内检查停止标志并入队，释放锁后唤醒一个工作线程。
4. 工作线程通过带条件判断的 `condition_variable::wait()` 等待任务或停止请求。
   等待期间释放锁，结束等待前重新取得锁。
5. 工作线程在锁内取出任务、移除队首，释放锁后执行任务，避免任务执行期间
   阻塞其他线程提交或取出任务。
6. 析构函数设置停止标志、唤醒所有线程，再逐个 `join()`。工作线程只有在
   停止标志为真且队列为空时才退出，所以已接受的任务会继续执行。

任务按进入队列的顺序取出，但多个工作线程并行执行，开始执行和完成的顺序
不保证一致。多个线程向 `std::cout` 连续插入内容时，输出片段也可能交错。

`stop` 和任务队列都受同一把互斥锁保护，所以停止标志使用普通 `bool` 即可。

## 构造失败与正常退出

线程创建中途失败时，构造函数捕获异常，设置停止标志、唤醒并等待已创建的
线程退出，随后重新抛出原异常。这样可以避免销毁仍处于 `joinable()` 状态的
`std::thread` 导致程序终止。`reserve()` 只预留容器空间，不会创建系统线程，
也不能保证后续线程创建成功。

正常销毁线程池前，所有外部提交线程必须结束，不能一边销毁对象一边调用
`enqueue()`；停止检查不能替代对象生命周期管理。线程池不能在自己的工作
线程中被销毁，否则会尝试 `join()` 当前线程。

析构会等待任务完成。如果任务永远不返回，析构也会一直等待。

## 当前限制与示例验证

- 任务队列没有容量上限，提交速度长期超过处理速度会增加内存占用。
- 没有公开的停止、取消、超时等待全部任务或动态调整线程数接口。
- 工作线程如果提交新任务到同一线程池并调用 `get()` 等待，可能耗尽可用
  工作线程而发生死锁，单工作线程时尤其容易出现。

`src/main.cpp` 演示了引用参数、无返回值任务、整数和字符串返回值、移动专属
参数、任务异常，以及多个任务的结果获取。当前示例已通过编译和运行验证；
项目尚未提供独立的自动测试套件，示例运行不代表所有并发边界都已经验证。
