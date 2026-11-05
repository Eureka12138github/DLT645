# DLT645-2007 电能表通信协议解析（C 语言实现）

基于开源项目 `dlt645_2007` 的学习副本。

* 实现 DL/T 645-2007 规约数据类型的定义
* 提供帧数据的封装及解析相关 API

## 目录结构

```
.
├── src/                    # 源码
│   ├── dlt645_api_07.c     # 协议实现（规约表、帧封装/解析）
│   ├── dlt645_api_07.h     # 协议头文件（数据结构、API 声明）
│   └── main.c              # 命令行入口
├── out/                    # 构建产物（编译时自动生成，已被 git 忽略）
│   ├── *.o / *.d           #   中间文件与依赖
│   └── dlt645_cli.exe      #   可执行文件
├── docs/                   # 学习笔记与记录
├── makefile                # 构建脚本
├── .clangd                 # clangd 解析配置（MinGW 头文件路径）
├── compile_commands.json   # 编译命令数据库（IDE 推断头文件语言用）
├── .vscode/settings.json   # IDE 端 clangd 参数
└── README.md
```

## 项目架构

本工程分为五层，自上而下依次是“演示外壳 → 对外接口 → 规约映射 → 数据转换 → 基础工具”，
协议逻辑全部集中在 `src/dlt645_api_07.c`，`src/main.c` 只做命令行演示。

```text
┌──────────────────────────────────────────────────────────────────┐
│ 应用层  src/main.c  —— 命令行演示，不含协议逻辑                  │
│   -i 查规约  -p 交互组帧  -u 解析报文  --all 全表扫描  -v/-h     │
└──────────────────────────────┬───────────────────────────────────┘
                               │ 调用公开 API（声明见 dlt645_api_07.h）
┌──────────────────────────────▼───────────────────────────────────┐
│ API 层  dlt645_api_07.c 对外入口（5 个）                        │
│   pack_d07_frame_by_data     结构体 → 帧字节流（组帧）          │
│   unpack_d07_frame           帧字节流 → 结构体（解帧）          │
│   get_d07_ruler_info         DI → 元信息（映射层总入口）        │
│   get_d07_ruler_id           元信息 → DI（空壳，直接返回成功）  │
│   get_d07_first_valid_frame  从字节流中定位第一个有效帧          │
└──────────────────────────────┬───────────────────────────────────┘
                               │ unpack 内部先查 DI，再按 func 转换
┌──────────────────────────────▼───────────────────────────────────┐
│ 映射层  get_d07_ruler_info()：约 5300 行，三层嵌套 switch        │
│   switch(DI3) 分派到 7 张标准表 A.1~A.7（case 0~6）             │
│   逐项填 { type, rdwr, format, func, para(结算日/费率), len, name } │
└──────────────────────────────┬───────────────────────────────────┘
                               │ func 字段即转换函数指针
┌──────────────────────────────▼───────────────────────────────────┐
│ 转换层  60 个 trans_d07_data_*（含 1 个错误占位）               │
│   U2F：用户侧字符串 → 帧侧 BCD 字节                             │
│   F2U：帧侧 BCD 字节 → 用户侧字符串 / 结构体                    │
└──────────────────────────────┬───────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────┐
│ 基础层  d07_str2bcd / d07_bcd2str / d07_bcd_to_double / d07_remove_dot │
└──────────────────────────────────────────────────────────────────┘
```

### 各层职责与位置

| 层 | 位置（`src/dlt645_api_07.c` 行号） | 职责 |
|---|---|---|
| 应用层 | `src/main.c` | 命令行解析、交互输入、结果打印 |
| API 层 | `pack_d07_frame_by_data`(L199)、`unpack_d07_frame`(L454)、`get_d07_first_valid_frame`(L12741) | 组帧 / 解帧 / 定位帧 |
| 映射层 | `get_d07_ruler_info`(L672–L5985) | 由 4 字节 DI 查表得到数据项元信息 |
| 转换层 | L6332 起的 `trans_d07_data_*` 共 60 个 | 数据域 BCD ↔ 用户侧数据（U2F/F2U） |
| 基础层 | L6005–L6331 | BCD 与字符串、浮点及小数点的互转工具 |

### 一次调用的数据流

* `-i 00000000`：`main → show_ruler_info → get_d07_ruler_info`，命中表 A.1 后填充 `S_D07_RULER_INFO`，
  再由 `main` 打印类型/长度/格式/名称。
* `-u 68 ... 16`：`unpack_d07_frame → 校验起始符/校验和/结束符 → 取数据域前 4 字节为 DI
  → get_d07_ruler_info → func(F2U) 转换数据域 → g_out_data_07 → main` 打印。
