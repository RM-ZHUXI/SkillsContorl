# 变更说明（Change Log）

- **日期**：2026-09-17
- **涉及模块**：`tools`（libtools.dll）、`net`（libnet.dll）、`projects`（主程序）、`www`（控制台页面）、`configs`
- **目标**：让「公共内存」和「技能链」可以通过 JSON 在网页上被监测与操作，并新增技能链线程管理器 `RunSkillsLists`

---

## 一、总体说明

本次改动把三条链路打通：

```
网页(www)  ──JSON──▶  net/http_server  ──JSON──▶  tools/assign  ──▶  configs/skillsList.json
                            │                          │
                            │                          └──▶ 公共内存(sc::tools::Memory) 读写
                            ▼
                     tools/RunSkillsLists（线程管理器）
                            │  每条技能链一条线程
                            ▼
                     tools/LoadSkills ──▶ skill DLL（init / run / end）
```

- **公共内存**：网页可以看键、类型、大小和**具体值**，也可以按 JSON 写入、按键名或前缀删除。
- **技能链**：网页可以增删改查技能链，按 `once` / `while` 启动、停止，并实时看到轮次、当前技能、错误。
- **线程管理器**：`sc::tools::RunSkillsLists` 负责「一条链一条线程」，线程之间通过公共内存交换数据。

---

## 二、新增文件

### 0. `scripts/package.ps1`（发布打包脚本，**已删除**）

> 这个脚本连同 `scripts/` 目录一起被删掉了，发布目录改为手工准备（步骤见 `README.md` 的 3.6）。
> 下面保留它的原始说明，仅作历史记录：把构建产物整理成可直接运行的发布布局
> `release/<版本>/<平台>/`（exe + 两个库 + configs + www + logs + 扁平化的 skills），
> 自动检测"技能没构建过 / url 指向导入库(.dll.a) / 缺主程序"等常见错误，
> 并用 .NET API 写**无 BOM** 的 `config.json`（BOM 会让后端 nlohmann 解析失败）。

### 0.1 `skills/camera_test/`（新技能：图像展示 · 本地窗口）

一个专门用来**展示图像**的技能：从公共内存取图（默认键 `image`）**开一个窗口显示出来** —— 内存里是什么就显示什么。
它只读内存、不写内存、不写文件、不碰网页；采集交给 `haikang_camera`（把画面写进 `image`）。

| 文件 | 说明 |
| --- | --- |
| `include/main.hpp` | 入口声明（`SKILL_API` + `sc::tools::Memory*`） |
| `include/display.hpp` / `src/display.cpp` | `viewer::Displayer`：显示线程、等比缩放、占位图、帧数统计、按键关窗 |
| `src/main.cpp` | `init/run/end`：读配置 → 起显示线程（起不来就抛异常）→ 每轮把最新帧交给显示线程 → 收尾关窗 |
| `config.yaml` | `source_key` / `window` / `window_width` / `window_name` |
| `config.json` | 技能描述（`url` = `build\lib\libCAMERA_TEST.so`） |
| `CMakeLists.txt` | 单独构建；**不需要 MVS SDK**；自动适配两种布局找 `libtools.so`（发布目录 / 源码树 `build/lib`） |

**设计要点**

1. **不在技能链线程里开 GUI**：`namedWindow` / `imshow` / `waitKey` / `destroyWindow` 全部发生在技能自己起的显示线程里；
   框架线程（`run()`）只把最新帧用 mutex + 拷贝递过去。跨线程开窗口会崩，而技能崩了会把宿主进程一起带走。
2. **窗口起不来就明确失败**：无 DISPLAY / 桌面会话时 `start()` 返回 false，`init()` 直接抛异常，
   技能链停止并在日志与网页上给出原因，而不是静默黑屏。
3. **空内存不闪黑窗**：还没取到帧时先 `imshow` 一张 "waiting for image in memory ..." 的占位图，
   一眼区分"技能没跑"和"上游还没出图"；`run()` 取到空帧直接返回，窗口保持上一帧。
4. **`run()` 只干一次活就返回**，循环交给链的 `mode=while + interval`；窗口里按 ESC/q 只关窗口，技能继续跑。
5. **只读不写**：本技能不往公共内存写任何键（显示相关的帧数只进日志），所以它不会污染上游数据。
6. 配置用 `YAML::LoadFile` 自己读（不调用框架的 `sc::tools::load`），并用 `dladdr` 定位自己的 `config.yaml`
   —— 与 `haikang_camera` 同一套做法，从任何工作目录启动都能找到配置。
7. 日志要显式 `#include "tools/logger.hpp"`（`skillscontrol.hpp` 只带 `Memory`），技能 CMake 需链 `spdlog/fmt`。

**同时改的**

