# 更新文档 - v1.0.1

---

## 发现上一个版本的问题

1. 鲁棒性不高，所有错误均抛出给主线程处理，导致所有的错误不论是小错误还是大错误都会使程序退出
2. skill逻辑问题，init()函数会在程序开始时统一调用，每一次停止不是卸载dll而是停止调用run()，不符合常理，应该skill链调用的时候统一init()然后进行run()
3. memory管理问题，插入注册函数会导致删除，每一次调用可能导致出现获取函数夹在删除和插入之中
4. Git库版本管理没有处理

---

## 当前已经处理

- [x] memory管理问题

- [x] skills链管理器

- [x] 异常抛出问题

- [x] Git库版本管理问题

---

## 处理问题方案

---

### memory处理

```cpp
template<typename T>
void Memory::insertOrAssign(const std::string& name, const T& value)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    impl_->content.insert_or_assign(name, std::any(value));
}
```

修改`insertOrAssign`函数处理逻辑，由于impl_.content是std::map处理，直接使用std::map.insert_or_assign进行覆盖处理

---

## skill链管理器更改方案

### 分析原因

由于v1.0.0代码中dll管理问题，对于opencv库需要绑定线程的用法，动态卸载dll和加载dll会出现问题，新的代码把dll管理和线程管理分开，在一开始就加载好dll动态库，然后在后续不加入新的dll，如果skill的dll有问题，需要重启程序，然后再每一次开始的时候运行init(),每一轮运行run(),以及结束后运行end()用来释放dll逻辑内存。

### 更改方案

重写runskillslist程序，新写runskills.hpp/cpp用来代替runskillslists.hpp/cpp程序，新的逻辑图如下所示

### skill管理图

```mermaid
flowchart TD
    A[网页/调度器 请求] --> B{start 还是 stop}

    B -->|start name| S0[Runskills::start]
    S0 --> S1["查链是否存在<br/>lock(mtx_chain)"]
    S1 --> S2{已存在?}
    S2 -->|是| S3[取已有 chainptr]
    S2 -->|否| S4["try_emplace 建新链<br/>lock(mtx_chain)"]
    S4 --> S3

    S3 --> S5{"查 state<br/>lock(mtx_state)"}
    S5 -->|state==true| S6[拒绝: 正在运行<br/>返回 false]
    S5 -->|state==false| S7[runInit 逐个技能 load+init]

    S7 --> S8{init 全部成功?}
    S8 -->|否| S9[写 chain.error<br/>返回 false]
    S8 -->|是| S10["置 run=true<br/>lock(mtx_run)"]

    S10 --> S11["启动线程<br/>thread = std::thread(runthread)"]
    S11 --> S12[返回 true]

    S11 --> T0[runthread 开始]
    T0 --> T1["置 state=true<br/>lock(mtx_state)"]
    T1 --> T2{mode == once?}

    T2 -->|once| O1[runOnce 跑一遍]
    O1 --> O2["置 run=false<br/>lock(mtx_run)"]
    O2 --> T9[runEnd 逐个技能 end]

    T2 -->|while| L1["sleepUntil(tick)<br/>每 50ms 查一次 run"]
    L1 --> L2{"run==false 或 STATUS==false?"}
    L2 -->|是| T9
    L2 -->|否| L3[roundStart = now]

    L3 --> L4[runOnce 跑一轮]
    L4 --> L5{"runOnce 抛异常?"}
    L5 -->|是| L6[写 chain.error<br/>返回]
    L5 -->|否| L7[算 runOnceTime<br/>写 chain.time lock mtx_time]

    L7 --> L8{runOnceTime < interval?}
    L8 -->|是| L9[tick = roundStart + period]
    L8 -->|否| L10[tick = now]
    L9 --> L1
    L10 --> L1

    T9 --> T10["置 state=false<br/>lock(mtx_state)"]
    T10 --> T11[线程结束 join 可返回]

    B -->|stop name| P0[Runskills::stop]
    P0 --> P1["查链存在?<br/>lock(mtx_chain)"]
    P1 -->|不存在| P2[返回 false]
    P1 -->|存在| P3{"查 state<br/>lock(mtx_state)"}
    P3 -->|state==false| P4[已停止 返回 false]
    P3 -->|state==true| P5["置 run=false<br/>lock(mtx_run)"]
    P5 --> P6[返回 true 发出停止信号]

    P6 -.->|等待| T11
    T11 --> DONE[线程结束]
```

---

## 异常抛出问题

### 问题分析

对于skill的抛出以前直接退出程序，导致运行失败

### 解决方案

先改链结构的异常抛出问题

```mermaid
flowchart TD
    A[runthread 执行 runOnce] --> B{runOnce 内部是否抛异常?}

    B -->|否| C[正常返回 true]
    C --> D[继续下一轮循环]

    B -->|是| E[异常向上抛出]
    E --> F[runOnce 的 catch 捕获]
    F --> G["写 chain.error<br/>lock(mtx_error)"]
    G --> H[runOnce 返回 false]

    H --> I{runthread 是否检查返回值?}
    I -->|否| J[忽略 false<br/>继续循环]
    I -->|是| K[break 退出循环]

    E -->|若 catch 未捕获| L[异常继续向上抛]
    L --> M[栈展开 逐层析构]
    M --> N{锁是 RAII?}
    N -->|是| O[lock_guard 析构 自动解锁]
    N -->|否| P[锁未释放 遗留]
    O --> Q[线程函数抛出]
    P --> Q
    Q --> R[std::terminate 终止进程]

    J --> S[异常被吞<br/>状态不一致]
    K --> T[runEnd 收尾]
```

---

## git库管理问题

当前已经上传源码到`https://github.com/RM-ZHUXI/SkillsContorl.git`,并且同步更新skill创建