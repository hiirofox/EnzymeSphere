# ClangEnzyme / LLVM 21 的 Vulkan SPIR-V 补充包

现有 Windows Clang 21.1.8 已包含 SPIR-V 后端，不需要重新编译整个 Clang。
此前提供的便携包缺少 HLSL 头文件及部分 LLVM 工具，本包补齐这些文件，并提供 Enzyme → Vulkan SPIR-V 的最小实例。

**本包复用原来的 MinGW ClangEnzyme-21.dll、clang.exe 和运行库。没有替换编译器或插件。**

## 安装和运行

将整个 `vulkan21` 文件夹放在你已有的 `clang.exe` 旁边，例如：

```text
D:\Projects\c++\WinGrad\ClangEnzyme\clang.exe
D:\Projects\c++\WinGrad\ClangEnzyme\ClangEnzyme-21.dll
D:\Projects\c++\WinGrad\ClangEnzyme\vulkan21\build-vulkan.ps1
```

从该 ClangEnzyme 目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\vulkan21\build-vulkan.ps1
```

也可以显式指定原有编译器目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\vulkan21\build-vulkan.ps1 -ToolchainDir "D:\Projects\c++\WinGrad\ClangEnzyme"
```

只测试 HLSL → Vulkan SPIR-V，不加载 Enzyme：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\vulkan21\build-vulkan.ps1 -ShaderOnly
```

所有新工具位于 `vulkan21/bin`，原有 LLVM/Clang bin 保留。编译输出在 `vulkan21/out`。
补充工具需要原有包中的 `libLLVM-21.dll`、`libc++.dll` 等运行库。

## 编译流程

1. C++ 模型和求导适配函数 → Vulkan triple 的 LLVM IR。
2. `opt -load-pass-plugin=ClangEnzyme-21.dll -passes=enzyme` → 梯度 IR。
3. HLSL 只声明 shader 入口、buffer binding 和任务调度，生成相同 triple 的 IR。
4. `llvm-link` 链接模型梯度与入口，`opt` 内联、优化并删除不需要的函数。
5. `llc -mtriple=spirv-unknown-vulkan1.3-compute -filetype=obj` → Vulkan SPIR-V。
6. SPIRV-Tools 清理无用数据，并用 `spirv-val --target-env vulkan1.3` 验证。

`--target=spirv64` 默认面向 OpenCL，不能替代上述 Vulkan 路线。
HLSL 没有直接调用 Enzyme 的 C++ 函数指针接口；本例先对独立 C++ 模型求导，再链接 shader 入口，因此也不要求 Enzyme 理解 HLSL 的资源读写 intrinsic。

## 包内实例与 Vulkan buffer 布局

| 输出 | 工作组大小 | `vkCmdDispatch` | Descriptor set 0 |
|---|---|---|---|
| `square.spv` | 64×1×1 | (1,1,1) | binding 0：64 个 float 的 storage buffer，输出 i² |
| `scalar.spv` | 64×1×1 | (1,1,1) | binding 0：64 个 float 的 storage buffer，输出 Enzyme 生成的 2i |
| `tasks.spv` | 2×1×1 | **严格 (1,1,1)** | binding 0：3 个 float 的输入 storage buffer；binding 1：6 个 float 的输出 storage buffer |

`tasks.cpp` 保留 `loss(int task, const float* params, int n)` 形式。
演示适配器将参数数固定为 3，shader 将任务数固定为 2，并分别写出三个梯度分量。
不能把这个演示当作任意动态参数长度、任意 task 数量的通用实现。

对于 params=[2,3,5]，解析期望值和 CPU 数值检查结果是：

| 结果 | 值 |
|---|---|
| task 0 | [4,4,6] |
| task 1 | [2,2,4] |
| 梯度求和 | [6,6,10] |

本例 GPU shader 输出前两行共六个数，没有实现 GPU reduction。

## 实际验证范围

在当前 Linux 环境，使用同版本 LLVM/Clang 21.1.8 和 Enzyme 提交
`2c8fd12d5ccc2f4f26a171f6a79ac170bc986ce4`，已经实际完成：

- 带 buffer 写入的 HLSL shader 编译。
- 标量 C++ 模型及三参数 C++ 模型的 Enzyme 反向微分。
- 梯度 IR 与 shader 入口链接、优化和 SPIR-V 代码生成。
- 标量与三参数输出均通过 `spirv-val --target-env vulkan1.3`。
- 最终实例仅声明 `OpCapability Shader`，使用 `OpMemoryModel Logical GLSL450` 和 `OpEntryPoint GLCompute`。
- 将同一份梯度 IR 的 target layout / calling convention 适配到 x86_64，执行 CPU 数值检查：scalar(-2,0,0.5,3)=[-4,0,1,6]；两项 task 的梯度为 [4,4,6]、[2,2,4]。

预编译 `.spv`、反汇编、梯度 IR 和 CPU 数值记录见 `verified/`。
**CPU 检查不是 Vulkan 执行。当前没有 GPU/软件 Vulkan ICD，未执行 Vulkan dispatch，也未运行 Windows 版二进制。**
Windows 工具来自匹配的 MSYS2 CLANG64 21.1.8-4 发行包；Windows PowerShell 脚本尚未在 Windows 运行。

## LLVM 21 的已遇到限制

- 官方 HLSL 头文件中的部分 constexpr 兼容重载触发诊断，本脚本采用 `-Wno-invalid-constexpr`；其余两项 warning suppression 针对该版本头文件的诊断。
- C++ 局部梯度数组保留动态索引时，SPIR-V 后端的 bitcast legalization 会失败。本例通过固定三个输出分量，使优化器消除局部数组。
- 某些 HLSL 循环组合会产生无效的 convergence IR。本例采用固定、明确的输出调用。
- LLVM 21 默认启用全部 SPIR-V 扩展可能产生额外 capability。本脚本限制扩展，并在无 8-bit 整数类型时移除残留的冗余 Int8 capability，然后重新验证。

因此，本包证明了受限 C++ 模型经 Enzyme 到 Vulkan 的可行流程，并不承诺任意普通 C++、动态内存分配或复杂反向传播 tape 都能转换。

## 来源与复现

- LLVM / Clang / HLSL headers：21.1.8。
- Enzyme 测试源码提交：`2c8fd12d5ccc2f4f26a171f6a79ac170bc986ce4`。
- Windows LLVM 工具：MSYS2 CLANG64 21.1.8-4。
- Windows SPIRV-Tools：MSYS2 CLANG64 3~1.4.328.1-1。
- Linux 校验工具：Ubuntu 24.04 的 SPIRV-Tools 2025.1。
- 本次没有重编 Windows Clang，也没有另造一个与现有 Enzyme 不兼容的工具链。

本次测试用 Linux Enzyme 库由源码构建；为减少编译时间，CallDerivatives.cpp 使用 Clang -O0，其余核心文件使用 GCC -O2。没有改动 Enzyme 算法源码。

参考文档：

- https://releases.llvm.org/21.1.0/docs/SPIRVUsage.html
- https://releases.llvm.org/21.1.0/tools/clang/docs/UsersManual.html
- https://github.com/EnzymeAD/Enzyme
- https://github.com/llvm/llvm-project/tree/llvmorg-21.1.8/clang/lib/Headers
- https://repo.msys2.org/mingw/clang64/

许可证见 `licenses/`，文件来源和校验值见 `manifest.json`。
