# skillcontorl 项目说明

> 插件化（技能 DLL）+ 多线程调度 + 公共内存总线 + Web 控制台
>
> 本文件是项目的总入口文档：**文件结构 / 构建运行 / HTTP API / C++ 函数文档 / 技能写作规范**。
> 变更记录见 [`change.md`](./change.md)，规划见 [`ROADMAP.md`](./ROADMAP.md)。

---

## 一、项目简介

skillcontorl 把功能拆成一个个**技能（skill）动态库**，由主程序统一加载、按**技能链**串起来跑：

```
主程序 skillcontorl.exe
   ├─ libtools.dll   公共内存 / 技能加载 / 技能链管理 / 线程管理器 / 日志 / 时间 / 配置
   ├─ libnet.dll     HTTP 服务器（提供 JSON 接口 + 静态网页）
   ├─ www/index.html Web 控制台（内存监控 + 技能链图形化编排）
   └─ 技能 DLL       每个技能导出 init / run / end 三个 C 入口
```

四个核心概念：

| 概念 | 说明 |
| --- | --- |
| **公共内存** `sc::tools::Memory` | 全进程唯一、跨 DLL 共享的键值总线（`std::any` + 互斥锁），技能之间靠它交换数据 |
| **技能** | 一个导出 `init/run/end` 的 DLL，配一份 `config.json` 描述自己 |
| **技能链** `SkillList` | 一串技能的**有序**列表 + 运行模式（`once` 只跑一次 / `while` 一直循环）+ 固定节拍（每 x 毫秒跑一轮） |
| **线程管理器** `sc::tools::RunSkillsLists` | 一条技能链一条工作线程，负责按模式反复调用链内技能的 `run()` |

典型用法：网页上拖出一条技能链 → 启动 → 在「内存实时监控」里看技能写进公共内存的数据 → 随时停止。

---

## 二、文件结构

```
skillcontorl/
├── CMakeLists.txt                 顶层构建：tools + net + 主程序
├── configs/                       运行期配置（改这里不用重编译）
│   ├── config.yaml                端口 / backlog / 技能目录 / 两个 json 路径
│   ├── skills.json                技能扫描结果（程序自动写，别手改）
│   └── skillsList.json            技能链定义（技能链的"源文件"）
├── doces/                         文档
│   ├── README.md                  本文件
│   ├── change.md                  变更记录（按版本/问题清单组织）
│   ├── ROADMAP.md                 规划
│   └── DOCUMENTATION.md           预留
├── include/                       对外头文件（exe / net / 技能 都要用）
│   ├── skillscontrol.hpp          技能侧唯一入口：定义 SKILL_API + 引入 Memory
│   ├── net/
│   │   ├── net.hpp                网络模块总入口
│   │   ├── http.hpp               底层 HTTP：socket / 路由表 / HttpHand / Http
│   │   └── http_server.hpp        上层业务服务器：11 个 JSON 接口
│   └── tools/
│       ├── tools.hpp              tools 模块总入口（一次引入全部）
│       ├── tools_api.hpp          TOOLS_API 导入导出宏（跨 DLL 必须挂）
│       ├── type.hpp               Skill / SkillList 结构体 + 全局状态 + JSON 序列化
│       ├── memory.hpp             公共内存接口（实现全在 libtools.dll）
│       ├── time.hpp               时间工具
│       ├── logger.hpp             spdlog 日志单例
│       ├── yaml.hpp               yaml 读取（内联）
│       ├── findskills.hpp         技能扫描
│       ├── loadskills.hpp         单个技能 DLL 的加载与入口调用
│       ├── assign.hpp             技能链管理器 + 公共内存的 JSON 赋值
│       ├── runskillslists.hpp     技能链线程管理器（当前使用）
│       └── runskills.hpp          早期单链运行类（已被 RunSkillsLists 取代，遗留）
├── tools/                         → libtools.dll
│   ├── CMakeLists.txt
│   ├── memory.cpp                 公共内存实现 + 类型注册 + 显式实例化清单
│   ├── type.cpp                   全局状态变量定义
│   ├── time.cpp / logger.cpp
│   ├── findskills.cpp             递归扫描 skills 目录下的 config.json
│   ├── loadskills.cpp             LoadLibrary + 入口解析 + init/run/end 调用
│   ├── assign.cpp                 技能链读写（skillsList.json）+ JSON ↔ 内存
│   ├── runskillslists.cpp         线程管理器实现
│   └── runskills.cpp              遗留
├── net/                           → libnet.dll
│   ├── CMakeLists.txt
│   ├── http.cpp                   socket / 请求解析 / 路由 / 静态文件
│   └── http_server.cpp            业务 API 实现（注册 11 个路由）
├── projects/
│   └── main.cpp                   程序入口：装配各模块 + 主循环
├── skills/                        技能源码目录（每个子目录一个技能）
│   ├── haikang_camera/            示例技能①：海康相机采集（写 image 到公共内存；Linux 需 MVS SDK）
│   │   ├── CMakeLists.txt         技能单独构建（不参与顶层构建）
│   │   ├── config.json            技能描述（程序靠它发现技能）
│   │   ├── config.yaml            技能自己的配置（相机型号/曝光/存图等）
│   │   ├── include/               main.hpp + camera.hpp
│   │   ├── src/                   main.cpp + camera.cpp
│   │   └── build/                 技能自己的构建目录（产物 lib/libXXX.so 或 bin/libXXX.dll）
│   └── camera_test/               示例技能②：图像展示（把公共内存里的图用本地窗口显示，见 6.10）
│       ├── CMakeLists.txt
│       ├── config.json
│       ├── config.yaml
│       ├── include/               main.hpp + display.hpp
│       └── src/                   main.cpp + display.cpp
├── www/
│   └── index.html                 单文件控制台（技能链 / 公共内存 / 运行状态）
├── release/                       发布目录（可整体拷到别的机器运行）
│   ├── v1.0.0/win/                exe + 两个 dll + configs + www + skills + logs
│   └── v1.0.0/linux/              同上，二进制为 Linux 版（见 3.6）；另带 README.md（技能作者手册）
├── logs/                          运行日志（每次启动一个 UTF-8 文件）
├── build/                         构建目录（★ 注意分平台，见 3.6：Windows 建议用 build-win）
└── build-win/                     Windows 专用构建目录（示例命名，避免和虚拟机里的 build/ 互相覆盖）
```

**产物位置**

| 产物 | 路径 |
| --- | --- |
| 主程序 | `build/bin/skillcontorl.exe` |
| 工具库 | `build/bin/libtools.dll` |
| 网络库 | `build/bin/libnet.dll` |
| 技能 | `skills/<名字>/build/bin/lib<名字>.dll` |

> 技能产物**不在** `build/bin` 下，技能 DLL 的路径由 `skills/<名字>/config.json` 里的 `url` 决定（相对技能目录）。

---

## 三、构建与运行

### 3.1 依赖

MSYS2 UCRT64 工具链（当前工程实测环境）：

- `g++`（C++17）、`cmake` ≥ 3.10（实测 4.4）
- 第三方库（UCRT64 包）：`fmt`、`spdlog`、`nlohmann_json`、`yaml-cpp`、`opencv4`

### 3.2 构建主工程

```bash
cmake -S . -B build
cmake --build build -j8
```

### 3.3 构建技能（每个技能独立构建）

```bash
# ① 海康相机采集（Linux 需要 MVS SDK，见 3.6）
cmake -S skills/haikang_camera -B skills/haikang_camera/build
cmake --build skills/haikang_camera/build -j8

# ② 图像展示（不需要 MVS SDK，只需要 OpenCV）
cmake -S skills/camera_test -B skills/camera_test/build
cmake --build skills/camera_test/build -j8
```

> 技能通过绝对/相对路径直接链接 `build/bin/libtools.dll`（Linux 是 `build/lib/libtools.so`），所以**要先构建主工程**。
> 在发布目录（`release/v1.0.0/linux/`）里构建技能时，`camera_test` 的 CMake 会自动在同级的 `libtools.so` 与源码树 `build/lib/libtools.so` 之间选一个能找到的。