- `configs/skillsList.json`（两份同步）：新增技能链 `camera_preview`
  （`mode=while`, `interval=200`, `list=["haikang_camera","camera_test"]`），演示"采集 → 显示"串联；
  其中 `list` 用的是**技能名简写**，保存/加载时由后端自动补全成完整对象。
- `skills/haikang_camera/`（仓库 + 发布目录同步）：`main.cpp` 收敛到最小实现 ——
  `init` 建相机并启动（失败即抛异常）、`run` 取一帧写进公共内存 `image`（空帧直接返回）、`end` 停相机；
  早先的"存盘快照"功能已移除，**它只负责把画面写进公共内存**，显示交给 `camera_test`。
- `www/index.html`：**不加图像面板**。图像预览面板曾经加过一版（读 `camera_test.preview_jpeg` + `PREVIEW_KEY`），
  按需求已完整回退，仓库与发布两份都回到"技能链 / 公共内存 / 运行状态"版本（两份哈希一致）；
  图像只看技能自己弹出的窗口。

**验证**

- `-Wall -Wextra` 全量语法/类型校验通过（4 个 TU × 仓库/发布两棵树，均 exit 0）；
- 另写临时测试程序做窗口实机验证（Windows，合成 640×360 渐变帧 ×10 投递到 `Displayer`）：
  `窗口就绪=1` / `已显示帧数=10` / `stop 之后 windowRunning=0`，进程退出码 0，窗口正常关闭（临时程序与构建产物已删）。

### 0.2 `release/v1.0.0/linux/README.md`（新增：发布版技能开发手册）

面向**技能作者**的独立手册（约 1200 行），放在发布目录里，跟着发布包一起走。六章：

1. **文件结构** —— 发布目录树 + 每条硬性事实（必须在发布目录内启动、`.so` 不热更新、`include/` 是给技能编译用的）
2. **两个动态库的导出函数怎么用** —— 命名空间分工；技能 ABI 约定（`SKILL_API` / 三个入口 / 导出名自查）；
   `Memory` 全 API + **完整类型实例化清单** + 语义与坑（`insert` 重名抛异常、`insertOrAssign` = erase+insert、
   `get` 类型不匹配的 `why` 文案、`erasePrefix("")` 会清空全部）；`logger()`/`Time`/`yaml`；
   `Skill`/`SkillList`/`RunState` 的 JSON 约定；框架内部类；`sc::net` 的 `HttpHand`/`Http`/`HttpServer` 与 11 条路由；
   链接期常见错误
3. **推荐的 skills 结构** —— 目录布局 + `config.json` / `main.hpp` / `main.cpp` / `CMakeLists.txt` 模板 + 构建命令 + 命名对齐约定
4. **配置文件内容** —— `configs/config.yaml` 逐项（含"键缺失时"的代码默认值）、`skills.json`、`skillsList.json`（含一轮执行流程）、
   技能自己的 `config.json` / `config.yaml`、编码要求
5. **skill 设计问题怎么处理** —— `run()` 不许死循环、`init/run/end` 生命周期与幂等、日志包含关系、
   **dladdr 定位配置路径**、异常该抛/不该抛、公共内存约定（含卸载前 `erasePrefix` 的原因）、
   线程与 GUI、编译部署、自检清单
6. **两个示例技能** —— `haikang_camera`（只写内存）与 `camera_test`（只读内存 + 自己起显示线程），
   含真实运行日志片段与串联演示

顺带修正的**文档不一致**：`doces/README.md` 里「`yaml.hpp` 失败会 `exit(1)`」已过时 ——
实际实现早已改成抛 `std::runtime_error`（技能里调用它不会再干掉宿主进程），已同步更正。

### 1. `doces/README.md`（项目总说明）

面向使用者的总入口文档：项目简介与核心概念、完整文件结构、构建与运行（含 `config.yaml` 说明）、
**HTTP API 文档**（11 个接口的请求/响应示例 + `Skill` / `SkillList` / `RunState` 数据结构 + `interval` 语义表）、
**C++ 函数功能文档**（`Memory` / `Time` / `FindSkills` / `Assign` / `LoadSkills` / `RunSkillsLists` / `net::Http` / `HttpServer` 及全局状态）、
**SKILL 写作模板与约定**（目录、`config.json`、`CMakeLists.txt`、`main.hpp`/`main.cpp` 模板 + 10 条硬性约定 + 常见坑速查 + 调试建议）、
全局编码约定、FAQ、已知限制。变更记录仍以本文件（`change.md`）为准。

### 1. `include/tools/runskillslists.hpp` / `tools/runskillslists.cpp`

技能链线程管理器，命名空间 `sc::tools`。

