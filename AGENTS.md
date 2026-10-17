# AGENTS.md

面向在本仓库工作的所有编码 agent（DSH、Claude Code、Cursor 等），不绑定具体工具。

## 第一原则：以代码为准

仓库里的文字材料有滞后，与源码冲突时一律以源码为准：

- `readme.md` 的「使用方法」仍写着 `--optimize-strict`、`--mem-size`、`--out-c`、`--out-legacy-pe`。这些选项名都已不在选项表里，传入不会报错，而是被静默忽略。详见下文「命令行」。
- `.claude/CLAUDE.md` 描述的是重构前的结构（`wmain` + `wchar_t*`、`NeoParser` 类、`--out-*` 系列开关），均已过时。`.claude/` 在 `.gitignore` 里，是本机遗留。
- `local_docs/` 同样被 `.gitignore`，是本机的优化构思草稿（见文末），不是权威文档。

改完涉及构建、命令行、架构的改动，顺手同步本文件。

## 项目是什么

C++23 写的 Brainfuck 优化编译器/解释器，产物是 **x86-64、64 位 Windows 7 以上** 的独立 PE。

编译器自己手写 x86-64 机器码编码，自己拼 PE 头、段表、导入表，运行时不需要任何外部汇编器/链接器/编译器。只支持 Windows 宿主，无第三方依赖。

需要 MSVC 或较新的 Clang（已测 Clang 22.1.3）加 Windows SDK。

## 构建

```powershell
cmake -S . -B cmake-build-release-clang -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release-clang
```

产物：`cmake-build-release-clang\bf-compiler.exe`。

- 现有构建树：`cmake-build-debug-clang`、`cmake-build-release-clang`（Ninja + Clang，都被 gitignore，`cmake-*` 已排除）。
- CMake 全局定义：`_HAS_EXCEPTIONS=0`、`DEFAULT_MEM_SIZE=80000000`、按宿主平台定义 `HOST_OS_WIN` / `HOST_OS_MAC` / `HOST_OS_LINUX`。`INTERPRETER_CHECK` 默认被注释掉。
- Release 额外开启：Clang 为 `-fno-stack-protector -flto`，MSVC 为 `/GS- /GL /Gw` + `/LTCG:INCREMENTAL`。两边都关 RTTI 和异常。
- 只有一个 target `bf-compiler`。`loop_explorer.cpp` **不在** CMake 里，而且是未跟踪的本地草稿，不要以为它参与构建。

## 命令行

选项表在 `main.cpp:39-110`（`OPTIONS`），解析逻辑在 `option.cpp`。

| 选项 | 参数 | 作用 |
| --- | --- | --- |
| `-O0` | | 关闭优化 |
| `-O1` | | 激进优化 |
| `-Ostrict` | | 保守优化，不产生保护空间 |
| `-o` / `-o=<file>` | file | 输出文件；不传 `-o` 则直接解释执行 IR |
| `--out-format=<value>` | `pe` \| `c` | 输出 PE 或 C 源文件 |
| `-mmem-size=<value>` | 1..2147483647 | 运行时内存大小 |
| `--print-info` | | 打印 IR/ASM 计数和各优化遍的结果 |
| `-help` / `--help`、`-v` / `--version` | | 帮助、版本 |
| `-falign-loops=<N>` | 0 \| 16 | **只做参数校验，不产生任何效果** |
| `-fclj` | | **无任何效果** |

### 必须知道的三个坑

**1. 优化默认开的是 O1。** `getOptimizationLevel()`（`main.cpp:135-149`）的优先级是 `-O1` > `-Ostrict` > `-O0`，三个都没给时返回 `O1`。要别的级别必须显式传 `-O0` / `-Ostrict`。另外 `-O0` 下 6.bf / 7.bf 这类 8MB 源文件跑到分钟级是正常的，不是卡死。