### 3.4 运行

```bash
cd <仓库根目录>          # 必须！webroot 与 configs/ 都是相对路径
build/bin/skillcontorl.exe                       # 默认读 configs/config.yaml
build/bin/skillcontorl.exe configs/config.yaml   # 或显式指定
```

- 启动后**会自动用系统默认浏览器打开控制台** `http://127.0.0.1:8080/`（由 `config.yaml` 的 `http_auto_open` 控制，设为 `false` 就只打印地址不弹浏览器）
- 端口、backlog、技能目录都在 `configs/config.yaml`
- 日志同时输出到控制台和 `logs/YYYY-MM-DD_HH-MM-SS.log`（UTF-8，级别 debug）
- `Ctrl+C` 退出：先停技能链，再关 HTTP

### 3.5 `configs/config.yaml`

```yaml
# net配置
http_port : 8080                              # http端口
http_backlog : 20                             # http最大等待数
http_auto_open : true                         # 启动后自动用默认浏览器打开控制台网页
# http_webroot : "./www"                      # 静态网页目录（可选，默认 ./www）

# skill配置
skills_url : "skills"                         # skill扫描路径（递归找 config.json）
skills_file : "configs/skills.json"           # 技能扫描结果（程序写）
skillsList_file : "configs/skillsList.json"   # 技能链定义（网页读写）
```

| 配置项 | 默认 | 说明 |
| --- | --- | --- |
| `http_port` | 8080 | 监听端口 |
| `http_backlog` | 128（代码默认）/ 20（配置） | `listen()` 队列长度 |
| `http_auto_open` | `true` | 启动成功后是否自动拉起浏览器打开控制台；`false` 时只在日志里打印地址 |
| `http_webroot` | `./www` | 静态网页目录 |
| `skills_url` | `skills` | 技能扫描根目录 |
| `skills_file` | — | 扫描结果写到这里（程序维护） |
| `skillsList_file` | — | 技能链定义文件（网页读写） |

> **控制台中文编码**：程序启动时会自动把 Windows 控制台切到 UTF-8 代码页（65001），
> 所以 spdlog 日志和技能里的 `std::cout` 中文都能正常显示（切换动作会写进日志：
> `[logger] 控制台输出代码页 936 -> 65001(UTF-8)…`）。
> 日志文件一直是 UTF-8，不受影响。**注意：改了这条逻辑后必须重启程序**，
> 正在运行的旧进程用的还是旧 `libtools.dll`。

### 3.6 发布目录怎么准备

> 以前仓库里有个 `scripts/package.ps1` 打包脚本，现已删除；下面是手工步骤。

发布目录的作用是"能整体拷到别的机器上跑"，布局（**技能是"扁平"的**，和源码树不同）：

```
release/v1.0.0/win/
├── skillcontorl.exe
├── libtools.dll
├── libnet.dll
├── configs/            config.yaml（+ skills.json / skillsList.json）
├── logs/               运行日志目录（空目录，程序自己写）
├── www/index.html
└── skills/<技能名>/
    ├── config.json     url 要改成同级 DLL 名
    └── libXXX.dll      ★真正的技能 DLL
```

从源码树复制（Windows 示例，Linux 把 exe/dll 换成无扩展名的可执行文件与 `.so`）：

```powershell
$src = "build-win\bin"                       # 或 Linux 的 build/bin
$dst = "release\v1.0.0\win"
Copy-Item "$src\skillcontorl.exe","$src\libtools.dll","$src\libnet.dll" $dst -Force
Copy-Item configs,www $dst -Recurse -Force
New-Item -ItemType Directory -Force "$dst\logs" | Out-Null

# 技能：复制"真 DLL"，并把它放到 config.json 同级、把 url 改成同级文件名
Copy-Item skills\haikang_camera\build\bin\libHAIKANG_CAMERA.dll "$dst\skills\haikang_camera\" -Force
Copy-Item skills\camera_test\build\lib\libCAMERA_TEST.dll          "$dst\skills\camera_test\"     -Force
```

> Linux 版同理：`libHAIKANG_CAMERA.so` / `libCAMERA_TEST.so`。

坑（都是踩过的）：

| 事项 | 说明 |
| --- | --- |
| **别拷成 `.dll.a`** | `.dll.a` 是导入库（给别的程序链接用），**不是可运行的 DLL**。只拷它会得到 `LoadLibrary 失败 ... 错误: 126`（找不到模块） |
| 技能 `url` 与 DLL 位置要对应 | `config.json` 的 `url` 是**相对技能目录**的路径；扁平布局下要写成 `libXXX.dll` |
| `config.json` **必须无 BOM** | 后端用 nlohmann 解析，带 BOM 会直接解析失败。Windows PowerShell 5.1 的 `Set-Content -Encoding UTF8` 会写 BOM，改用 `[System.IO.File]::WriteAllText($p,$s,(New-Object System.Text.UTF8Encoding($false)))` |
| 相对路径 | 程序按相对路径找 `configs/`、`www/`、`logs/`，所以**必须在发布目录里启动**（或把该目录设为工作目录） |
| 构建目录要分平台 | 共享目录（VMware hgfs）下 Windows 与虚拟机如果共用一个 `build/`，后者会把前者的 `CMakeCache.txt` 和产物覆盖掉。建议 Windows 用 `build-win`、虚拟机用 `build-linux`（或各自独立的目录） |

---

## 四、HTTP API 文档

### 4.1 通用约定

| 项 | 约定 |
| --- | --- |
| 路径 | 一律 `/api/` 开头；非 `/api/` 开头的走静态文件（webroot 默认 `./www`，可用配置里的 `http_webroot` 覆盖） |
| 格式 | 请求与响应都是 JSON（`Content-Type: application/json; charset=utf-8`） |
| **参数位置** | **只走 POST body**；后端按路径匹配路由，不支持查询串参数（`?x=1` 会被剥掉，但不解析） |
| 方法 | 服务端不区分 GET/POST（handler 只拿到 body）；**约定：读用 GET，写用 POST** |
| 返回 | 带 `status` 字段（`ok` / `error`），**失败时一定带 `message`**（前端直接把这句话显示给用户，不要自己写死文案） |
| 缓存 | 所有响应（含静态页）都带 `Cache-Control: no-store, no-cache, must-revalidate` + `Pragma: no-cache` + `Expires: 0`，避免浏览器拿旧的 `index.html` 渲染出"老界面" |
| 状态码 | 成功 200；参数/业务错误 400；**请求体过大 413**；未知路由 404；handler 抛异常 500 |
| 请求体上限 | **2 MB**；超出直接返回 `413 Payload Too Large` 并说明实际大小（原样截断后拿去解析会得到莫名其妙的 JSON 解析错误） |

### 4.2 接口一览

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| GET | `/api/GetMemory` | 公共内存概览（名字/类型/大小） |
| GET | `/api/GetMemoryValues` | 公共内存概览 + **具体值** |
| POST | `/api/SetMemory` | 按 JSON 写入公共内存 |
| POST | `/api/DelMemory` | 按键名或前缀删除 |
| GET | `/api/GetSkills` | 已发现的技能列表 |
| GET | `/api/UpdateSkills` | 重新扫描技能目录 |
| GET | `/api/GetSkillChain` | 技能链列表 |
| POST | `/api/UpdateSkillChain` | 覆盖保存技能链数组 |
| POST | `/api/StartSkillChain` | 启动技能链 |
| POST | `/api/StopSkillChain` | 停止技能链 |
| GET | `/api/GetRunStatus` | 技能链运行状态 |

### 4.3 接口详情

#### `GET /api/GetMemory`

```json
[ { "name": "name", "type": "string", "size": 7 },
  { "name": "abc",  "type": "int",    "size": 4 } ]
```