| 成员 | 说明 |
| --- | --- |
| `RunSkillsLists(Memory* memory)` | 构造，注入公共内存 |
| `bool start(const SkillList& list)` | 启动一条链（重复启动会被拒绝，须先 `stop`） |
| `bool stop(const std::string& name)` | 停止一条链并 join 线程 |
| `std::size_t stopAll()` | 停止全部，返回条数 |
| `bool remove(const std::string& name)` | 停止并移除运行记录 |
| `bool running(name)` / `std::size_t runningCount()` | 运行状态查询 |
| `std::vector<RunState> states()` | 状态快照（名称/模式/运行中/轮次/当前技能/错误/时间戳） |
| `nlohmann::json statusJson()` | 状态转 JSON，给 `/api/GetRunStatus` 用 |

**线程模型与生命周期（重要）**

1. **load 一次**：技能第一次被用到时加载 DLL 并调用 `init()`，以 `skill.url` 为键全局共享，**同一个技能只 init 一次**。
   循环链不会每轮重新 init —— 否则技能的 `insert("name", ...)` 会抛「名称已存在」把线程打死。
2. **run 每轮一次**：每轮对链内每个技能按顺序调用 `run()`。
3. **end 一次**：只在技能被真正卸载（`~RunSkillsLists` → `releaseAll()`）时调用 `end()`。
4. **停止是软停止**：`stop()` 只置停止标志，线程跑完**当前**技能后自然退出；不做强行杀线程。
5. **锁外 join**：线程收尾时还要拿锁更新状态，所以 `start/stop` 的 `join()` 都在锁外执行，避免死锁。
6. 每条链的 `interval` 只作用于 `while` 模式（轮间隔，默认 1000ms，避免空转吃满 CPU）。

---

## 三、修改文件

### 1. `include/tools/type.hpp`

- 新增 `SkillList` 的 **JSON 双向序列化**（全局命名空间 `to_json` / `from_json`）：
  - 输出键名用 `list`（与 `configs/skillsList.json` 一致）：
    `{"name","mode","interval","list":[skill 对象...]}`
  - `list` 数组元素**两种写法都认**：技能名字符串（`"haikang_camera"`）或完整 skill 对象。
  - 解析 skill 对象时**字段可缺省**（只给 `{"name":"xxx"}` 也认），缺的字段由 `Assign::resolve()` 用 `FindSkills` 补全。
    注意不能用宏生成的 `Skill::from_json`，它要求 5 个字段齐全，前端只提交 name 时会抛 `key 'description' not found`。
- `SkillList` 新增 `interval` 字段（`while` 模式的轮间隔毫秒，默认 1000）。
- 明确 **mode 只有两个值**：
  - `once` —— 只跑一次，按顺序跑完一遍线程就结束；
  - `while` —— 一直循环，直到 `stop()` 或全局 `STATUS=false`。
  `skillListModeValid()` 只认这两个值，非法值按 `once` 处理并告警。

### 2. `include/tools/assign.hpp` / `tools/assign.cpp`

技能链管理器完善 + 与 JSON 嵌合：

- **修 bug**：原来读配置时只取了 `mode` 和 `list`，**从没给 `s.name` 赋值**，导致所有链名字都是空串、重名检查失效。现在正确解析 `name`。
- **修 bug**：`list` 支持字符串和对象两种写法（旧实现只按字符串取，遇到对象直接抛异常，整条链加载失败）。
- **`resolve()`**：以 `FindSkills` 扫描到的信息为准补全技能（避免前端提交过期 url）；技能不存在且没带 url 时报错。
- **JSON 接口**：
  - `json getListsJson() const` —— 链集合转 JSON 数组；
  - `bool setListsJson(const json& arr)` —— JSON 数组覆盖保存（先落盘再重载，失败则原链不变）；
  - `getLists()` / `hasList()` / `getList()` / `upsertList()` / `removeList()`；
  - `get_all_lists_json()` 保留兼容旧调用。
- **公共内存的 JSON 赋值（静态方法，http_server 直接用）**：
  - `assignMemory(mem, items, report)` —— `{"键":值}` 写进内存，已存在的键走 `insertOrAssign` **覆盖**，不再抛重名异常；返回写入数 + 失败明细；
  - `readMemoryValues(mem, names)` —— 读出「名字/类型/大小/值/是否存在」，内部做 `std::any → json` 的类型分派（标量、字符串、数值数组、字符串数组、`map<string,*>`）；
  - `eraseMemory(mem, names)` —— 按键名批量删除。
- `LOAD_STATUS` 语义固定为：加载过程中 `false`，全部成功才 `true`，中途失败保持 `false`。

### 3. `include/tools/loadskills.hpp` / `tools/loadskills.cpp`

把「加载/运行/收尾」拆开，供线程管理器反复调用（`run()` 仍然等于 `load+runOnce+end`，旧调用方不受影响）：