**2. 未知选项被静默忽略。** `parseOptions` 匹配不到就跳过，不报错也不提示。所以旧文档里的 `--out-c` 会安静地给你一个 PE 文件（实测输出以 `4D 5A` 开头），`--mem-size`、`--optimize-strict`、`--out-legacy-pe` 同理只是无效。写脚本时选项名写错了不会有人告诉你。

**3. `-falign-loops` 和 `-fclj` 是空壳。** 它们在选项表里注册、`-falign-loops` 还带校验器，但 `main.cpp` 从头到尾没有读过 `OPT_ALIGN_LOOPS` / `OPT_CLJ`。实测加不加这两个开关产物字节相同。

退出码：`0` 成功；`1` 输入文件打不开；`2` 输出文件打不开；`3` 起为解析错误 `PARSE_ERR + argv索引`，`10000` 起为参数校验错误 `ARG_ERR + argv索引`。例：`-falign-loops=7` 位于 `argv[2]` 时退出码是 `5`。

## 验证改动

### 回归测试脚本（`test/run_tests.py`）

纯标准库，用 uv 跑（本机的 `python.exe` 是 WindowsApps 占位程序，直接调用会失败）：

```powershell
uv run --no-project python test/run_tests.py
```

三层检查：

1. **二进制比对** —— 每个用例在每组配置下编译两次，比较两次产物 SHA256（捕捉非确定性），再和 `test/baseline.txt` 比较（捕捉产物变化）。纯重构不应该改变产物任何字节。
2. **输出比对** —— 编译产物喂入 `<name>_in_N.txt`，与 `<name>_out_N.txt` 逐字节比较。
3. **计时** —— 打印各配置编译耗时的中位数、合计和最慢 5 项。

| 参数 | 作用 |
| --- | --- |
| `--configs O0,O1,strict` | 参与检查的配置，默认三组全跑 |
| `--filter "8*"` | 按用例名过滤，快速迭代 |
| `--cases-dir PATH` | 用例目录，默认 `test/cases` |
| `--large-dir PATH` | 大压力用例目录，默认 `test/cases_large`，配合 `--include-large` |
| `--jobs N` | 并行度，默认 CPU 数 |
| `--timeout N` | 单任务超时秒数，默认 30；源文件超过 1MB 的用例自动放宽到 180 |
| `--no-binary` | 只做输出比对 |
| `--record -Force` | 用 `--reference-compiler`（默认 `test/bf-compiler-stable.exe`）重录参考输出 |
| `--record-baseline` | 用本轮结果重写 `test/baseline.txt` |
| `--list` | 列出用例及其标记 |

中间产物默认写 `test/out/`（已 gitignore），`--workdir` 可改。

### 用例数据现状

参考输出已全部由 `test/bf-compiler-stable.exe` 重录、逐字节保存。用 `--record` 重录前先确认不会把 bug 一起录进去。

唯一的例外是 `dbf2c` 的两个参考：它们按真值修正过，不是 stable 的输出（stable 在 `dbf2c_in_1` 上就是错的）。**别用 `--record` 覆盖它们，否则会把 bug 重新录回基准。** 详见下文「当前已知的失败」。

**关于换行**：编译产物经 ucrt `putchar` 写 stdout，处于文本模式，BF 程序输出的 `\n` 会被翻成 `\r\n`；解释执行路径（`std::cout`）同样如此。原参考里有 30 组是纯 LF，那是抓取时被转掉的，不是程序真实输出。用一份独立写的朴素 BF 解释器（8 位单元回绕、纸带全零、模拟文本模式输出 `\n` 写 `\r\n`）复核：**25 个可跑完的小用例里 24 个与 stable 逐字节一致**，原参考只有 3 个能对上。所以重录在实质上是对的。

原数据里的两处硬伤：`quine_out_1.txt` 是直接抄的 `quine.bf` 源文件（并非程序输出，真实输出是 822B 即源码去掉换行），`prime_out_1.txt` 是 UTF-16 BOM。