#### `GET /api/GetMemoryValues`

请求体可选：`{"names":["a","b"]}`（不给就是全部）。

```json
[ { "name": "demo_arr", "type": "vector<long long>", "size": 24, "exists": true,  "value": [1,2,3] },
  { "name": "demo_str", "type": "string",            "size": 5,  "exists": true,  "value": "hello" },
  { "name": "nope",     "type": "",                  "size": 0,  "exists": false, "value": null } ]
```

> 值转换只覆盖常用类型（标量 / 字符串 / 数值与字符串数组 / `map<string,*>`）；
> `cv::Mat` 之类会返回 `"<cv::Mat 暂不支持转 json>"` 字符串，不会报错。

#### `POST /api/SetMemory`

```json
{ "items": { "demo_num": 42, "demo_str": "hello", "demo_bool": true, "demo_arr": [1,2,3] } }
```

也接受直接给键值表（不带 `items` 包装）。已存在的键会被**覆盖**（内部走 `insertOrAssign`，不会因重名报错）。

```json
{ "status": "ok", "written": 4, "failed": [] }
```

失败示例：`{"status":"error","written":3,"failed":[{"name":"x","message":"数组元素类型不一致..."}]}`

#### `POST /api/DelMemory`

```json
{ "names": ["k1","k2"] }      // 按键名删
{ "prefix": "haikang." }      // 或按前缀批量删
```

```json
{ "status": "ok", "erased": 2 }
```

#### `GET /api/GetSkills`

```json
[ { "name": "haikang_camera", "description": "skill_description", "version": "0.0.1",
    "url": "skills\\haikang_camera\\build\\bin\\libHAIKANG_CAMERA.dll", "author": ["x1","x2"] } ]
```

#### `GET /api/UpdateSkills`

```json
{ "status": "ok", "skills_size": 1 }
```

- **没有任何技能链在运行时**：重新扫描技能目录（`skills.json` 重写，同时刷新链里引用的技能信息），返回 200。
- **有技能链正在运行时**：直接拒绝，返回 400：

  ```json
  { "status": "error", "running": 1,
    "message": "有 1 条技能链正在运行，请先「全部停止」再更新技能" }
  ```

  > 内部走 `FindSkills::update(force = true)`：`STATUS` 在程序启动时就被置为 `true`，
  > 不强制的话这个接口会**永远失败**（历史上它就一直是 400）。
  >
  > 前端行为：点「更新技能」遇到这个 400 时，会把上面的 `message` 原样显示，
  > 并弹出确认框「是否全部停止后立即重新扫描？」——确认后自动 `StopSkillChain {all:true}` 再重试，
  > 不需要用户自己去一条条停。

#### `GET /api/GetSkillChain`

```json
{ "status": "ok", "loaded": true,
  "skills_lists": [
    { "name": "test", "mode": "while", "interval": 1000,
      "list": [ { "name": "haikang_camera", "description": "...", "version": "0.0.1",
                  "url": "skills\\haikang_camera\\build\\bin\\libHAIKANG_CAMERA.dll",
                  "author": ["x1"] } ] } ] }
```

#### `POST /api/UpdateSkillChain`

body 是**完整数组**（整体覆盖，未修改的链也要带上）；也接受 `{"skills_lists":[...]}`。

```json
[ { "name": "test", "mode": "while", "interval": 1000, "list": [ { "name": "haikang_camera" } ] } ]
```

- `list` 元素可以是**技能名字符串**，也可以是完整 skill 对象；写名字时后端会用扫描结果补全。
- 校验：链名不能为空/重名；`mode` 只能是 `once` / `while`（非法值按 `once` 处理并告警）；技能必须能查到。
- 失败时原配置不变，响应带 `message`。

#### `POST /api/StartSkillChain`

```json
{ "name": "test" }        // 启动指定链
{ "all": true }           // 启动配置里的全部链
```

```json
{ "status": "ok", "started": ["test"], "failed": [], "running": 1 }
```

> 启动前会重新读取 `skillsList.json`；**已在运行的链会被拒绝**（`failed` 里给出原因），要改配置请先停止。

#### `POST /api/StopSkillChain`

```json
{ "name": "test" }  或  { "all": true }
```

```json
{ "status": "ok", "stopped": ["test"], "running": 0 }
```

> 停止是**软停止**：等当前这一轮（含正在执行的 `run()`）跑完后线程才退出。

#### `GET /api/GetRunStatus`

```json
{ "status": "ok", "running": 1,
  "lists": [ { "name": "test", "mode": "while", "running": true, "starting": false,
               "loop": 3, "current": "haikang_camera", "skill_count": 1, "error": "",
               "start_time": "2026-09-18 23:16:00", "update_time": "2026-09-18 23:16:02" } ] }
```

### 4.4 数据结构

**`Skill`**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | 技能唯一名（也是内存 key 前缀建议值） |
| `description` | string | 描述 |
| `version` | string | 版本 |
| `url` | string | DLL 路径（相对技能目录或绝对路径都可） |
| `author` | string[] | 作者 |

**`SkillList`（技能链）**

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | 链唯一 ID |
| `mode` | string | `once` 只跑一次 / `while` 一直循环 |
| `interval` | int | 固定节拍（毫秒）：每 x 毫秒开始跑一轮，见下表 |
| `list` | Skill[] 或 string[] | 按顺序执行的技能 |

**`interval` 语义（`while` 模式 = 固定节拍：每 x 毫秒开始跑一轮）**

| 值 | 含义 |
| --- | --- |
| `> 0` | **固定节拍**：第 n 轮的开始时刻 = 首轮起点 + n × `interval`；跑得快就在下一轮开头把剩余时间睡掉，单轮超时就丢弃积压的节拍（不补跑），所以**长期速率恒为 1/interval，不会像"跑完再等 x 毫秒"那样越跑越慢** |
| `= 0` | 不等待、全速循环（每轮只 `yield()` 让出时间片，**CPU 占用会很高**） |
| `< 0` | 非法，回落默认 `1000` 并写告警日志 |
| 缺省 | `1000` |

`once` 模式跑完一遍即结束，不受 `interval` 影响。
**正在运行的链不会热更新**：改完 `mode` / `interval` 要「停止」再「启动」才生效。

| 节拍参数 | 单位 | 实测能力（Windows，200 ms 节拍，4 秒） |
| --- | --- | --- |
| 平均周期 | ms | 200.7（目标 200，误差 < 1%，且**不随时间累加漂移**） |
| 抖动 | ms | min 185 / max 213（≈ ±7%，主要来自 Windows 默认 15.6 ms 定时器精度） |
| 节拍被拖慢 | - | 单轮耗时 ≥ 节拍时写告警日志，并在状态里累计 `skipped` |

**`RunState`（运行状态）**

| 字段 | 说明 |
| --- | --- |
| `running` / `starting` | 是否运行中 / 是否处于启动中间态（`starting` 包含 `init()` 阶段） |
| `loop` | 已完成的轮次（`once` 最多 1） |
| `current` | 当前正在执行的技能名（空闲时为空串） |
| `skill_count` | 链内技能数量 |
| `error` | 最近一次错误（空串表示无；`init` 失败的原因也在这里） |
| `start_time` / `update_time` | 启动时间 / 最近状态刷新时间 |
| `interval` | 本链的目标节拍（毫秒），0 = 全速 |
| `inited` | 本次 `start` 真正执行了 `init()` 的技能数（复用的不算） |
| `round_ms` | 上一轮所有 `run()` 的总耗时 |
| `tick_ms` | 上一轮起点 → 本轮起点的实际间隔（用来验证节拍准不准） |
| `skipped` | 因单轮超时被丢弃的节拍数（累计） |

### 4.5 前端调用示例

```js
// 读（GET 无 body）
const mem = await (await fetch("/api/GetMemoryValues", { cache: "no-store" })).json();

// 写（POST + JSON body，参数只能放 body）
await fetch("/api/SetMemory", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ items: { "demo": 1 } })
});

// 启停技能链
await fetch("/api/StartSkillChain", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ name: "test" })
});
```