| 方法 | 说明 |
| --- | --- |
| `load(Memory*)` | 解析 `init/run/end` 入口符号并调用一次 `init()`（重复调用安全） |
| `runOnce()` | 只执行 `run()`，未 `load` 时报错 |
| `end()` | 只执行 `end()`（技能没导出 end 时什么都不做） |
| `run(Memory*)` | = `load + runOnce + end`（兼容旧用法） |
| `loaded()` / `info()` / `path()` | 状态与信息查询 |

`resolveSymbols()` 把入口解析单独抽出来，仍保留「导出名被 C++ 修饰时按候选名回退并告警」的逻辑。

### 4. `include/net/http_server.hpp` / `net/http_server.cpp`

- 构造函数增加 `Assign&`、`RunSkillsLists&` 两个依赖。
- 所有业务接口统一 **JSON 进出**，新增 `replyJson()` / `parseBody()` 两个内部工具。
- 注册了全部 11 个路由（旧代码里 `apiUpdateSkillChain` 只声明没注册，且路由名拼成了 `/api/apiGetSkillChain`）。
- 见下文「四、接口一览」。
- **（1.2.0）启动后自动打开控制台网页**：新增 `consoleUrl()` / `openConsole()` / `autoOpenConsole()`，
  `start()` 在监听就绪后按 `config.yaml` 的 `http_auto_open`（缺省 `true`）调用 `ShellExecuteA` 拉起默认浏览器；
  设为 `false` 时只在日志里打印控制台地址。地址用 `127.0.0.1` 而不是 `localhost`，避免部分系统先解析到只监听了 IPv4 的 `::1`。
  `Http::port()` 是配套新增的端口读取接口（不再硬编码 8080）。

### 5. `net/http.cpp`

- **修 bug（关键）**：原来一次 `recv` 就把请求读完，JSON body 只要被 TCP 拆包就丢（实测 `SetMemory` 写入 0 项、`UpdateSkillChain` 收到空数组）。
  现在先收到请求头结束符 `\r\n\r\n`，再按 **`Content-Length` 收满 body**，并加了 2MB 请求体上限。
- API 路由匹配前先剥掉 `?查询串` 和 `#片段`，方便前端带时间戳防缓存。

### 6. `projects/main.cpp`

- 修正原来的接线错误（`tools::Assign(...)` 建了个临时对象立刻析构、`HttpServer` 只传了 3 个参数 → 当前代码树编译不过）。
- 技能不再在启动时被直接跑完，改为交给 `RunSkillsLists`，由网页/接口按技能链启停。
- 调用 `Memory::registerTypeAll()` 注册内置类型。
- 注册 `SIGINT/SIGTERM`，退出顺序：`STATUS=false` → `stopAll()` → `httpServer.stop()`。

### 7. `include/tools/tools.hpp` / `tools/CMakeLists.txt`

- 头文件新增 `runskills.hpp`、`runskillslists.hpp`。
- 源文件加入 **`assign.cpp`（原来漏了，根本没参与编译）** 和 `runskillslists.cpp`。

### 8. `www/index.html`（技能链改为链式图形 UI）

控制台从「只读监控」扩展为「可操作」，技能链部分采用**图形化链式（节点 + 连接线）**表达。

- **内存实时监控**：新增「值」列（`GetMemoryValues`，失败自动降级 `GetMemory`），500ms 轮询。
- **技能列表**：名称/版本/描述/URL，保留「更新技能」。
- **技能链（链式图形 UI）**：每条链渲染成一条横向链带：

  ```
  [起点端子] ─→ [① haikang_camera v0.0.1] ─→ [② 技能名] ─→ [终点端子]
  ```

  - 节点 `.chain-node`：序号圆标 + 技能名大字 + 版本号，悬停显示描述；
  - 连接线 `.chain-link`：渐变细线 + `::after` 三角 border 画的箭头（纯 CSS，无 SVG、无第三方库）；
  - 端子 `.chain-term.start / .end`：起点圆环、终点方块箭头；
  - **运行高亮**：`current` 命中的节点加 `.active` 呼吸发光；整条链运行时链带描边发光、连接线走 `linkFlow` 流动动画（gradient + background-position，性能友好）；
  - 链头：链名、模式徽标（`once · 只跑一次` / `while · 一直循环` 双配色）、轮间隔、运行状态徽标（运行中/已停止/启动中）；下一行是 技能数 · 已跑轮次 · 当前技能 · 启动/更新时间，错误信息红色单独一行；
  - 每条链四个按钮：启动 / 停止 / 编辑 / 删除；顶部「全部启动 / 全部停止」；空链渲染虚线占位节点「空链 · 请编辑添加技能」；
  - 状态刷新是**增量更新**（只改 class 与 textContent），轮询不重建 DOM；窄屏链带横向滚动，不撑破布局。
