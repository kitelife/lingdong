---
title: absl::Hash 的一个误用
id: absl-hash-misuse
date: 2025-06-15
---

近期在写 [LingDong](https://github.com/kitelife/lingdong) 的 [PlantUML](https://github.com/kitelife/lingdong/blob/7077f1def3790d1ab3f1c9a7f5809c71c98c2ddc/src/plugin/plantuml.hpp#L191) 插件时，一开始使用了 `absl::Hash` 来计算 PlantUML 输出的 svg 图内容的哈希值，作为输出文件名。

```cpp
auto hash_value = absl::Hash<std::string>{}(absl::StrJoin(codeblock->lines, "\n"));
```

发现：即使文章中 plantuml 原始绘图代码没有变更，每次构建，都会产出新的 svg 文件（文件名是文件内容的哈希值，从文件名看都是新文件）。

一开始猜测 PlantUML 生成的 svg 图内容中暗含了什么生成时间点之类的字符串。
经单测分析：对于相同的原始字符串，一次单测运行中两次生成的哈希值是相同的，不同次单测运行中生成的哈希值不同。那么基本可以确定问题原因是 `absl::Hash` 哈希依赖于某个不确定的种子。分析源码会发现：

```cpp
absl::Hash<std::string>{}(...)
```

会间接调用 `MixingHashState::hash`，其实现为：

```cpp
template <typename T, absl::enable_if_t<!IntegralFastPath<T>::value, int> = 0>
static size_t hash(const T& value) {
  return static_cast<size_t>(combine(MixingHashState{}, value).state_);
}
```

其中 `MixingHashState{}` 调用私有的无参构造函数进行实例化：

```cpp
MixingHashState() : state_(Seed()) {}
```

`state_` 成员变量 和 `Seed()` 成员函数的定义如下所示：

```cpp
  // Seed()
  //
  // A non-deterministic seed.
  //
  // The current purpose of this seed is to generate non-deterministic results
  // and prevent having users depend on the particular hash values.
  // It is not meant as a security feature right now, but it leaves the door
  // open to upgrade it to a true per-process random seed. A true random seed
  // costs more and we don't need to pay for that right now.
  //
  // On platforms with ASLR, we take advantage of it to make a per-process
  // random value.
  // See https://en.wikipedia.org/wiki/Address_space_layout_randomization
  //
  // On other platforms this is still going to be non-deterministic but most
  // probably per-build and not per-process.
  ABSL_ATTRIBUTE_ALWAYS_INLINE static uint64_t Seed() {
#if (!defined(__clang__) || __clang_major__ > 11) && \
    (!defined(__apple_build_version__) ||            \
     __apple_build_version__ >= 19558921)  // Xcode 12
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&kSeed));
#else
    // Workaround the absence of
    // https://github.com/llvm/llvm-project/commit/bc15bf66dcca76cc06fe71fca35b74dc4d521021.
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(kSeed));
#endif
  }
  static const void* const kSeed;

  uint64_t state_;
```

而 `kSeed` 静态成员变量的初始化逻辑为 - 存储自身的地址：

```cpp
ABSL_CONST_INIT const void* const MixingHashState::kSeed = &kSeed;
```

C++ 程序中，**静态变量的地址在编译期可知（链接时确定），静态变量在程序启动时初始化（早于 main 执行）**。
基于安全考虑，现代操作系统都会使用“[地址空间配置随机加载（ASLR）](https://en.wikipedia.org/wiki/Address_space_layout_randomization)” 这一机制，导致：**同一程序中的同一静态变量，不同次运行或者在不同进程中，内存地址不同**。

`absl::Hash` 基于这一机制来生成哈希的不确定种子值。[Abseil 官方指南](https://abseil.io/docs/cpp/guides/hash#abslhash)中其实也有一处不太显眼的说明：

```text
NOTE: the hash codes computed by absl::Hash are not guaranteed to be stable across different runs of your program, or across different dynamically loaded libraries in your program.
```

这也就意味着：基于 `absl::Hash` 生成的哈希值以及 Abseil 中依赖哈希逻辑的容器不能持久化以后续使用。

Abseil 库中似乎也未提供方法以绕过这一机制。只好改成使用 C++ 标准库中的 `std::hash`：

```cpp
auto hash_value = std::hash<std::string>{}(absl::StrJoin(codeblock->lines, "\n"));
```

`std::hash` 生成的哈希值是确定性的，其实现基于 [MurmurHash2](https://en.wikipedia.org/wiki/MurmurHash#MurmurHash2) 和 [CityHash](https://github.com/google/cityhash) 哈希算法。