---

## 五、函数功能文档（C++）

命名空间：工具库为 `sc::tools`，网络库为 `sc::net`。
`Skill` / `SkillList` 两个结构体定义在**全局命名空间**（`include/tools/type.hpp`）。

### 5.1 `sc::tools::Memory`（公共内存）

全进程唯一的内存总线，**所有非模板实现都在 libtools.dll**，`exe` / `net` / 技能 DLL 共享同一份数据。

| 成员 | 说明 |
| --- | --- |
| `Memory()` / `~Memory()` | 构造 / 析构（不可拷贝） |
| `registerTypeAll()`（static） | 注册全部内置类型；**构造 `Memory` 时自动调用**（幂等，`main` 里再显式调一次也无害） |
| `registerType<T>(typeName, sizeFn)`（static） | 注册自定义类型的类型名与大小回调 |
| `typeNameOf(const std::type_info&)`（static） | 类型 → 可读名字 |
| `insert(name, value)` | 写入；**重名会抛 `std::runtime_error`**。字面量/`const char*` 会归一化成 `std::string` |
| `insertOrAssign<T>(name, value)` | 存在则覆盖（推荐） |
| `get<T>(name, out, why=nullptr)` | 读取；失败返回 `false`，`why` 里写明「不存在（当前共 N 项）」或「类型不匹配：实际=x，期望=y」 |
| `get<T>(name)` | 读取，失败抛异常（异常信息含原因） |
| `insertString` / `insertAny` | 非模板后端（也可以直接塞 `std::any`） |
| `getAny(name, out[, why])` | 取出 `std::any`（值类型由调用方判断） |
| `erase(name)` / `erasePrefix(prefix)` | 删除单个 / 按前缀批量删除（返回删除条数） |
| `exist(name)` / `getTypeName(name)` / `getSize(name)` | 查询（不存在时类型名返回空串、大小为 0） |
| `size()` / `keys()` | 条目数 / 全部键名 |
| `clear()` | 清空 |
| `toJsonObject()` / `toJson(indent)` | 导出为 `[{name,type,size}, ...]` |

**类型规则（重要）**

- 模板的定义与**显式实例化**都在 `tools/memory.cpp`：**只有清单里列出的类型能用**，
  否则链接期报未定义。清单覆盖：`int / unsigned / long long / unsigned long long / float / double / bool / char / std::string`，
  以及常见容器（`vector` / `deque` / `list` / `set` / `map` / `array` / `pair` / `tuple` / `complex`）
  和 OpenCV 类型（`cv::Mat / Point / Size / Rect / Scalar / Vec* / Matx* / KeyPoint / DMatch` 及其 `vector` 版本）。
- 需要新类型：在 `memory.cpp` 里加一行 `TOOLS_INSTANTIATE_IO(你的类型)`；
  想让网页也能显示值，再到 `tools/assign.cpp` 的 `anyToJson()` 里加一条分支。
- `get<T>` 要求类型**完全一致**（`std::any` 的规则），不做数值隐式转换：
  `insert<int>` 之后必须 `get<int>`，`get<long long>` 会失败并提示类型不匹配。

**跨 DLL 约定**

- 值的构造与析构都发生在 libtools.dll 内部，所以技能 DLL 卸载后公共内存里的数据依然安全。
- 非模板接口都挂了 `TOOLS_API`；**给 libtools 新增跨库接口时必须挂这个宏**，
  因为一旦有显式 `dllexport`，MinGW 的"自动导出全部符号"就被关闭了。

### 5.2 `sc::tools::Time`

| 成员 | 说明 |
| --- | --- |
| `nows()` / `nowms()` / `nowus()` / `nowns()` | 当前时间戳（秒/毫秒/微秒/纳秒） |
| `nowDate()` / `nowTime()` | 当前日期 `xxxx-xx-xx` / 时间 `xx:xx:xx` |
| `getTime()` | 秒表：返回距上次调用的毫秒数（首次返回距构造时间） |
| `clear()` | 重置秒表 |

### 5.3 `sc::tools::FindSkills`（技能扫描）

| 成员 | 说明 |
| --- | --- |
| `FindSkills(configs)` | 传入 `config.yaml` 路径；构造时立即扫描一次并写回 `skills.json` |
| `update(force = false)` | 重新扫描并写回 `skills.json`。默认在 `STATUS=true`（程序运行中）时拒绝；`force=true` 强制重扫（HTTP 层在确认没有链在跑后会这么调）；返回是否成功 |
| `get_skills()` | `std::vector<Skill>` |
| `get_skill_once(name)` | 取单个技能；不存在**抛异常** |

扫描规则：递归遍历 `skills_url`，凡是含 `config.json` 的目录即视为一个技能；技能重名直接抛错。

### 5.4 `sc::tools::Assign`（技能链管理 + 内存 JSON 赋值）

| 成员 | 说明 |
| --- | --- |
| `Assign(config, find)` | 构造时从 `skillsList_file` 载入（失败只告警） |
| `update()` | 重新从文件加载（`LOAD_STATUS`：加载中 `false`，成功 `true`） |
| `set(map)` | 覆盖保存并落盘 |
| `getListsJson()` / `get_all_lists_json()` | 链列表 → JSON（数组 / 字符串） |
| `setListsJson(arr)` | JSON 数组 → 覆盖保存（先落盘再更新内存，失败保持原状） |
| `getLists()` / `getList(name)` / `hasList(name)` | 读取（`getList` 不存在时抛异常） |
| `upsertList(sl)` / `removeList(name)` | 增改 / 删除并落盘 |
| `assignMemory(mem, items, report)`（static） | `{"键":值}` 写进内存（覆盖），返回写入数与失败明细 |
| `readMemoryValues(mem, names)`（static） | 读出「名字/类型/大小/值/是否存在」 |
| `eraseMemory(mem, names)`（static） | 按键名批量删除 |

解析容错：字段缺失、数字写成字符串、`list` 用名字或对象、单条链写错只跳过该条 —— 都不会让整份配置失效。

### 5.5 `sc::tools::LoadSkills`（单个技能的加载与调用）

| 成员 | 说明 |
| --- | --- |
| `LoadSkills(skill)` | 加载 DLL（失败抛异常），入口符号此时不解析 |
| `load(Memory*)` | 解析 `init/run/end` 并**调用一次** `init()`（同一个对象重复调用只 init 一次；`RunSkillsLists` 每次 `start` 都用新对象，所以每次 `start` 都会重新 init） |
| `runOnce()` | 只调用 `run()`（必须先 `load`） |
| `end()` | 只调用 `end()`（技能没导出 end 就什么都不做） |
| `run(Memory*)` | = `load + runOnce + end`（一次性用法） |
| `unload()` | 卸载 DLL（析构里会调用） |
| `loaded()` / `info()` / `path()` | 状态与信息 |

入口查找：先按规范名 `init` / `run` / `end` 找；找不到会回退到 Itanium 修饰名
（`_Z4initPN5tools6MemoryE` 等）并**告警**；`init` / `run` 缺失直接抛异常，绝不静默跳过。

### 5.6 `sc::tools::RunSkillsLists`（技能链线程管理器）

| 成员 | 说明 |
| --- | --- |
| `RunSkillsLists(Memory*)` | 注入公共内存 |
| `start(const SkillList&)` | 启动一条链：**按 list 顺序 load DLL + `init()`**（每次都 init），全部成功才起线程；已在运行、或某个技能 `init` 失败则返回 `false`（失败会回滚：已 init 的技能立刻 `end` + 卸载，原因写进 `state.error`）；非法 `mode` 按 `once`；`interval<0` 回落 1000 |
| `stop(name)` | 停止并 join 线程，然后对本次 init 过的技能调用 **`end()` 并卸载 DLL**（返回时 `end()` 已经执行完） |
| `stopAll()` | 停止全部，返回条数（每条都会走 `end()`） |
| `remove(name)` | 停止（含 `end`）并移除运行记录 |
| `setShareSkillAcrossChains(bool)` | 同一个技能被多条链同时使用时的策略：`true`（默认）共用同一份实例，`false` 独占（后启动的链直接启动失败）。也可在 `config.yaml` 里写 `skills_exclusive: true` 打开独占 |
| `running(name)` / `runningCount()` / `has(name)` / `names()` | 查询 |
| `states()` | `std::vector<RunState>` 快照 |
| `statusJson()` | 状态 JSON（给 `/api/GetRunStatus`） |