- **链编辑器（可视化编排）**：技能以可拖拽节点排成一排，节点之间自动补连接线，增删与排序实时更新；空链时链尾显示虚线落点提示。
  - **HTML5 拖拽排序**：`dragover` 先 `preventDefault()`，按节点中线判定插入位置（`.drop-before` / `.drop-after` 竖条高亮），拖到 `.edit-drop-tail` 落链尾，`dragend` 复位；
  - 兜底保留 `↑ / ↓ / ✕` 小按钮（触屏浏览器可用）；
  - 拖拽期间 `renderChains()` 直接返回，绝不重建 DOM 打断拖拽；编辑器只由编辑操作重绘，轮询仅同步节点高亮；
  - 字段：链名、模式（下拉**只有** `once` / `while`）、轮间隔（仅 while 生效）；校验链名非空与重名；保存时整体提交完整数组，技能统一用 `pickFullSkill()` 从 `/api/GetSkills` 取完整对象。
- **内存写入/操作（新增）**：键名 + JSON 值写入，字符串/数字/布尔/数组快捷按钮，按键名删除、按前缀删除，并显示写入结果明细。
- **内存写入/操作（新增）**：键名 + JSON 值写入，字符串/数字/布尔/数组快捷按钮，按键名删除、按前缀删除，并显示写入结果明细。

### 9. `configs/skillsList.json`

格式规范化（语义不变）：每条链补上 `interval` 字段，`list` 仍是完整 skill 对象数组。
配置项 `skillsList_file` 在 `configs/config.yaml` 中已存在（`configs/skillsList.json`）。

---

## 四、接口一览

所有接口返回 JSON，带 `status` 字段（`ok` / `error`），写操作一律用 **POST + JSON body**（后端不解析查询串）。

| 方法 | 路径 | 请求体 | 响应（要点） |
| --- | --- | --- | --- |
| GET | `/api/GetMemory` | — | `[{"name","type","size"}, ...]` |
| GET | `/api/GetMemoryValues` | 可选 `{"names":["a"]}` | `[{"name","type","size","exists","value"}, ...]` |
| POST | `/api/SetMemory` | `{"items":{"键":值}}` 或直接 `{"键":值}` | `{"status","written","failed":[{"name","message"}]}` |
| POST | `/api/DelMemory` | `{"names":["k1"]}` 或 `{"prefix":"skill."}` | `{"status","erased"}` |
| GET | `/api/GetSkills` | — | `[Skill, ...]` |
| GET | `/api/UpdateSkills` | — | `{"status"}`（运行中会失败，需先停链） |
| GET | `/api/GetSkillChain` | — | `{"status","loaded","skills_lists":[SkillList, ...]}` |
| POST | `/api/UpdateSkillChain` | `[SkillList, ...]` 或 `{"skills_lists":[...]}` | `{"status","skills_lists"}` |
| POST | `/api/StartSkillChain` | `{"name":"test"}` 或 `{"all":true}` | `{"status","started":[],"failed":[],"running":n}` |
| POST | `/api/StopSkillChain` | `{"name":"test"}` 或 `{"all":true}` | `{"status","stopped":[],"running":n}` |
| GET | `/api/GetRunStatus` | — | `{"status","running":n,"lists":[RunState, ...]}` |

**`SkillList` 数据格式**

```json
{
  "name": "test",
  "mode": "while",
  "interval": 1000,
  "list": [ { "name": "haikang_camera", "description": "...", "version": "0.0.1",
              "url": "skills\\haikang_camera\\build\\bin\\libHAIKANG_CAMERA.dll", "author": ["x1"] } ]
}
```

`list` 元素也可以简写成字符串 `"haikang_camera"`，保存时会自动补全成完整对象。

**`interval`（轮间隔）语义**

| 值 | 含义 |
| --- | --- |
| `> 0` | `while` 模式每轮之间等待这么多毫秒 |
| `= 0` | 不等待、全速循环（runner 每轮只 `yield()` 让出时间片，CPU 占用会很高） |
| `< 0` | 非法，回落默认 `1000` 并写告警日志 |
| 字段缺省 | 默认 `1000` |

`once` 模式跑完一遍即结束，不受 `interval` 影响。
注意：**正在运行的链不会热更新**——改完模式 / 轮间隔要「停止」再「启动」才会生效（保存时页面会给出提示）。

**`RunState` 数据格式**

```json
{ "name": "test", "mode": "while", "running": true, "starting": false, "loop": 3,
  "current": "haikang_camera", "skill_count": 1, "error": "",
  "start_time": "2026-09-17 14:23:36", "update_time": "2026-09-17 14:23:38" }
```

---

## 五、修复的问题清单