6.bf / 7.bf / mandelbrot / hanoi3 / factorial2 / prime_in_1 在纯 Python 里跑不完，没有经过独立复核，只靠「当前编译器 vs stable」互相印证。

**输入文件必须以换行结尾。** `prime`、`numwarp` 原来缺行尾换行，程序会一直等输入永不终止（表现为输出 0 字节或超时），现已补上。新加用例时注意这一点。

仓库开了 `core.autocrlf=true`，索引存 LF、检出变 CRLF。已逐一验证 74 组输入按 LF 和 CRLF 喂进去结果完全相同，所以换行转换不会破坏套件，不必为此改 git 配置。

`infinite_loop` 用例（`fib`、`factorial2`）只比较前 `len(_out_N.txt)` 个字节，读满就杀进程，不要求程序自己终止。重录时保留原有长度，所以 `fib` 仍只比前 27KB。

### 当前已知的失败

全量运行 4 项未过：`-O0` 29/30、`-O1` 29/30、`-Ostrict` 29/30，二进制基线与产物哈希全部一致。这 4 项都是真的代码问题，不是数据问题。

- **`dbf2c` 的误编译，已定位到汇编生成层**。它的输出是 C 代码：
  - `dbf2c_in_1`：`-O0` 输出 `write(1,e,1)`，`-O1` 输出 `write(w,e,1)`，`-Ostrict` 输出 `char>r[65536]`。
  - `dbf2c_in_2`：`-O0` 和 `-O1` 都正确，只有 `-Ostrict` 错（`char>r`）。
  - `write(w,e,1)` 里 `w` 全文没有声明，不合法；同段 C 的 `read(0,e,1)` 用的是整数 fd，所以 `write` 的 fd 也应是 `1`。**`write(1,e,1)` 才是真值**，独立写的朴素解释器和 `-O0` 产物都给出它。
  - 两个参考文件已按真值修正（解释器与 `-O0` 双重确认），所以上表这些失败是真 bug。
  - **用解释执行路径（不带 `-o`）跑同一个程序，current 和 stable 在 `-O0` / `-O1` / `-Ostrict` 六种组合下全部输出正确的 `write(1,e,1)`。** 解释执行走优化器和 `rearrangeIRs`，只是不经过 `generateWindows` 的指令选择。所以故障在**指令生成 / 寄存器分配 / 溢出 / 编码**这一层，优化器和 `rearrangeIRs` 是干净的。
  - 也排除了纸带布局：解释执行和编译产物用同一组 `tapeSize` / `initOffset`，变量紧凑化在解释路径上同样生效且结果正确。
  - `-O0` 的编译产物是对的，说明「原始形态 IR → ASM」这条路没问题；错只出现在「重排后的 IR → ASM」。优先怀疑 `generateWindows` 对重排后 IR 形态的假设、`postOptimize` 的折叠、以及 `imulConstant` / `lea` 替代乘法的常量计算。
- **`7.bf` 的 `7_in_4` 在 `-O0` 下超过 180 秒**。同目录其他输入在 O0 下要 37~68 秒，这个更慢；尚未确认是慢还是死循环。

`--print-info` 仍是定位优化问题的主要手段，会打印 `numIR` / `numASM` 和各优化遍的成功与否及 IR 数量变化。

### 用例目录约定

用例默认放在 `test/cases/`，可用 `--cases-dir` 指向别处。