**生命周期与线程模型（2.0：以 start/stop 为边界）**

1. **`init` 每次 `start` 一次**：`start()` 里对链内每个技能 `load`（LoadLibrary）+ `init()`。
   同一条链「停止 → 再启动」= 完整地重新初始化一次设备，不是"进程起来只 init 一次"。
2. **`run` 每轮一次**：每轮对链内技能按顺序调用 `run()`。
3. **`end` 每次 `stop` 一次**：`stop()` 里调用 `end()` 并卸载 DLL。
   同一个技能被多条链同时使用时按引用计数：**第一个使用者 `init`，最后一个使用者 `stop` 时才 `end` + 卸载**
   （一个 DLL 只有一份全局变量，重复 `init` 会把别人正在用的资源覆盖掉）。
4. **线程自然结束时也会收尾**：`once` 跑完 / 出错 / 退出（`STATUS=false`）时，工作线程自己把技能 `end` + 卸载。
5. **一条链一条线程**：状态更新用一把互斥锁保护；`join()` 一律在锁外，避免死锁；
   `load/init/end/卸载` 另用一把锁串行化，且不阻塞状态查询。
6. **软停止**：`stop()` 只置停止标志，线程跑完当前技能后自然退出，不强行杀线程。
7. **固定节拍**：第 n 轮开始时刻 = 首轮起点 + n × `interval`（不是"跑完再等 x 毫秒"）；
   等待过程每 50 ms 醒一次检查停止标志，最后一段按剩余时间精确睡；
   单轮超时丢弃积压的节拍并在日志告警（`skipped` 计数），`=0` 时每轮只 `yield()`。

### 5.7 `sc::tools::RunSkills`（遗留）

早期的单链运行类（`run()` 里每个技能都 `LoadSkills::run`，会重复 `init`），
已被 `RunSkillsLists` 取代，保留未删除，新代码不要再用。

### 5.8 其他工具

| 名称 | 说明 |
| --- | --- |
| `logger()` | `std::shared_ptr<spdlog::logger>`：控制台 + `logs/xxx.log`，级别 debug，`info` 以上自动 flush；**首次调用会自动切控制台代码页** |
| `setupConsoleUtf8()` | 把 Windows 控制台切到 UTF-8(65001)，返回切换前的代码页（0 = 无控制台/输出被重定向）；已被 `logger()` 自动调用 |
| `load(path)` / `read<T>(node, key)` | yaml-cpp 的薄封装（内联；**失败抛 `std::runtime_error`，不再 `exit(1)`** —— 理由见 6.8） |
| `TOOLS_API` | 跨 DLL 导入导出宏（`tools_api.hpp`） |
| `sc::tools::STATUS` | 全局运行开关：`true` 才允许技能跑；`false` 时技能链线程会退出 |
| `sc::tools::LOAD_STATUS` | 技能链是否加载完成（技能链加载中为 `false`） |

### 5.9 `sc::net`（HTTP）

| 类型 | 说明 |
| --- | --- |
| `HttpHand` | 响应封装：`setStatus(int)` / `setHeader(k,v)` / `setBody(string)`；`toString()` 生成报文 |
| `HttpHandler` | `std::function<void(std::string body, HttpHand&)>` |
| `Http(config)` | 底层服务器：`registerHandler(path, handler)` / `start()`（非阻塞）/ `stop()` / `port()`；每连接一个线程 |
| `HttpServer(config, memory, findSkills, assign, runSkillsLists)` | 业务服务器：构造时注册全部 11 个路由；`start()` 成功后按 `http_auto_open` 自动打开浏览器 |
| `consoleUrl()` / `openConsole()` / `autoOpenConsole()` | 控制台地址（`http://127.0.0.1:<端口>/`）／用默认浏览器打开（Windows `ShellExecuteA`，其它平台 `xdg-open`）／配置里是否允许自动打开 |

请求解析要点：先收到 `\r\n\r\n`，再按 `Content-Length` 收满 body（2MB 上限）；
路由匹配前剥掉 `?查询串`；非 `/api/` 路径按 `webroot` 提供静态文件。

### 5.10 程序主流程（`projects/main.cpp`）

```
init(argc, argv)           解析参数、初始化日志、Memory::registerTypeAll()
                           （registerTypeAll 在构造 Memory 时已自动跑过一次，这里是幂等的保险）
FindSkills findSkills      扫描技能 → 写 configs/skills.json
Assign assign              读 configs/skillsList.json
RunSkillsLists runner(&memory)
HttpServer httpServer(...) 注册路由并 start()
STATUS = true              允许技能运行；注册 SIGINT/SIGTERM
while (g_running)          主循环（200ms 心跳）
退出：STATUS=false → runner.stopAll() → httpServer.stop()
```

---

## 六、SKILL 写作模板与约定

### 6.1 目录结构

```
skills/<技能名>/
├── CMakeLists.txt          单独构建
├── config.json             技能描述（程序靠它发现你）
├── include/main.hpp        入口声明
└── src/main.cpp            入口实现
```

### 6.2 `config.json` 模板

```json
{
    "name" : "my_skill",
    "description" : "一句话说明这个技能干什么",
    "version" : "0.0.1",
    "author" : ["你的名字"],
    "url" : "build\\bin\\libMY_SKILL.dll"
}
```

| 字段 | 约定 |
| --- | --- |
| `name` | **全局唯一**，重名会让扫描直接抛错；建议与目录名一致 |
| `url` | 相对技能目录的 DLL 路径（Windows 路径在 JSON 里写 `\\`），也可以写绝对路径 |
| 其它 | 会被 `/api/GetSkills` 与网页展示，写清楚便于运维 |

### 6.3 `CMakeLists.txt` 模板

```cmake
cmake_minimum_required(VERSION 3.10)
project(my_skill VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 产物固定输出到 <技能目录>/build/bin（要和 config.json 的 url 对上）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(MY_SKILL SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

target_include_directories(MY_SKILL PUBLIC
    ${CMAKE_SOURCE_DIR}/include          # 技能自己的 include
    ${CMAKE_SOURCE_DIR}/../../include    # 主工程 include（skillscontrol.hpp / tools/）
)

# 只链接主工程的 libtools.dll：Memory 的唯一实现就在里面
# （注意：技能目录都在 <根>/skills/<名>，所以这里是 ../../build/bin）
target_link_libraries(MY_SKILL PUBLIC
    ${CMAKE_SOURCE_DIR}/../../build/bin/libtools.dll
)
```

> **技能还需要日志或 OpenCV 时**（默认不需要，别加）：
> 在技能 CMakeLists 里 `find_package(spdlog REQUIRED)` / `find_package(OpenCV REQUIRED)`，
> 把 `spdlog::spdlog` / `fmt::fmt` / `${OpenCV_LIBS}` 一起加进 `target_link_libraries`，
> 并把编译定义对齐（`SPDLOG_COMPILED_LIB` / `SPDLOG_FMT_EXTERNAL` / `SPDLOG_SHARED_LIB`）。
> 原因见约定 #9：`libtools.dll` 改成显式导出后，技能**不能**再蹭它的 fmt/spdlog 符号。

### 6.4 `include/main.hpp` 模板