| # | 位置 | 问题 | 处理 |
| --- | --- | --- | --- |
| 1 | `tools/assign.cpp` | 读配置时**从未赋值 `s.name`**，所有链名都是空串，重名检查形同虚设 | 正确解析 `name`，并在解析失败时告警返回 |
| 2 | `tools/assign.cpp` | `skillsList.json` 里 `list` 是对象数组，旧代码按字符串数组解析 → 抛异常、整条链加载失败 | `from_json` 同时支持字符串与对象 |
| 3 | `include/tools/type.hpp` | `Skill` 的宏生成 `from_json` 要求 5 字段齐全，前端只给 `{"name":"x"}` 时抛 `key 'description' not found`（实测 400） | skill 对象改为容错解析，缺省字段交给 `FindSkills` 补全 |
| 4 | `net/http.cpp` | 只 `recv` 一次，JSON body 被 TCP 拆包就丢（实测 `SetMemory` 写入 0 项） | 按 `Content-Length` 收满 body，并加请求体上限 |
| 5 | `net/http_server.cpp` | `apiUpdateSkillChain` 声明了没注册；路由名拼错为 `/api/apiGetSkillChain` | 11 个路由全部正确注册，删除错误别名 |
| 6 | `projects/main.cpp` | `Assign` 建成临时对象立即析构；`HttpServer` 只传 3 个参数 → 编译不过 | 修正接线，`Assign`/`RunSkillsLists` 生命周期覆盖整个主循环 |
| 7 | `tools/runskills.cpp`（旧） | 每轮 `LoadSkills::run` 都会重新 `init`，循环第二轮因内存重名抛异常 | 新管理器改为「load 一次 + runOnce 多轮」，`LoadSkills` 拆出三个入口 |
| 8 | `tools/assign.cpp` | `LOAD_STATUS` 中途失败后不再恢复，语义不明确 | 固定为：加载中 `false`、全部成功 `true`、失败保持 `false` |
| 9 | `tools/assign.cpp` | **轮间隔调不动**：`if (sl.interval <= 0) sl.interval = 1000;` 把用户填的 `0` 悄悄改成 `1000`，界面上表现为「轮间隔怎么调都还是 1000 ms」 | 只有**负数**才回落 1000；`0` 表示不等待、全速循环；runner 里 `0` 走 `yield()` 不再按 1000 处理 |
| 10 | `include/tools/type.hpp` | 手改配置把数字写成字符串（`"interval": "300"`）或 `"version": 1` 会抛 json `type_error`，实测 `UpdateSkillChain` 直接 400 | 新增 `jsonStr` / `jsonInt` / `jsonValStr` 宽松解析，数字与字符串互认 |
| 11 | `tools/assign.cpp` | 一条链配置写错（缺 name / 重名 / 技能不存在）会让 `update()` 直接 `return false`，**所有链一起丢** | 改为单条 `try/catch` 跳过并告警，其余链正常加载 |
| 12 | `tools/findskills.cpp` + `net/http_server.cpp` | **「更新技能」按钮永远 400**：`FindSkills::update()` 里 `if (STATUS) return false;`，而主程序启动就把 `STATUS` 置为 `true`，运行期间这个接口不可能成功 | `update(bool force = false)`；HTTP 层在 `runningCount()==0` 时用 `force=true` 强制重扫并顺带 `assign_.update()`，有链在跑时返回 400 + 明确提示（实测：空闲 200 / 运行中 400 且带 message） |
| 13 | 发布目录（打包） | 运行 `release/v1.0.0/win/skillcontorl.exe` 时技能加载失败：`LoadLibrary ... 错误: 126`。根因是发布目录里**只拷了 `libHAIKANG_CAMERA.dll.a`（导入库），真正的 `libHAIKANG_CAMERA.dll` 没拷**，`skills\haikang_camera\` 下没有可加载的模块 | 新增 `scripts/package.ps1` 打包脚本：正确复制技能 DLL（并把 `config.json` 的 `url` 重写成同级路径）、复制 exe/两个库/configs/www、建 `logs/`，缺件时列告警并非零退出；已用它重建 `release/v1.0.0/win` 并实跑验证 |
| 14 | `www/index.html` | **界面提示与服务端不一致**：`apiGet/apiPost` 在非 2xx 时直接 `throw new Error("HTTP 400")`，把服务端 JSON 里的 `message` 丢了；更新技能按钮还残留写死的旧文案（"请关闭全部 skill 运行重试"），用户看到的不是真实原因 | `apiGet/apiPost` 改为先读 body，非 2xx 时抛 `Error(data.message \|\| "HTTP <status>")` 并挂上 `err.status` / `err.data`；「更新技能」失败时显示服务端真实原因，并弹确认框「有 N 条链在运行，是否全部停止后立即重扫？」——确认即自动 `StopSkillChain {all:true}` 后重试 |
| 15 | `include/net/http.hpp` | 响应没有缓存头，浏览器会复用旧的 `index.html`（以及旧 JS），出现"代码改了但界面还是老样子"，并会让人误以为提示文案是新的 | `HttpHand::toString()` 默认补 `Cache-Control: no-store, no-cache, must-revalidate` / `Pragma: no-cache` / `Expires: 0`（handler 自己设了同名头则以 handler 为准），静态页与接口一并生效 |
| 16 | `tools/logger.cpp` + `include/tools/logger.hpp` | **控制台中文乱码**（`鏇存柊skills鏁版嵁`）：程序（spdlog 日志 + 技能里的 `std::cout`）输出的是 UTF-8 字节，而 Windows 控制台默认按 GBK(936) 解释 | 新增 `sc::tools::setupConsoleUtf8()`：`GetConsoleOutputCP()` 为 0（无控制台/重定向）时跳过，否则 `SetConsoleOutputCP(CP_UTF8)` + `SetConsoleCP(CP_UTF8)`，返回旧代码页；`set_logger()` 在创建日志前调用它，并写一行 `[logger] 控制台输出代码页 936 -> 65001(UTF-8)…` 便于确认。实测：`chcp 936` 起手 → 程序启动后控制台变成 65001，日志中文全部正常 |
| 17 | `net/http.cpp` + `net/http_server.cpp` | **超过 2MB 的请求会回几 MB 的响应体**：读满上限后把截断的 body 交给 nlohmann 解析，而解析错误信息里带 `last read: '<出错附近的原文>'`，被原样拼进了响应（实测 3MB 请求 → 3MB 响应），用户看到的还是一句莫名其妙的 JSON 解析错误 | ① `http.cpp` 记录 `Content-Length`，超限直接回 **413 Payload Too Large** + 明确说明（`HttpHand` 补 413 状态文本）；② `http_server.cpp::parseBody()` 把异常文本清换行并截断到 200 字符。实测：3MB → `413`（响应 280 字节）；非法 JSON → 响应 186 字节；1.9MB → 正常写入 |
| 18 | `tools/runskillslists.cpp` + `doces/README.md` | **技能在 `run()` 里写死循环**（或长阻塞）时没有任何提示：表现为点了「停止」没反应、`POST /api/StopSkillChain` 挂住、退出程序卡死、链里后续技能永远轮不到、`loop`/`current` 状态失真，而日志里查不到原因 | ① 把「`run()` 不允许有死循环」写成硬性约定第 6 条，并新增 **6.7 节**：错误/正确写法对比、上表这些后果、以及"循环交给 `mode=while` + `interval`"的替代做法；② `RunSkillsLists::runOneLoop()` 给每次 `run()` 计时，单次 ≥ 3 秒打 warn（提示疑似死循环/长阻塞）。实测：临时技能故意 `sleep(4s)` → 日志出现 `技能 _tmp_slow 单次 run() 耗时 4009 ms，疑似在 run() 里写了死循环或长阻塞`。<br>注：只告警不打断 —— `std::thread` 没有超时打断能力，死循环只能靠约定避免 |
| 19 | `net/http.cpp` | **Linux 下"程序结束但 8080 一直被占用"**：`Http::stop()` 里 `close(listenFd_)` 之后就 `join()` accept 线程，而 POSIX 下"一个线程正阻塞在 `accept()` 时，另一个线程 `close()` 同一个 fd"**不会唤醒它** → `accept()` 不返回 → 线程不退出 → `join()` 永久阻塞 → 主线程卡在 `httpServer.stop()`，永远走不到 `[main] exit at`，监听 socket 依然绑着 → 下次启动报 `bind() failed on port 8080`。<br>证据：Linux 侧 13 份日志**没有一份**有 `[main] exit at` / `[Http] stopped` / `accept loop exited`，而 `bind() failed` 出现 4 次。 | ① **accept 循环改用 `select()` 带 200ms 超时**（每 200ms 回来检查一次停止标志，不再依赖"close 能否唤醒 accept"这种平台语义）；② `stop()` 里先 `::shutdown(SHUT_RDWR)` 再 `close()`（POSIX 下 shutdown 才会让阻塞中的 accept 立刻返回），双保险；③ `~Http()` 等客户端线程最多 5 秒，超时打 warn 后继续退出（避免个别 API 卡死拖住整个进程）；④ `stop()` 前后加日志 `[Http] 正在停止` → `[Http] 等待 accept 线程退出...` → `[Http] stopped`，以后再卡能一眼定位。<br>实测（Windows 全链路：临时 exe 打了"5 秒自动退出"补丁）：`[Http] 正在停止: 关闭监听端口 8080` → `等待 accept 线程退出...` → `accept loop exited` → `[Http] stopped` → `[main] exit at`，进程自行退出、端口释放、stderr 无异常（atexit 守卫确认是正常退出） |

---

## 六、构建与验证

- `tools/CMakeLists.txt` 已加入新源文件，正常用 CMake 构建即可（VS Code 的 CMake Tools 会自动重新配置）。
  本次因环境限制用 g++ 手工编译并重链了 `libtools.dll`、`libnet.dll`、`skillcontorl.exe` 进行验证，编译参数与 `flags.make` 一致。
- 实测结果（程序运行中逐个调用接口）：
  1. `POST /api/SetMemory` 写入 4 项 → `{"written":4,"failed":[]}`；
  2. `GET /api/GetMemoryValues` 正确回读类型与值：`vector<long long> [1,2,3]`、`bool true`、`unsigned long long 42`、`string "hello"`；
  3. `POST /api/StartSkillChain {"name":"test"}` → 启动成功，`GetRunStatus` 显示 `loop` 递增、`running=true`，技能写入的 `name=HAIKANG` 出现在公共内存里；
  4. `POST /api/StopSkillChain` → 线程正常收尾退出，`running=0`，轮次停在停止那一刻；
  5. `POST /api/UpdateSkillChain` 提交两条链（一条 `while`、一条 `once`，后者只写技能名）→ 保存成功且自动补全；
  6. `{"all":true}` 全部启动：`while` 链持续循环（loop=3），`once` 链跑完 1 轮后**自己结束**（`running=false`，loop=1）；
  7. `POST /api/DelMemory` 按键名与按前缀删除均正常。
- **轮间隔（interval）补充验证**（修完问题 9/10/11 之后）：
  - `interval = 0` → 存回仍是 `0`（修复前会被改成 1000），实跑 2 秒 `loop = 18645`（全速）；
  - `interval = 500` → 实跑 2 秒 `loop = 4`（对照正确）；
  - `interval = -5` → 回落 `1000` 并告警；
  - `interval = "300"`（字符串）→ 正常存成 `300`（修复前 400）；
  - `{"version": 1}`（数字代替字符串）→ 正常接受（修复前抛 `key 'description' not found` / type_error）；
  - `interval = 50 / 250`（非 100 步进）→ 原样保存，不再受前端 `step="100"` 影响（已改为 `step="any"`）。
- **前端（`www/index.html`）验证**：`node --check` 语法检查通过；HTML 41 个 id 全部唯一、39 个 `getElementById` 引用全部存在；11 个接口路径拼写逐一核对一致；`chainMode` 仅 `once` / `while` 两个选项。
  另外写了一个**带 DOM 垫片的 Node 冒烟测试**（用真实后端数据驱动页面脚本，跑完即删），28 项断言全部通过，覆盖：
  链带 HTML 生成（节点 / 序号 / `data-skill` / 连接线 / 起止端子 / 模式徽标 / 轮间隔 / 四个按钮 / `data-role` 状态位）、空链占位节点、**XSS 转义**（`<img src=x onerror=1>` 被转成 `&lt;`）、编辑节点渲染与拖拽属性、编辑区端子与拖尾落点，以及**拖拽重排的索引运算**（首→尾、尾→首、中间→尾、相邻下移四种顺序结果逐一校验）、运行状态轮询无异常。

---

## 七、已知限制与后续建议

1. **HTTP 层不区分方法**：handler 只拿到 body，GET/POST 走同一个处理函数；约定「读用 GET、写用 POST」。
2. **停止是软停止**：技能 `run()` 正在执行时必须等它返回；长耗时技能会让停止有延迟（不做强行终止线程）。
3. **end() 只在卸载时调用一次**：不是每轮、也不是每次 `stop()` 调用；如果技能需要在每次停止时收尾，需要在技能里自己实现。
4. **技能不可重复 init**：`Memory::insert` 重名会抛异常，所以技能内写公共内存建议用 `insertOrAssign`，或者在 `init` 里一次写完。
5. **`tools/runskills.cpp`（旧的 `RunSkills`）保留未删除**：功能已被 `RunSkillsLists` 取代，后续可以清理。
6. **`/api/UpdateSkills` 在 `STATUS=true` 时会失败**：这是 `FindSkills::update()` 的既有约束（运行中不重新扫描技能），接口会返回提示信息，需先停掉全部技能链。
7. **`ApiHand` 状态文本**：`HttpHand::toString()` 只覆盖 200/201/204/400/404/500，其它状态码直接输出数字，前端按状态码判断即可。
8. **链编辑器拖拽不支持触屏**：HTML5 Drag & Drop 在触屏浏览器不触发，触屏请用节点上的 `↑ / ↓ / ✕` 按钮（已作为兜底保留）。
9. **节点内小按钮与拖拽的轻微冲突**：`.edit-node` 整体 `draggable="true"`，个别浏览器下从按钮上按下并拖动可能被判为节点拖拽；点击功能不受影响。
10. **浏览器实机视觉未验证**：本环境无法启动无头浏览器（Edge 在远程会话下被策略拦截），链式 UI 的动画、箭头、拖拽视觉反馈建议刷新页面人工确认一次；渲染逻辑已由上面的冒烟测试覆盖。