- `test/cases/<name>/<name>.bf` 必需。
- `<name>_in_<N>.txt` 和 `<name>_out_<N>.txt` 成对出现，`N` 从 1 开始，可以有多组（`6`、`7`、`9`、`calc`、`numwarp`、`prime`、`dbf2c` 都有多组）。
- `<name>.md` 可选，说明题目。
- 空标记文件 `interactive`：需要交互输入，不适合自动化（`bounce`、`ghost`、`tictactoc`）。
- 空标记文件 `infinite_loop`：故意不终止（`factorial2`、`fib`），不要拿去跑自动化。
- `test/cases_large/` 被 gitignore，本地有 `8cc/8cc.bf` 这类大压力用例，靠 `--include-large` 纳入（目录可用 `--large-dir` 改）。
- `test/bf-compiler-stable.exe` 是参考实现（本地、未跟踪），全部参考输出都由它录出。它比当前编译器旧；换掉它等于换掉整个套件的判定基准，动手前想清楚。
- `test/regression.ps1` 是已被 `run_tests.py` 取代的旧脚本（本地、未跟踪、且本身跑不起来：用例发现是非递归的、`--optimize-strict` 已失效，路径也仍指向旧的 `cases/`）。除非要考古，否则忽略它。

## 架构

`main.cpp` 把三段流水线直接串起来，没有中间框架：

```
(BF) --parseToQuads--> (Quadruple 线性 IR) --CodeOptimizer--> (MidBasicBlock CFG) --WindowsPEPlusGenerator--> (PE)
                                                    \----------- Interpreter（走 X64BasicBlock）-----------/
```

另有一条旁路：`--out-format c` 只走 `parseToC`，不经过优化器和生成器。

两个入口函数：
- `compileAndSave`（`main.cpp:158`）：`OUT_C` 走 `parseToC`，其余走 `parseToQuads` → `CodeOptimizer::optimize` → 构造 generator → `generate`。
- `compileAndExec`（`main.cpp:184`）：同上，但优化后只调 `rearrangeIRs` 重排，然后 `Interpreter::interpret` 直接解释执行 `X64BasicBlock`。

### Parser（`parser/`）

只剩两个全局自由函数，`NeoParser` 类已经没有了（文件名是历史遗留）：

- `std::expected<std::vector<Quadruple>, Error> parseToQuads(std::istream&)`
- `void parseToC(int32_t tapeSize, std::istream&, std::ostream&)`

没有 AST。

### IR（`optimizer/quadruple.h`）

`Quadruple {type, result, arg1, arg2}`，12 个操作码：`HALT PTR_ADD ADD SUB MUL NEG ASSIGN JNZ JZ JMP PUTCHAR GETCHAR`。参数带类型标签：`NIL` / `PTR_OFFSET`（`[ptr+offset]`）/ `BB_TEMP`（`T<n>`）/ `CONSTANT`。

关键约束：纸带单元是字节，算术 **mod 256**；`ADD` / `MUL` 的常量必须放在 `arg2`；化简前先跑 `preSimplifyMod256()`。

**两种 IR 方言**（`quadruple.h` 开头注释）：parser 输出的跳转目标在 `arg2`，基本块内的 IR 丢掉目标、改靠 CFG 边（`succY` / `succN`）。`jnz` 和 `jz` 在块内只差哪个后继算 Y。

窥孔重写是表驱动的：`Quadruple::SINGLE_SIMP_RULES`（13 条）和 `MERGE_RULES`（24 条），元素是 `cond` / `actionMod256` 函数指针。**加规则改这两张表，不要在调用点写特判**；改条数要同时改 `std::array` 的长度模板参数。

### Optimizer（`optimizer/`）

`CodeOptimizer` 持有 `std::vector<std::unique_ptr<MidBasicBlock>>`。`MidBasicBlock` 是裸字段 `struct`（`irs`、`preds`、`succN`、`succY`、`ptrIn`/`ptrOut`、`ptrDelta`，外加 `varValueInfo`、`liveVarInfo`），优化方法挂在它身上。

`CodeOptimizer::optimize()`（`code_optimizer.cpp`）是手工编排的定点循环序列，不是 pass manager。实际顺序：