```cpp
#ifndef MAIN_HPP
#define MAIN_HPP

#include <string>

#include "skillscontrol.hpp"      // 提供 SKILL_API 与 sc::tools::Memory

/**
 * @brief 初始化：由 skillcontorl 注入公共内存
 * @warning 声明与实现的签名必须完全一致，否则导出名会被 C++ 修饰导致调用不到
 */
SKILL_API void init(sc::tools::Memory *memory);   // 必须存在

/**
 * @brief 运行：技能链每一轮调用一次
 */
SKILL_API void run();                             // 必须存在

/**
 * @brief 收尾：每次停止技能链时调用一次（也是 DLL 被卸载前最后一次机会）
 */
SKILL_API void end();                             // 可选

#endif
```

> `SKILL_API` 已经是 `extern "C" __declspec(dllexport)`，**不要再套一层 `extern "C" { }`**，
> 也不要在别处重复声明成 `Memory&`（`&` 与 `*` 混用是历史上让 `init` 找不到的头号原因）。

### 6.5 `src/main.cpp` 模板

```cpp
#include "main.hpp"

namespace {
    // init 注入的内存指针，run()/end() 里必须先判空
    sc::tools::Memory *mem = nullptr;
}

SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr) return;

    // 建议键名带技能前缀，避免和其它技能撞键
    memory->insertOrAssign("my_skill.name",    "my_skill");
    memory->insertOrAssign("my_skill.counter", 0);

    mem = memory;
}

SKILL_API void run()
{
    if (mem == nullptr) return;          // init 没跑成功就直接返回，别崩

    // ★ run() 只干一次活就返回 ★
    //   绝对不要在这里写 while(true) / for(;;)：循环由技能链负责（mode=while + interval）
    int n = 0;
    mem->get("my_skill.counter", n);     // 取不到就保持默认值
    mem->insertOrAssign("my_skill.counter", n + 1);

    // 例如采集一帧就结束：
    // cv::Mat frame = camera.capture();
    // mem->insertOrAssign("my_skill.frame", frame);
}

SKILL_API void end()
{
    if (mem == nullptr) return;
    // 收尾：释放自己的资源（每次「停止技能链」都会调用一次，可以在这里关设备/停线程）
}
```

### 6.6 硬性约定（必须遵守）

| # | 约定 | 原因 |
| --- | --- | --- |
| 1 | 三个入口必须用 `SKILL_API`（= `extern "C"`）导出，且声明与实现签名**完全一致** | 签名不一致会变成另一个 C++ 重载，导出名被修饰成 `_Z4initPN5tools6MemoryE`，主程序按 `init` 找不到 → 技能静默不初始化 |
| 2 | `init()` **每次「启动技能链」调用一次**（不是全局一次），可以把"打开设备/建连接/读配置"放在这里；不要在 `init` 里写"每轮都要做"的逻辑 | `while` 链不会每轮 init（每轮只调 `run()`），但「停止 → 再启动」会重新 init 一次；对同一个键重复用 `insert` 会抛异常把线程打死，用 `insertOrAssign` |
| 3 | 写公共内存优先用 `insertOrAssign`；用 `insert` 就要自己保证键不重复 | `insert` 重名抛 `std::runtime_error` |
| 4 | 使用的类型必须在 `memory.cpp` 的显式实例化清单里（或自己加一行） | `Memory` 的模板定义不在头文件里，未实例化 = 链接期未定义符号 |
| 5 | 内存键名建议 `<技能名>.<字段>` | 公共内存是全局共享的，避免不同技能互相踩；也方便按前缀清理 |
| 6 | **`run()` 里不允许有死循环!!!**（`while(true)` / `for(;;)`），必须是"干一次活就返回" | 循环由技能链负责（`mode=while` + `interval`）。在 `run()` 里套死循环会让：停止按钮永远不生效、`StopSkillChain` 请求挂住、退出程序卡死、链里后续技能永远轮不到、轮次/当前技能状态全部失真 —— 详见 6.7 |
| 7 | `run()` 单次执行尽量别超过几秒，长活要拆开做 | 停止是软停止，链的线程必须等当前 `run()` 返回；单次超过 3 秒会在日志里告警 |
| 8 | `run()` / `end()` 里用注入的指针前必须判空 | `init` 失败时指针为空，直接解引用会让**整个进程**崩溃 |
| 9 | 不要在技能里调用 `exit()` / `abort()` / `std::terminate`，也不要创建无法回收的线程 | 技能跑在主程序进程内，退出整个程序就没了 |
| 10 | 技能 DLL 里**不要** `#include "tools/tools.hpp"`，只 include 你需要的头 | 会把 spdlog/yaml-cpp/opencv 拖进技能 DLL，进而要求技能自己链接 fmt/spdlog |
| 11 | 技能不需要（也不应该）自己调用 `end()`；`stop` 之后 `init` 会被再调一次，所以 `init` 必须是"可重复进入"的（每次进来都把状态建好） | 生命周期由 `RunSkillsLists` 统一管理：`start`→`init`、`stop`→`end`+卸载 |
| 12 | 公共内存里的键**跨 `stop/start` 保留**：第二次 `init` 时上一轮写的键还在 | 所以 `init` 里一律用 `insertOrAssign`，或在 `end()` 里 `mem->erasePrefix("技能名.")` 清掉自己的键 |

### 6.7 `run()` 的正确写法（**不允许死循环**）

`run()` 是"**干一次活就返回**"的普通函数：采集一帧 / 处理一次 / 写一次公共内存，然后 `return`。
**循环交给技能链**（`mode=while` + `interval`），不要在 `run()` 里自己套一层循环。

❌ 错误写法（死循环）：

```cpp
SKILL_API void run()
{
    while (true)                      // ← 整个链的线程永远出不来
    {
        cv::Mat f = camera.capture();
        mem->insertOrAssign("cam.frame", f);
    }
}
```

✅ 正确写法：

```cpp
SKILL_API void run()
{
    if (mem == nullptr) return;

    cv::Mat f = camera.capture();     // 干一次活
    mem->insertOrAssign("cam.frame", f);   // 写一次公共内存
}                                     // 立刻返回，循环由链负责
```

再配一条链就是"30fps 连续采集"，而且随时能停、频率随时能改：

```json
{ "name": "cam_loop", "mode": "while", "interval": 33, "list": ["my_skill"] }
```

**在 `run()` 里写死循环会连带坏掉一串东西：**

| 后果 | 说明 |
| --- | --- |
| 「停止」永远不生效 | 停止是软停止（`stopFlag` 只在两轮之间检查），线程卡在 `run()` 里就永远看不到这个标志 |
| `POST /api/StopSkillChain` 请求挂住 | 该接口要 `join` 工作线程，线程不返回 → HTTP 请求不返回，浏览器一直转圈 |
| 退出程序卡死 | `main` 退出时 `stopAll()` 同样要 `join`，进程关不掉，只能强杀 |
| 链里后面的技能永远轮不到 | 一个线程按顺序跑技能，第一个不返回，第二个就没机会执行 |
| 轮次 / 当前技能状态失真 | `loop` 恒为 0、`current` 恒为它，网页上的运行状态失去意义 |
| `interval` 失效 | 循环写进了技能里，链的节拍自然管不到 |

**违背时能在日志里看到**：`RunSkillsLists` 会给每次 `run()` 计时，单次超过 3 秒就告警：

```
[RunSkillsLists] 技能 my_skill 单次 run() 耗时 5123 ms，疑似在 run() 里写了死循环或长阻塞
（约定：run() 必须干一次活就返回，循环交给技能链的 mode=while）
```

> 这只是告警：`std::thread` 没有"超时打断"能力，真正的死循环只能靠这条约定避免。

### 6.8 常见坑速查

