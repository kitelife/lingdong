---
title: 译文：小心 fast-math
id: beware-of-fast-math
date: 2025-06-06
---

原文：[Beware of fast-math](https://simonbyrne.github.io/notes/fastmath)

## 一、fast-math 是什么？

fast-math 是一个编译器标志（flag），或者许多编程语言和编译器中存在的一个配置项，包括如下这些：

- [GCC](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) 和 [Clang](https://clang.llvm.org/docs/UsersManual.html#cmdoption-ffast-math) 中的 `-ffast-math`（`-Ofast` 也会包含这个编译标志）
- [ICC](https://www.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/compiler-reference/compiler-options/compiler-option-details/floating-point-options/fp-model-fp.html) 中的 `-fp-model=fast`（默认行为）
- [MSVC](https://docs.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170) 中的 `/fp:fast`
- Julia 中的 `--math-mode=false` [命令行配置项](https://docs.julialang.org/en/v1/manual/command-line-options/#command-line-options) 或 `@fastmath` [宏](https://docs.julialang.org/en/v1/base/math/#Base.FastMath.@fastmath)

那它实际会干啥呢？名副其实，让数学计算更快。听起来很棒，我们当然应该这样做！

> 我的意思是：fast-math 的核心是牺牲某些情况下的正确性，换取速度。如果 fast-math 任何情况下都能给出正确的结果，那它就不是 fast-math 了，而是数学计算的标准方式。
> -- [Mosè Giordano](https://discourse.julialang.org/t/whats-going-on-with-exp-and-math-mode-fast/64619/7?u=simonbyrne)

[IEEE 754标准](https://en.wikipedia.org/wiki/IEEE_754) 规定了浮点运算的规则，所有流行的编程语言基本都遵从该标准。编译器默认仅被允许执行遵从这些规则的优化手段。fast-math 允许编译器打破其中一些规则，这些突破常规的做法初看似乎无害，不过某些情况下可能会产生一些重大的下游效应[^译注1]。

[GCC](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) 中，`-ffast-math`（或 `-Ofast`） 会启用以下编译选项：
- `-fno-math-errno`
- `-funsafe-math-optimizations`
- `-ffinite-math-only`
- `-fno-rounding-math`
- `-fno-signaling-nans`
- `-fcx-limited-range`
- 以及 `-fexcess-precision=fast`

注意：`-funsafe-math-optimizations` 本身又包含一组编译选项：
- `-fno-signed-zeros`
- `-fno-trapping-math`
- `-fassociative-math`
- 以及 `-freciprocal-math` 等等

其中一些编译选项大多数情况下都不太可能会造成什么问题：`-fno-math-errno`[^1]、`-fno-signaling-nans`、`-fno-trapping-math` 会禁用很少使用（且支持不佳）的特性。其他一些，比如 `-freciprocal-math` 可能会略微降低精度，但在大多数情况下不太可能会造成什么问题。

[Krister Walfridsson](https://kristerw.github.io/2021/10/19/fast-math/) 对其中部分编译选项做了非常棒（也更客观一点）的解释，不过我想重点关注一下其中的三个。

## 二、`-ffinite-math-only`

> 基于参数和结果不会是 NaN 或正负 Inf 的假设，对浮点运算做优化。

其意图是允许编译器执行一些[额外的优化](https://stackoverflow.com/a/10145714/392585)，不过如果存在 NaN 或 Inf 值，则优化后的运行结果会不正确，例如：`x == x` 条件判断会被假设始终为真（实际上如果 `x` 是一个 NaN 值，则这个条件判断结果应该为假）。

这听起来真不错！我的代码不会产生任何 NaN 或 Inf 值，所以这个优化应该不会造成任何问题。

但是，如果你的代码之所以不会产生任何 NaN 中间结果，是因为代码内部调用了 `isnan` 来确保正确地处理了 NaN 值，那又会怎么样呢？

<iframe width="100%" height="400px" src="https://gcc.godbolt.org/e#z:OYLghAFBqd5QCxAYwPYBMCmBRdBLAF1QCcAaPECAMzwBtMA7AQwFtMQByARg9KtQYEAysib0QXACx8BBAKoBnTAAUAHpwAMvAFYTStJg1AB9U8lJL6yAngGVG6AMKpaAVxYMQAJlIOAMngMmABy7gBGmMQg0gAOqAqEtgzObh7epHEJNgIBQaEsEVHSlpjWSUIETMQEKe6ePiVlAhVVBLkh4ZHRFpXVtWkNvW2BHQVdkgCUFqiuxMjsHACkXgDMgchuWADUiyuOLEwECAB0CLvYixoAgoEEW/yo1LSoh/cTOwDsAEKXV1v/W2ImAIswYWzwCmYDGoE12P2uiw%2BABEOFNaJwAKy8TwcLSkVCcRxbBQzOaYHarHikAiaVFTADW0Q%2Bxw%2BAE4AByrdkY1kadkfLwffScSS8FgSDQaUg4vEEji8BQgKU03Go0hwWAwRAoVAsGJ0SLkShoPUGqLIYBcLg%2BGi0AiRRUQMK00hhQJVACenCpJrYggA8gxaF7VaQsAcjOJQ/ggWUAG6YRWhzCqUque3e3i3TDo0O0PBhYie5xYF0EYh4cXcNVUAzABQANTwmAA7v6YoxMzJBCIxOwpN35Eo1C7dFx9IYTGZ9AXFZApqgYtkGEmALRUKhMBQEVcHI5bVf%2BlYKnOlZf2BhOFx1CQ%2BfwjfKFEArDLxRICfqea2vrJJdqProX0aZcWj6a80m/YDyiGf9OiiIChk/W8elaWCxngqYSVmeY9HLTAFh4NFMWxF05VUdkADZVwoyQtmAZBkC2K1ji8LYIEcUgtlwQgSApFZxy2ZxTXoYg%2BK4CZeBVLQJimBBMCYLAoggBkQAxKVc1FUhxTU6VSM4BUlWpWkpg1bUTX1ESjQgcyzRQDZJy4FZJT4Oh7WIR1nVDN1mGIEMfV1P0CEDYMXXDScozxGMzzwBMkzxFM0wzatyEEHMXXzQtiwwBY8XLStMymWsmHrJtW3bTtkv4HtRHEAcqqHFR1FDXQfAMIwQFMYxzAyudlPxJckjXDctx3PcEAPI8Tysc8IAcJDx3vPI4L0TJ32ScCvx/Na0KfccoOaRCNr0faGFA4YlvQ47DtSTbt1Qh9lvE6ZsP7akgQItVcyxXTQzIyjqNo%2ByjCYlZjg0MG2I4rj8CIUTln4zihIsyI%2BK8CSjNVGTSDkhSuj6jSxVUqUZV4OUDOVYyVIxLwWI0LgPgoqQNFZa0PhfXNjx%2B2V9Ix6SiI4LwSN%2BnmpLpUgE3cpJoiAA%3D"></iframe>

> 基于 [John Regehr 写的一个示例](https://twitter.com/johnregehr/status/1440024236257542147)

> 解释一下这段代码：这个函数将返回寄存器 `eax` 与自己做 xor 异或操作，从而将返回寄存器设置为 0，这意味着函数将始终返回 `false`。

没错，你的编译器这时移除了所有那些检查操作。

这个做法看起来可能是显然的（“你告诉编译器不会存在 NaN 值，那为什么它还要做检测？”），也可能是荒谬的（“如果都不做检测，那又怎么能安全地把 NaN 值优化掉呢？”），对错与否，取决于你问谁，即使是编译器开发者也[无法达成一致意见](https://twitter.com/johnregehr/status/1440021297103134720)。

这也许是 fast-math 相关 [StackOverflow 问题](https://stackoverflow.com/q/7263404/392585)和 [GitHub](https://github.com/numba/numba/issues/2919) [bug](https://github.com/google/jax/issues/276) [报告](https://github.com/pytorch/glow/issues/2073)中最常见的原因。因此，如果你的代码经过 fast-math 编译优化后给出了错误结果，那么第一反应应该是关掉这个编译选项（`-fno-finite-math-only`）。

## 三、`-fassociative-math`

> 允许在浮点运算序列中重新结合操作数。

这个编译选项允许编译器改变浮点运算序列中的求值顺序。例如，如果有一个表达式 `(a+b)+c`，编译器可以将其调整为求值 `a+(b+c)`。这两个表达式对于实数在数学上是等价的，但在浮点运算中它们的求值结果并不相等：它们产生的误差可能不同，在某些情况下差异可能非常显著：

```julia
julia> a = 1e9+1; b = -1e9; c = 0.1;

julia> (a+b)+c
1.1

julia> a+(b+c)
1.100000023841858
```

### 3.1 向量化

那么我们为什么要启用这个编译选项呢？一个主要原因是它能启用向量/SIMD 指令相关的优化。

<iframe width="100%" height="400px" src="https://gcc.godbolt.org/e#z:OYLghAFBqd5QCxAYwPYBMCmBRdBLAF1QCcAaPECAMzwBtMA7AQwFtMQByARg9KtQYEAysib0QXACx8BBAKoBnTAAUAHpwAMvAFYTStJg1AB9U8lJL6yAngGVG6AMKpaAVxYMQAJlIOAMngMmABy7gBGmMQSXKQADqgKhLYMzm4e3nEJSQIBQaEsEVFcMZaY1slCBEzEBKnunj6l5QKV1QS5IeGR0RZVNXXpjX3tgZ0F3cUAlBaorsTI7BwApF4AzIHIblgA1EurjixMBAgAdAh72EsaAIJX11S0qEfbCu5eAKwAbNSPzwBU22qxEmuwA7AAhO7baHbB5PAgvPaQm4w7aBBF4JFQmEKXarAAi2w0Jw0VCxKJh/GI2wgeDxhI0SLReMc2w%2BnyZmK8kO52xBSwh2NRuL2hJFvKBS3e4OZUsJTKFAvxQuImAIcwYiNWyNuoPxHGmtE4714ng4WlIqE4rIUs3mmF2ax4pAImgN0wA1iBJKCTqCAJwADjWgfe/o0gdBXlB%2Bk4kl4LAkGg0pDNFqtHF4ChAKdd5oNpDgsBgiBQqBYsTokXIlDQFarUWQwGKPhotAIkWzEDCbtIYUC1QAnpxnXW2IIAPIMWjD/OkLCHIziOf4VXlABumGzc8wqjKrg7I946MwRrntDwYWIQ%2BcWF7BGIeET3ALDyYwAUADU8JgAO4T2JGCPGRBBEMR2CkED5CUNRe10GIDCMEBTGMcwLzCbNIGmVBYhsARtwAWgnVYs1PMo8M8CAHAGTwYn8UZ8kKPR4kSCiaOYrIKI6RiJgsMjmgYVp%2Bhceo9CaCihJGPIuiKXo2nYkphm4mSJGmW05gWPQH0wRYeENY1TV7DNVEDT4CM%2BSRtmAZBkG2YoTi8GlHFIbZcEIEhHVWGJtmcet6GpFYvMmXg8y0SZpgQTAmCwKIIE9EB3hTM941IRNEtTIzOCzHMXTdaYi1LOtK38msICKhsUGbLhWzoDtiC7Hs537ZhiFnUdy3HAgpxnXsF0MYBlwtVdyLwTdtwtXd90PF9yEEU9e3Q69WtvRYLQfJ8j2mN8P2/P8AKAmb%2BFA0RxEgo7oJUdQ510HxEJMMx9EvTC4stXDkkI4jtgIqgqCYBQCAIw5jlIqwKPsBgnBE9I6Ih5TxlkljshSKHaMyVjkjhpiSn4iThgUvjQYqJSGJUxT5JRsTiek%2BHVJmDSIJdVVdILM8TQyudjNM8zLOs2z7MciBnNc/AiACp0XN84rIk8rxgty/NwtISLou6F7koTBKUzTXgM2y3M8vi94vAcjQuFBT4pA0f1qtBVZYw4Ej2fTLL5bC/SOC8QyOZd0L3VITd6uSb0gA"></iframe>

可能有些人不太熟悉 SIMD 操作（或汇编语言），所以这里我简要解释一下（其他人可以跳过这一部分）。由于原始时钟速度没能再显著提高，处理器能够提升性能的一种方式是使用可以一次处理一个“向量”（简单来说，就是内存中连续存放的一组值）的操作（或者说指令）。

这种情况下，不再是执行一系列浮点数加法（`addss`），而是利用一个 SIMD 指令（`addps`），以浮点向量为参（当前示例中浮点向量包含4个浮点数，如果启用 AVX512 指令，则会多达16个浮点数），一次操作就能完成该向量与另一个向量逐元素地相加。对整个数组完成向量化相加后，以一个归约步骤将向量求和为单个值。这意味着不是如下这样求值：

```text
s = arr[0] + arr[1];
s = s + arr[2];
s = s + arr[3];
...
s = s + arr[255];
```

实际是如下这样做：

```text
s0 = arr[0] + arr[4]; s1 = arr[1] + arr[5]; s2 = arr[2] + arr[6];  s3 = arr[3] + arr[7];
s0 = s0 + arr[8];     s1 = s1 + arr[9];     s2 = s2 + arr[10];     s3 = s3 + arr[11]);
...
s0 = s0 + arr[252];   s1 = s1 + arr[253];   s2 = s2 + arr[254];    s3 = s3 + arr[255]);
sa = s0 + s1;
sb = s2 + s3;
s = sa + sb;
```

其中每行代码都只对应一条浮点指令。

问题是编译器通常不被允许做这个优化：它要求以不同于代码中指定的结合分组方式来求和，所以可能会得出不一样的结果[^2]。尽管在当前示例中它很可能是无害的（甚至可能提高精度[^3]），但并不总是如此。

### 3.2 补偿算术

然而，有些算法严格依赖于浮点运算的执行顺序。*补偿算术*就会利用这一点来计算中间计算中产生的误差，并在后续计算中对此进行校正。

利用补偿算术的最知名算法应该是 [Kahan 求和](https://en.wikipedia.org/wiki/Kahan_summation_algorithm)，它能校正求和循环中加法步骤产生的舍入误差。我们可以启用 `-ffast-math` 来编译 Kahan 求和算法的一种实现，并将结果与上面简单的循环求和进行对比：

<iframe width="100%" height="600px" src="https://gcc.godbolt.org/e#z:OYLghAFBqd5TKALEBjA9gEwKYFFMCWALugE4A0BIEAZgQDbYB2AhgLbYgDkAjF%2BTXRMiAZVQtGIHgBYBQogFUAztgAKAD24AGfgCsp5eiyagA%2BudTkVjVEQJDqzTAGF09AK5smUgMzknADIETNgAcp4ARtikIABM5AAO6ErE9kyuHl6%2BicmpQkEh4WxRMfHW2LZpIkQspEQZnt48fuWVQtW1RAVhkdFxVjV1DVnNA53dRSVxAJRW6O6kqJxcNPToLEQA1EqetGsbmwBUm7Wk05sApADsAEIXWgCCm8%2Bbq%2BtbShc%2Bd48vm8FbAhfH5PF5KS4%2BAAimy0ADotDRgfdQc9BKRNhACBDoVpgf8Ic5NrEAKwANjxQNidypm3O1xBf2e4K%2B0OZNNOF2JN3xnOheORL2ukIFz1I2CICyY2yRjyFyK4s3o3GJ/G8XB05HQ3EJSnmi2wl1iPj45CI2gVswA1iBiVpDNxpPw2Da7WqNVquPwlCA7Wb1QryHBYCgMGwEgxopRqKHw4wYqhgDwePE6PQiNFvRAIubyBFgrUAJ7cE2hjjCADyTHoRf95BwbGMwEktcIYsqADdsN7a9h1BV3Oni/wAdglbX6AQIqRC64cDmiKQCM7eAHVixgEoAGoEbAAd3LCWYQ7kwjEEk4MhPihUGhz%2Bh4hkbIHMpksE4i3sgs3QCTsQm7AC05Y%2BJsAE0DQLBKEQAENkQSBeqOFR/t4EBOMMTT%2BEwmATL0MQPkkKTIehBgEXkTA4cUfQPq0yEdEMbiNAYNFVIMXTBD0lF4WM9GZBhUHjOxkxUbMuoLEsBgLtgyx8IqyqqjmHrqAAHKSAGktImzAKgqCbEmsKxBizjkJs%2BDEGQhrGsZrhhhG6IXEaPDTPwfo6NMsxINgLA4DEEBWi69pcI65BuvwHpej6prmm5AWxE6IA%2BNIsKkjwWjSFcJLSDwtpXFofghZq3DOVFgaIEGyBoFgeCECQFBULQEasBwx6CKe4iSJeLXXmomi1vo8RGCYz4WHMYnLG8Bw7GwpiWiwSDGHs7xHCcpBnJctwiqi%2BwfMZlibEQxkFjKKL/MI/xHX8zJQpsOksjC8KIt8G2vOZmLYjCFIEkSZIUvZ1LcnS62/IyBZvRyXJAsS0IAdd52Mlst1styh2PUDfw3VdEBbNDSjnNDyMMoyl3QkQsNrcKqNihKpBSp8KMPHKjwBmOKrBQp2rbHqSwWbFkX%2BtFHleX0vnkNatoBUFzo%2BFcsLEqztZhVYEUuQGZUQCG6A2XGUYQDGtloImyYCAw6akJm2a1nmrCkDWJYa2WRCVtWOb1o2zYaq2SEEJ23Yar2/aDiulDCKOObvtO1uzssGoLkuQ6zGuG7bnuB5HoHnVnu1sidco3V3v0A1mMN76fsLP7IYBwGgeBkHQbB8FWIhbQoWhDEjJh2GCbhJG5ERrcYaRyEUVM1GN7RrHESPNhjwJhRd9R4990xrFD8JI36v0knSUzcly%2B63DKap6madpuk8PphnGaZNXc1ZGuxtE3NObzrnuZ53nUH5YtjkF%2BUK96vpRVklwHmktYjwjJDwAAnJlEkWVIHqV3qFQqz8ValRQIQcC2t05tQvFneQOdbzjiQN6B89BiEYJoEQAsh4IqkGIf0OhSgKFUJoVoIBLNf7cEhAQcCmwk67gfgfNSGktI6T0gZds4IhFH1EaffSRU%2BZAIlvFHwsIfDqI0ZozRiCCqekVgAxRIspBaDhCYsx5jzGkO4D4eS8tkHK2ip2U2aQQDSCAA"></iframe>

它和上面原始的求和代码给出了完全相同的汇编。为什么？

如果你将 `t` 的表达式代入 `c`，会得到：

```cpp
c = ((s + y) - s) - y);
```

再应用重新结合（reassociation）操作数，编译器将确定 `c` 实际上始终为零，所以可以完全移除。沿着这一逻辑进一步推理，`y = arr[i]` 以及循环内部的内容实际为：

```cpp
s = s + arr[i];
```

因此，它“优化”成与上面简单的求和循环一样了。

这可能看起来是一个微小的权衡，但补偿算术通常用于实现核心数学函数，例如：三角函数和指数函数。允许编译器在这些函数内部重新结合操作数，可能会给出[灾难性的错误结果](https://github.com/JuliaLang/julia/issues/30073#issuecomment-439707503)。

## 四、将次正规数[^译注2]清零

这一点是最微妙的，但无疑是最大的隐患，因为它会影响未使用 fast-math 编译的代码，并且仅在 `-funsafe-math-optimizations` 的文档中隐晦地提了一句：

> 在链接时使用，它可能包含一些会改变默认 FPU 控制字或触发一些其他类似优化的库或启动文件。

这是啥意思？这指的是浮点数相关的那些有点烦人的特殊情况之一 - 次正规数（有时也称为非规格化数）。[维基百科提供了一个比较不错的概述](https://en.wikipedia.org/wiki/Subnormal_number)，但在这里你需要知道的主要是（a）它们非常接近零，以及（b）它们在许多处理器上会造成显著的性能下降[^4]。

解决这一问题的简单方法是“清零”（FTZ，Flush To Zero），即，如果返回的结果是一个非规格化值，则取代之返回零。在很多情况下这是可以接受的，这个做法在音频和图形应用中很常见。但很多场景下它并不适用：FTZ 会破坏一些重要的浮点数误差分析结果，比如 [Sterbenz 引理](https://en.wikipedia.org/wiki/Sterbenz_lemma)，也因此可能出现非预期的结果（比如：迭代算法无法收敛）。

这里我们想要说的问题在于 FTZ 在大多数硬件上的实际实现方式：它不是针对单条指令设置，而是[由浮点环境控制](https://software.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/compiler-reference/floating-point-operations/understanding-floating-point-operations/setting-the-ftz-and-daz-flags.html)的，更具体地来说，它是由浮点控制寄存器控制的，在多数系统中，该寄存器是在线程级别设置的，启用 FTZ 将影响同一线程中的所有其他操作。

GCC 通过 `-funsafe-math-optimizations` 启用 FTZ，即使在构建共享库时也是如此。这意味着仅仅加载一个共享库就可能改变完全不相关的代码的执行结果，这可真是[一种有趣的调试体验](https://github.com/JuliaCI/BaseBenchmarks.jl/issues/253#issuecomment-573589022)。

## 五、程序员能做啥？

我曾在推特上开玩笑地说“别让你的朋友使用 fast-math”，不过严肃地说，我承认它确实有合理的使用场景，也能带来实在的性能提升；随着 SIMD 通道变宽，指令变得更复杂（SIMD lanes get wider and instructions get fancier），这些优化的价值只会增加。至少，它可以为进一步的性能优化提供参考。那么，何时以及如何安全地使用它呢？

如果你并不关心结果的精确性：我来自科学计算领域，这个领域的程序主要输出一堆数字，从业人员也需要关注数值结果的精确性，但是许多其他领域虽然涉及一些浮点计算，但计算结果的精确性实质影响很小，比如：音频、图形、游戏和机器学习这些领域。我对这些领域的要求不太熟悉，不过[20年前 Linus Torvalds 提过一个有趣的抱怨](https://gcc.gnu.org/legacy-ml/gcc/2001-07/msg02150.html),认为过于严格的浮点数语义在科学领域之外几乎无关紧要。尽管如此，[一些轶事](https://twitter.com/supahvee1234/status/1382907921848221698)表明 fast-math 可能会造成问题，所以了解清楚它干了什么以及为什么要这么干，很可能仍然是有用的。如果你在这些领域工作，我很想听听你的经验，特别是如果你发现这些优化中有些会产生积极或消极的影响。

> 我认为，一般而言，对`-ffast-math`可能会或不会做的变换进行防御性编程，基本上是无法解决实质性问题的。如果没能理解编译器的行为，就为编译器提供 `-ffast-math` 选项，相当于赠予你的敌人核武器。但这并不意味着你不能使用它！只是你必须充分测试，以确信在你的系统上编译器不会发生爆炸。
> -- [Matt Bauman](https://discourse.julialang.org/t/when-if-a-b-x-1-a-b-divides-by-zero/7154/5?u=simonbyrne)

如果你确实关心结果的精确性，那你需要小心谨慎地对待 fast-math。一种常见做法是到处启用 fast-math，观察错误结果，然后尝试像处理 bug 一样隔离并修复根因。不幸的是，这个工作并不简单：你无法插入分支来检查 NaN 和 Inf 值（编译器会直接移除它们），你无法依赖调试器，因为 [bug 可能会在调试版本中消失](https://gitlab.com/libeigen/eigen/-/issues/1674#note_709679831)，并且它甚至会[破坏打印功能](https://bugzilla.redhat.com/show_bug.cgi?id=1127544)。

所以你必须谨慎地对待 fast-math。一个典型的过程可能是：

1、开发可靠的验证测试用例（validation tests）
2、开发有用的基准测试（benchmarks）
3、启用 fast-math，并比较基准测试结果
4、有选择地启用/禁用 fast-math 优化项[^5]，以识别：
- 哪些优化会影响性能，
- 哪些会导致问题，
- 以及这些变化在代码中的哪些位置发生。
5、验证最终的数值结果

这一过程的目标应该是在尽可能少的地方使用最少数量的 fast-math 选项，同时通过充分的测试来确保启用优化的代码位置结果仍然是正确的。

或者，你可以考虑其他方法来获得相同的性能提升：在某些情况下，可以通过重写代码来得到相同的结果。例如：许多科学计算代码库中经常可以看到 `x * (1/y)` 这样的表达式。

对于 SIMD 操作，[OpenMP](https://www.openmp.org/spec-html/5.0/openmpsu42.html) 或 [ISPC](https://ispc.github.io/) 这些工具库提供一些结构来编写代码方便实现自动化 SIMD 优化。Julia 提供了 `@simd` 宏，但使用它也有一些重要的注意事项。极端情况下，你也可以使用 [SIMD 内联函数](https://stackoverflow.blog/2020/07/08/improving-performance-with-simd-intrinsics-in-three-use-cases/)，但需要更多的付出和专业知识，并且难以移植到新的平台。

最后，如果你正在编写一个开源库，请不要[在 Makefile 中硬编码 fast-math](https://github.com/tesseract-ocr/tesseract/blob/5884036ecdb2807419cbd21b7ca44b630f547d80/Makefile.am#L140)。

## 六、编程语言和编译器开发者能做啥？

我认为 fast-math 的广泛使用应该被视为一个基础的设计失败：由于未能为程序员提供他们需要的特性来充分利用现代硬件，程序员只好退而求其次去启用一个已知明显不安全的编译选项。

首先，GCC 应该解决 FTZ 库问题：[这个 bug 已经提出9年了，但仍然处于 NEW 标记状态](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55522)。至少，这个行为应该有更清晰的文档说明，并提供一个特定的选项来禁用它。

除此之外，还有2个主要的方法：教育用户，以及提供更精细的优化控制。

教育用户最简单的方法就是给这个编译选项起一个更好的名字。与其叫“fast-math”，不如叫“unsafe-math”。文档也应该改进，让用户快速清晰地了解这些选择带来的后果。例如：代码检查工具和编译器警告信息可以提醒用户代码中的 `isnan` 现在已无用处，或者仅仅高亮显示哪些代码区域受到了优化的影响。

其次，编程语言和编译器需要提供更好的工具来支持完成同样的工作。理想的方式，这些行为不应该通过编译器标志来启用或禁用，这是一个非常粗粒度的工具，而是应该在代码中局部地指定，例如：

- GCC 和 Clang 都允许[以每个函数为单位启用/禁用优化](https://stackoverflow.com/a/40702790/392585)：这些函数粒度的优化方式，应该标准化，然后所有编译器都来支持这个标准。
- 应该提供更精细的控制选项，比如：一个指令（pragma）或宏，以便用户可以断言“在任何情况下都不应该移除这个`isnan`检查 / 这个算术表达式应该重新结合”。
- 与当前设计不同，提供一种机制来标记某些加法或减法操作，告知编译器无论存在什么样的编译器选项都可以重新结合优化（或者合并优化为单个乘加融合算子（contract into a fused-multiply-add operation））[^6]。

这些优化机制的具体语义应该如何，仍然是尚待讨论解决的问题： 如果将一个普通的 `+` 和一个 fast-math 的 `+` 组合使用，它们能否重新结合？ 作用域规则应该是什么样的，以及与跨过程（inter-procedural）优化这类优化应该如何交互？这些问题很困难但非常重要，解决好了，程序员就能够安全地使用这些优化特性。

## 七、补充更新

从我写下这篇笔记以来，有一些更新：

- Brendan Dolan-Gavitt 写了一篇精彩的文章来介绍 [Python 第三方包中启用了 FTZ 的库](https://moyix.blogspot.com/2022/09/someones-been-messing-with-my-subnormals.html)：文中还提供了一些不错的建议，方便确定你使用的库编译时是否启用了 fast-math。
  - 他还对[相关的缓冲区溢出漏洞](https://github.com/moyix/2_ffast_2_furious)做了概念验证（PoC）。
- Clang 在使用 fast-math 构建共享库时也会启用 FTZ，但前提是系统全局安装了 GCC。我已[提交了这个 issue](https://github.com/llvm/llvm-project/issues/57589)。
- MSVC 不会移除 `isnan` 检查，不过在使用 fast-math 编译时[生成了看起来更糟糕的代码](https://twitter.com/dotstdy/status/1567748577962741760)。
- FTZ 库的问题[将在 GCC 13 中修复](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55522#c45)！


[^1]: 显然，GCC 中的 `-fno-math-errno` [会影响 malloc](https://twitter.com/kwalfridsson/status/1450556903994675205)，所以可能并不那么无害。
[^2]: 实际上，可以构造一个数组，以不同方式对数组求和，[几乎可以得到任何浮点数值结果](https://discourse.julialang.org/t/array-ordering-and-naive-summation/1929?u=simonbyrne)。
[^3]: 数值分析中的一个重要结论是：求和的[误差界限与中间求和结果绝对值之和成正比](https://www.google.com/books/edition/Accuracy_and_Stability_of_Numerical_Algo/5tv3HdF-0N8C?hl=en&gbpv=1&pg=PA82&printsec=frontcover)。SIMD 求和将累加操作分散到多个值上，因此通常会得到较小的中间求和结果。
[^4]: [这里有个问答帖子对次正规数为什么会导致性能损耗做了很好的讲解](https://stackoverflow.com/a/54938328)。
[^5]: 如上所述，`-fno-finite-math-only` 应该是首先尝试的选项。
[^6]: Rust 通过[实验性内置函数](https://stackoverflow.com/a/40707111/392585)提供类似的功能，不过我不完全清楚支持哪些优化。
[^译注1]: 【译注1】指某个事件或行动的结果对于后续环节或相关方产生的影响。
[^译注2]: 【译注2】英文单词 subnormals，如果不理解其语义可以参考[什么是次正规数](https://segmentfault.com/q/1010000042733312)。