* `-p 00000000`：`pack_d07_frame → get_d07_ruler_info → 填充 S_D07_PACK_FRAME（地址/控制码/数据/长度）
  → pack_d07_frame_by_data（数据域逐字节 +0x33、累加 CS）→ 输出整帧`。

> 说明：上图中的行号以当前版本为准；`get_d07_ruler_id` 目前为空实现，`is_right_d07_frame` 等
> 少数声明尚未落地，属于原版遗留缺陷，学习阶段保持原样不动。

## 构建

### 环境要求

MinGW-w64（`gcc` 与 `mingw32-make` 需在 PATH 中）。本机路径：

```
D:\MinGW-64\x86_64-15.2.0-release-win32-seh-ucrt-rt_v13-rev0\mingw64\bin
```

### 方式一：make（推荐）

```powershell
mingw32-make          # 构建，产物输出到 out\dlt645_cli.exe
mingw32-make clean    # 清理 out\ 目录内的构建产物
```

### 方式二：直接 gcc 编译

```powershell
gcc -Wall -g -fexec-charset=GBK -o dlt645_cli.exe src\dlt645_api_07.c src\main.c
```

> `-fexec-charset=GBK`：源码为 UTF-8 编码，而 Windows 控制台默认 GBK 代码页，
> 需让字符串字面量按 GBK 字节写入 exe，中文输出才不乱码。

## 使用

`dlt645_cli.exe` 是命令行工具，需在终端中带参数运行（双击无参数启动会立即正常退出）。

```powershell
# 查看版本
.\out\dlt645_cli.exe -v

# 查看帮助
.\out\dlt645_cli.exe -h

# 查询某个规约 DI 的信息（DI 为 4 字节十六进制）
# 示例：00000000 = 组合有功总电能，输出类型/长度/读写属性/数据格式/转换函数
.\out\dlt645_cli.exe -i 00000000

# 解析一帧 645-2007 报文（从 0x68 起逐字节十六进制输入）
# 示例帧：地址 000000000001、控制码 0x91（从站应答读数据）、DI 00000000、数据 1234.56
.\out\dlt645_cli.exe -u 68 01 00 00 00 00 00 68 91 08 33 33 33 33 89 67 45 33 9E 16

# 交互式组帧（按提示输入地址、方向等）
.\out\dlt645_cli.exe -p 00000000

# 遍历显示全部有效规约（全表扫描，耗时较长）
.\out\dlt645_cli.exe --all
```

帧格式参考（DL/T 645-2007）：

```
0x68 | A0~A5(BCD倒序) | 0x68 | C | L | DATA(每字节+0x33) | CS | 0x16
```

CS = 从第一个 0x68 累加到 DATA 最后一字节（不含 CS 与 0x16），取低 8 位。

## IDE 支持（clangd）

编辑器使用 clangd 做代码补全与语法检查。clangd 默认不知道 MinGW 的头文件路径、
也不知道 `.h` 是 C 而非 C++，因此需要三个配置文件配合：

| 文件 | 作用 |
|---|---|
| `.clangd` | 补上 `--target=x86_64-w64-windows-gnu` 与 MinGW 的 `-isystem` 头文件目录 |
| `compile_commands.json` | 提供实际编译命令；clangd 据此把头文件推断为 `-x c-header`，避免按 C++ 解析 |
| `.vscode/settings.json` | IDE 端 `--query-driver` 指向 MinGW gcc，并把 `*.h` 关联为 C 语言 |

> 注意：`.clangd` 中只能有一个顶层 `CompileFlags` 键；重复出现会让后者覆盖前者，
> 导致 `--target` 丢失、退化为 MSVC 目标并产生大量误报。
>
> `compile_commands.json` 中的路径基于本机 MinGW 位置，换机器或新增源文件时需同步更新。
>
> 自检命令（输出 `All checks completed, 0 errors` 即为正常）：
>
> ```powershell
> clangd --check=src/main.c
> clangd --check=src/dlt645_api_07.h
> ```

## 学习基线

* 标签 `v1.0-original`：与 GitHub 原版逐行一致（UTF-8 转码无损验证）的对照基准
* `git diff v1.0-original`：查看学习期间的全部修改
* `docs/`：学习笔记、实验记录、分析文档（命名约定见 `docs/README.md`）

## 说明

原版代码存在若干已知缺陷（部分为致命级），当前仅做了编译必需的修复与文件名错位修正，协议逻辑保持原样以便学习对照；详见学习笔记与后续重构记录。