| 现象 | 原因 | 解决 |
| --- | --- | --- |
| 网页里看不到本技能写的内存 | `init` 没被调用（导出名被修饰 / 缺 init） | 用 `LoadLibrary` 工具确认导出名；保证 `SKILL_API` + 签名一致 |
| 日志报 `动态库缺少 init 导出符号` | 同上，或 DLL 根本没编出来 | 检查技能构建产物路径与 `config.json.url` 是否一致 |
| 链第二轮启动就报「名称已存在」 | `init` 里用了 `insert`，而公共内存里的键跨 `stop/start` 一直保留 | 改用 `insertOrAssign`，或在 `end()` 里 `mem->erasePrefix("myskill.")` |
| 停止再启动后 `init` 又跑了一遍（日志里看到第二次"动态库加载成功"） | 这是 2.0 的设计：`stop` = `end()` + 卸载 DLL，`start` = 重新加载 + `init()` | 让 `init` 可重复进入（把设备打开/配置读取放在这里，把释放放在 `end`），不要依赖"进程级只 init 一次" |
| 链接期报 `undefined reference to Memory::insert<...>` | 该类型没在 `memory.cpp` 实例化 | 在 `TOOLS_INSTANTIATE_IO` 清单里加一行并重新编译 libtools |
| 网页"值"列显示 `<cv::Mat 暂不支持转 json>` | 该类型没在 `assign.cpp::anyToJson` 里处理 | 加一条 `typeid` 分支 |
| `get` 一直返回 false | 类型不匹配（`int` vs `long long`）或键不存在 | 打开 `get(..., &why)` 把原因打出来；`/api/GetMemoryValues` 看实际类型 |
| 技能列表更新失败（400） | 有技能链正在运行 | 先「全部停止」再点更新技能 |
| 启动后技能报 `动态库加载失败 ... 错误: 126` | 126 = 找不到模块：技能的 `.dll` 不在 `config.json` 的 `url` 指向的位置（发布目录里最常见的是只拷了 `libXXX.dll.a`） | 手动确认 `skills/<名>/` 下确实有 `libXXX.dll`（见 3.6 的发布目录准备步骤） |
| **点了「停止」没反应、`StopSkillChain` 请求一直不返回、程序关不掉** | 技能 `run()` 里写了死循环（或长阻塞），链的线程卡在里面 | 去掉 `run()` 里的循环（见 6.7），循环交给链的 `mode=while` + `interval`；日志里会有 `单次 run() 耗时 xxx ms` 的告警 |
| 技能链启动被拒 | 该链已在运行 | 先停止再启动（配置不会热更新） |

### 6.10 示例技能：`camera_test`（图像展示 · 本地窗口）

职责只有一件：**把公共内存里的图"显示"出来** —— 内存里是什么就显示什么，不改内存、不写文件、不参与网页。
采集由别的技能负责（例如 `haikang_camera` 把画面写进公共内存的 `image`）。

**它做什么**

1. `run()` 每轮从 `source_key`（默认 `image`）`get` 一帧：取不到（或 `empty()`）就直接返回，窗口继续显示上一帧；
2. 取到了就把帧交给**显示线程**（`Displayer::show()`），显示线程 `imshow` 到窗口；
3. 窗口刚建立、还没有图时，先显示一张"waiting for image in memory ..."的占位图 —— 一眼就能区分"技能没跑"和"上游还没出图"；
4. 窗口里按 `ESC` 或 `q` 关掉窗口（技能本身继续跑，`end()` 时窗口自动销毁）；
5. **它不往公共内存写任何键**（只读），显示相关的统计只进日志。

**为什么窗口要单独起一个线程**

OpenCV 的 `namedWindow` / `imshow` / `waitKey` / `destroyWindow` 必须在同一个线程里成对使用；而技能链的线程属于框架，GUI 崩了会把宿主进程一起带走。
所以 `viewer::Displayer` 自己起一个显示线程：`run()`（框架线程）只把最新帧用 mutex + 拷贝递过去，所有 GUI 调用都在显示线程里发生。

**配置**（`skills/camera_test/config.yaml`）

| 配置 | 默认 | 说明 |
| --- | --- | --- |
| `source_key` | `image` | 从公共内存哪个键取图（要和上游技能写入的键一致） |
| `window` | `true` | 是否开窗口。`false` = 只看不显示（`start()` 仍返回成功，便于临时静音） |
| `window_width` | `1280` | 窗口最大显示宽度，更宽的帧等比缩小；`0` = 原尺寸 |
| `window_name` | `camera_test · 图像显示` | 窗口标题（标题里会带本次已显示帧数 `#N`） |

> 没有图形环境（无 `DISPLAY`、无桌面会话）时窗口建不起来：`init()` 会**抛异常**，技能链停止并在日志/网页上给出原因，而不是静默黑屏。

**接进技能链**（采集 → 显示）：

```json
{ "name": "camera_preview", "mode": "while", "interval": 200,
  "list": ["haikang_camera", "camera_test"] }
```

> `list` 里直接写**技能名**即可，保存时后端会用扫描结果补全成完整对象。
> 仓库里已经加好这条链（`configs/skillsList.json`）：网页上点「启动」，`haikang_camera` 采一帧写进内存，`camera_test` 紧接着把它显示到窗口。
> **显示窗口属于技能自己的线程，和网页无关** —— 网页只负责看内存、看日志、起停技能链。

**故障排查**

| 现象 | 原因 / 解决 |
| --- | --- |
| 窗口一直显示 `waiting for image in memory ...` | 上游没出图：检查 `haikang_camera` 是否在跑、`source_key` 是否和它写入的键一致（默认都是 `image`） |
| 日志报 `显示窗口打不开` | 没有图形环境（DISPLAY）或 `window=false`；在带桌面的会话里运行，或检查 `config.yaml` |
| 窗口弹出来了但图不动 | 链的 `interval` 太大、或上游 `run()` 慢；看 `logs/` 里 `单次 run() 耗时` 告警 |

### 6.11 调试建议

1. 看 `logs/` 里最新日志：`LoadSkills` 会打印入口名（`init=init, run=run, end=end`）与加载路径。
2. `GET /api/GetMemoryValues` 直接看键、类型、大小、值 —— 比 `std::cout` 快。
3. `GET /api/GetRunStatus` 看 `loop` / `current` / `error`。
4. 技能的 `std::cout` 会混在主程序标准输出里；结构化信息请用 `sc::tools::logger()`（需要额外链接 spdlog）。

---

## 七、全局约定

- **命名空间**：工具库 `sc::tools`，网络库 `sc::net`；`Skill` / `SkillList` 在全局命名空间。
- **文件名**：全小写、无分隔（`findskills.cpp` / `runskillslists.cpp`）；类名大驼峰（`RunSkillsLists`）。
- **注释模板**（所有源文件统一）：

  ```cpp
  /**
   * @file        xxx.cpp
   * @brief       一句话说明
   * @author      xxx xxx@xxx.com
   * @date        xxxx-xx-xx
   * @version     xxx
   * @par         本次版本改了什么（可多段 @version + @par 追加）
   */
  ```

- **跨库接口必须挂 `TOOLS_API`**：一旦 libtools.dll 里有显式导出，MinGW 的自动导出就关闭了，
  漏掉宏的类/函数在 exe 或其它 DLL 里会变成"找不到程序入口点"。
- **JSON 字段**：技能链配置对外统一用 `name / mode / interval / list`；
  解析一律走容错取值（数字与字符串互认、字段可缺省）。
- **错误处理**：网络层 handler 抛异常会被捕获成 500；配置里的单条错误只跳过该条并写 warn 日志，
  不让整份配置失效。
- **时间戳格式**：`YYYY-MM-DD HH:MM:SS`（`Time::nowDate() + " " + Time::nowTime()`）。

---

## 八、FAQ

**Q：页面打不开 / 请求 404？**
A：确认从仓库根目录启动（`webroot=./www`、`configs/` 都是相对路径）；确认端口没被占用（`config.yaml` 的 `http_port`）。

**Q：为什么改完技能链配置没生效？**
A：运行中的链不会热更新。改完「停止」再「启动」；`StartSkillChain` 每次都会重新读取配置。