1. `O0` 直接返回。
2. `eliminateLocalLoop` → `geOptimizeLocal` → `foldJumpBeforeGlobal`。
3. 定点：`eliminateLocalLoop` + `geOptimizeLocal` + `foldJumpBeforeGlobal`。
4. 定点：`hoistLocalLoopConstAssign` + `geOptimizeLocal` + `eliminateLocalLoop` + `foldJumpBeforeGlobal`。
5. `geOptimizeLocalWithGlobalInfo`。
6. 若存在指针状态已知的块：定点跑 `eliminateLocalLoop(false)` / `hoistLocalLoopConstAssign` / `foldJumpWithGlobalInfo`，每次后接 `geOptimizeLocalWithGlobalInfo`。
7. 若**所有**块指针状态已知：加 `eliminateAssignWithLiveVarInfo` 参与定点，最后做**变量紧凑化**——收集所有块用到的 offset，排序后经 `renameVar` 重映射成连续下标，并把 `m_tapeSize` 改成 offset 个数。这就是 `-mmem-size` 在全局指针可定时「被自动忽略」的原因。
8. 否则：把负向最小 offset 记入 `m_minNegOffset`，用于平移初始指针。

`mergeAndRemove()`（合并块 + 删不可达块）是在各遍内部被反复调用的，不在上面这串顶层序列里单独出现。

**顺序和先决条件是有意义的。** 每个方法上方的注释写了它的先决条件，例如：
- `analyzeLiveVar` 要求所有块指针状态已知、且指针移动已消除，否则结果错。改调用顺序前必读 `code_optimizer.h`。
- `prepareForGlobal()` = `analyzePtrFlow` + `eliminatePtrMove` + `analyzeVarValueFlow`。
- **`prepareForGlobal()` 可能往 `m_basicBlocks` 追加新块**（`eliminatePtrMove` 会把爆指针状态的跳转拆成新块并 append 到末尾）。不要跨它持有块的指针或下标。

### Generator（`generator/`）

`WindowsPEPlusGenerator` 的构造函数会**把 IR 数组从 `MidBasicBlock` 里移走**，所以构造之后原 CFG 的 `irs` 就空了。

`generate(level, isModern, printInfo, os)` 的顺序是：

```
generateASM  （level != O0 时内部先调 rearrangeIRs；然后逐块 generateWindows 做指令选择）
placeBasicBlocks  （DFS 排序，落空不行的地方插 jmp）
setBlocksRelJmp   （近跳转 → 短跳转松弛）
writeTo           （按 isModern 分派 modernWriteTo / legacyWriteTo）
```

- `main.cpp` 两处都固定传 `isModern = true`，所以 **`legacyWriteTo` 从 CLI 不可达**，头文件里也留着 `// todo 移除 legacy exe`。
- `X64Asm`（`x64_asm.h`）是汇编 IR，17 个操作符：`MOV MOVZX LEA ADD SUB CMP SHL NEG IMUL TEST JMP JZ JNZ CALL XOR INC DEC`，操作数是寄存器/内存加 scale+index+disp。
- `X64EncodingCtx`（`x64_encoding_ctx.h`）是**手写的 x86-64 编码器**，REX/ModRM/SIB/立即数全部手写。头文件顶部列了它「允许处理哪些汇编」，那是 `generateWindows` 与编码器之间的契约，加新指令形态必须先更新那份清单。
- 寄存器分配和溢出在 `X64BasicBlock`（`collectTempInfo`、`findReg`、`ensureOldTempInReg` 等），溢出到 `[rsp + STACK_SPILL_OFFSET]`，值为 40（`windows_pe_plus_generator.h:39`）。
- `x64_basic_block.cpp::generateWindows` 是全项目最脆弱的地方（该文件约 2400 行），有意不重构，`readme.md` 明确劝退。把它当承重墙。

### PE 输出（`generator/windows_pe_plus_generator.cpp`）

PE 头和 `.rdata` 以 `std::array<unsigned char, N>` 形式硬编码嵌入，字段位置靠 `MODERN_*_OFFSET` 常量寻址。`modernWriteTo` 写正常 PE，从 kernel32 导入 `ExitProcess`、从 ucrt 导入 `putchar` / `getchar`。