**Q：为什么轮间隔填 0 之后 CPU 很高？**
A：`0` 的语义就是"不等待、全速循环"。想降 CPU 就填正数（例如 1000）。

**Q：`UpdateSkills` 为什么总是失败？**
A：只有**有技能链在跑**的时候才会失败（返回 400 并说明有几条在跑）。全部停止后点「更新技能」即可成功。

**Q：发布到别的机器上，技能加载报 `错误: 126`？**
A：126 是"找不到模块"。检查 `skills/<技能名>/` 下是否有真正的 `libXXX.dll`——
把构建产物里的 **`.dll.a` 导入库**当成 DLL 拷过去是最常见的原因。按 3.6 的步骤把真正的 `libXXX.dll` 放到 `skills/<名>/` 即可。

**Q：Ctrl+C 之后程序好像还在，下次启动报 `bind() failed on port 8080`？**
A：这是**进程没真正退出**，不是 net 没关 socket（`SO_REUSEADDR` 早就设了，TIME_WAIT 不会挡住重启）。
旧版本有两个卡死点，都已在 `net/http.cpp` 修掉：

1. `Http::stop()` 关掉监听 fd 之后 `join()` accept 线程 —— 但 **POSIX 下"一个线程阻塞在 `accept()` 时另一个线程 `close()` 同一个 fd"并不会唤醒它**：`accept()` 不返回 → 线程不退出 → `join()` 永久阻塞 → 主线程卡在 `httpServer.stop()`，永远走不到 `[main] exit at`，监听 socket 也就一直绑着（表现就是"程序结束了、8080 还占着"）；
2. 客户端线程卡在某个 API（比如在等一个卡住的技能线程）时，`~Http()` 会无限等它们结束。

现在：accept 循环改成 `select()` 带 **200ms 超时**（不依赖唤醒语义，跨平台都稳）、`stop()` 先 `shutdown()` 再 `close()`、`~Http()` 最多等 5 秒就告警并继续退出；日志里也加了
`[Http] 正在停止` → `[Http] 等待 accept 线程退出...` → `[Http] stopped`，以后真卡住能一眼看出卡在哪一步。

**先确认是谁占着端口**：

```bash
ss -tlnp | grep 8080            # 有 LISTEN + PID = 还有残留进程；只有 TIME_WAIT = 无害
ps -ef | grep skillcontorl
kill -9 <PID>                   # 残留进程直接强杀（SIGINT/SIGTERM 可能正好卡在上面那个 join 上）
```

> 这条修复要**重新构建框架**（主程序 / `libnet.*`）才会生效。

**Q：我改了 `www/index.html`（或更新了程序），但页面还是老样子？**
A：服务器现在对所有响应都发 `Cache-Control: no-store`，正常情况下刷新即可看到新界面。
如果你手上是**更早版本**缓存的页面，第一次需要强制刷新一次（`Ctrl+F5`）把它冲掉。
另外：界面上的失败提示一律来自服务端返回的 `message`，所以提示内容和你看到的日志是一致的。

**Q：控制台里中文是乱码（`鏇存柊skills鏁版嵁` 这种）？**
A：这是代码页问题：程序输出的是 UTF-8，而控制台按 GBK(936) 解释。**新版程序启动时会自动切到 UTF-8(65001)**，
日志里会有一行 `[logger] 控制台输出代码页 936 -> 65001(UTF-8)，中文显示不再乱码`。
如果还乱码，说明跑的是旧 `libtools.dll`（重新构建框架并重启程序）。
日志文件本身一直是正确的 UTF-8，用记事本 / VS Code 直接打开就是对的。

**Q：把输出重定向到文件后，用编辑器打开是乱码？**
A：重定向时控制台代码页不参与，文件里就是**纯 UTF-8 字节**，用支持 UTF-8 的编辑器打开即可。

**Q：技能写进内存的数据，技能卸载后会崩吗？**
A：不会。`Memory` 的所有 `std::any` 都在 libtools.dll 内部构造，析构函数不会指向已卸载的技能 DLL。

**Q：能不能同时跑多条技能链？**
A：可以，一条链一条线程。同一个技能被多条链同时使用时默认**共用同一份实例**：只有第一个使用者 `init`，最后一个使用者 `stop` 时才 `end`（所以技能内部状态要自己考虑并发）；需要互斥就在 `config.yaml` 里写 `skills_exclusive: true`。

**Q：停止再启动，技能里上一次写进公共内存的数据还在吗？**
A：还在。`stop()` 只做 `end()` + 卸载 DLL，**不会清公共内存**。所以 `init` 里对同一个键用 `insert` 的写法，第二次 `start` 会报「名称已存在」；
请用 `insertOrAssign`，或者在 `end()` 里 `mem->erasePrefix("myskill.")` 自己清干净。

**Q：POST 大一点的数据就报 413 / 塞不进整张图？**
A：HTTP 请求体上限 2 MB（见「九、已知限制」第 8 条）。大片数据不要走 HTTP：
让技能在进程里直接 `insertOrAssign` 写公共内存，网页只读元信息、只发控制指令。

---

## 九、已知限制

1. **HTTP 层不区分方法**：handler 只拿到 body，约定"读 GET / 写 POST"。
2. **停止是软停止**：技能 `run()` 正在执行时要等它返回，不做强行终止线程。
3. **`end()` 在每次 `stop()` 时调用**（同一个技能被多条链共用时，最后一个使用者停止才 `end` + 卸载）。
4. **`init()` 需要"可重复进入"**：每次 `start` 都会调用它（见约定 #2、#11）；一个技能被两条链同时使用时默认共用同一份实例（见 `skills_exclusive`）。
5. **`interval = 0` 会全速循环**：CPU 占用高，慎用。
6. **网页拖拽不支持触屏**：HTML5 DnD 在触屏浏览器不触发，用节点上的 `↑ / ↓ / ✕` 按钮。
7. **`tools/runskills.cpp`（旧 `RunSkills`）未删除**：功能已被 `RunSkillsLists` 取代，可择机清理。
8. **HTTP 请求体上限 2 MB**：超限返回 `413 Payload Too Large`：

   ```json
   {"status":"error","message":"请求体 3072 KB 超过上限 2048 KB：大块数据请让技能直接写公共内存，不要走 HTTP JSON"}
   ```

   这条限制只约束"**通过 HTTP 传进来的数据**"，不限制公共内存本身能放多少。以整幅图像为例：

   | 途径 | 可行性 |
   | --- | --- |
   | `POST /api/SetMemory` 传整幅图 | ❌ 1920×1080×3 ≈ 6.2 MB 原始字节；写成 JSON 数字数组还要再放大十几倍（每个像素值的十进制文本 + 逗号）→ 远超 2 MB |
   | `POST /api/SetMemory` 传 `cv::Mat` | ❌ 该接口的 JSON 值只认 布尔/整数/浮点/字符串/数组/字典，装不下 `cv::Mat` 这类二进制类型 |
   | 技能在进程内直接写内存 | ✅ 推荐：`mem->insertOrAssign<cv::Mat>("haikang.frame", frame);` 数据不出进程，网页只负责看元信息（名字/类型/大小）和启停技能链 |
   | 想让网页看到图像内容 | 在 `tools/assign.cpp` 的 `anyToJson()` 里给 `cv::Mat` 加一条分支，例如返回缩略图的 base64（注意控制体积），或只返回 `宽x高x通道` 摘要 |

   一句话：**HTTP 接口是"控制面"（下指令、看状态、传小数据），批量/二进制数据走技能内部的公共内存这条"数据面"。**

---

## 十、相关文档

| 文档 | 内容 |
| --- | --- |
| [`change.md`](./change.md) | 变更记录：逐文件改动、接口一览、问题清单（含根因）、验证记录 |
| [`ROADMAP.md`](./ROADMAP.md) | 项目规划目标 |
| `configs/config.yaml` | 运行参数 |
| `configs/skillsList.json` | 技能链定义（也可以由网页维护） |