**任何改变代码长度或段布局的改动，都要重新推导这些偏移常量**，公式写在头文件注释里。`runtime/runtime_modern.asm` + `runtime_modern.exe` 是这些二进制块的来源（经 ml64 生成），不参与 CMake 构建，只有运行时或它的布局变了才需要重做。`runtime/runtime.cpp` + `runtime.asm` + `runtime.exe` 属于已废弃的 legacy 自举方案（无导入表，会被 Defender 报毒），仅作参考保留。

`inst_table.csv`、`modrm.png`、`sib.png` 是编码器的参考资料，不被任何代码加载。

### Interpreter（`interpreter/`）

`Interpreter::interpret(const void* block, const GettersForInterpret&, tapeSize, ptr)` 直接解释执行 IR，走 CFG 不编译。

要同时处理 `MidBasicBlock` 和 `X64BasicBlock`，但没有公共基类；靠传入 `GettersForInterpret`（`getCode` / `getSuccY` / `getSuccN` 三个函数指针）模拟虚表。`mid_basic_block.h` 为了这个 include 了 `interpreter.h`。

越界检查由 `INTERPRETER_CHECK` 宏控制，默认关闭（`CMakeLists.txt:8` 被注释）。怀疑纸带越界时临时打开这个宏，比读 `generateWindows` 省事。

## 硬性约定

- **不用异常。** 编译带 `-fno-exceptions` / `_HAS_EXCEPTIONS=0`。错误走 `std::expected<T, Error>`，协议写在 `error.h` 注释里：正常路径有返回用 `std::expected<T, Error>`，无返回用 `std::expected<void, Error>`，中途不要 `exit`。
- 注释、`readme.md`、本文件用中文；标识符用英文。
- `readme.md` 的「代码架构与自我反思」记录了作者自认的设计债、有意放弃的优化（无跨块临时变量、无全局 CSE、无全局复制传播、无更细指针分析）和几条反直觉但实测更快的决策（循环头不对齐、`inc` + `cmp` 而非 `lea` 的循环写法）。**看到「显然可以优化」的地方先读那一节**，不少怪异选择有测量数据支撑。
- `local_docs/` 是优化构想草稿：`algebraic_simp_cases.md`（代数化简）、`loop_pattern_cases.md`（循环模式）、`new_opt_cases.md`（Tail duplication 等）、`reg_opt_cases.md`（寄存器变量）、`todo.md`（分三期排的任务表与范围界定）。做优化前先看这里有没有已经写好的设想。

## 回复风格（中文对话）

用户明确反感 AI 伪人腔调。以下四类表述禁止使用。

**1. 翻案腔**：先立一个用户并没有的误解再推翻。禁用「不是……而是……」「并非……而是……」「不在于……而在于……」「与其说……不如说……」「看似……实则……」这类句式。直接给判断，再给依据。

> ❌ 这不是工具的问题，而是思维方式的问题。
> ✅ 这是思维方式的问题。

**2. 破折号滥用**：不要用破折号制造停顿揭晓，或插入无谓补充。写完整句子，或用逗号句号。

> ❌ 答案其实很简单——专注。
> ✅ 答案是专注。

**3. 拟人化喻体**：不要把工具、系统、流程比作理想化的人（「像一位智慧的导师」「不知疲倦的助教」）。写清它实际做了什么。

> ❌ 这相当于一个永不疲倦、秒级响应的初级审查员。
> ✅ 它不会疲倦，响应在秒级，能承担初级审查的活。

**4. 结尾请示腔**：回答末尾不要问「要不要我……？」「需要我……吗？」来征求许可。用陈述句收尾，给出判断、建议或计划，让用户自己决定。*本条只管对话末尾，不影响正常询问代码细节或预期效果。*
