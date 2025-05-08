---
title: 如何剖析 JVM 应用（译）
date: 2020-07-13
id: how-to-profile-jvm-applications
---

原文：[How to profile JVM applications](https://www.lightbend.com/blog/profiling-jvm-applications)

Hi 大家好。工具团队（tooling team）近期的一个关注点是改进 sbt 贡献流程（ improvement of the contribution process to sbt）。我们一直在思考的另一个事情是 sbt 的性能。为一举解决这两件事情，我调研了 Jason Zaugg、Johannes Rudolph 这些人如何剖析 JVM 应用，这篇文章即是调研结果。

这里论述的技术应该可以应用于Java 和 Scala，也基本与你使用的工具无关。

## 火焰图（使用 async-profiler 生成）

剖析 JVM 应用的方式有多种，但新晋热门是Netflix 高级性能架构师（Senior Performance Architect）Brendan Gregg 发明的**火焰图**。开发者先收集堆栈踪迹抽样数据（stack trace samples），然后将其处理成一张交互式的 svg 图。若要快速了解火焰图，可阅读如下链接资料：

- [Using FlameGraphs To Illuminate The JVM by Nitsan Wakart](https://www.youtube.com/watch?v=ugRrFdda_JQ)
- [USENIX ATC ’17: Visualizing Performance with Flame Graphs](https://www.youtube.com/watch?v=D53T1Ejig1Q)

我推荐的第一个火焰图工具是  Andrei Pangin 发起的 async-profiler，在 macOS 和 Linux 操作系统环境下均可使用，上手使用也更简单。

1. 下载安装器 [async-profiler 1.2](https://github.com/jvm-profiling-tools/async-profiler/releases/tag/v1.2)
2. 假设你的系统中存在一个命令查找路径 `$HOME/bin`，在 `$HOME/bin` 目录下创建符号链接指向 `build/` 和 `profiler.sh`

```bash
ln -s ~/App/async-profiler/profiler.sh $HOME/bin/profiler.sh
ln -s ~/App/async-profiler/build $HOME/bin/build
```

接下来，关闭所有 Java 应用，以及任何可能影响剖析过程的东西，比如 Slack，然后在终端程序（terminal）中运行你的应用。对于我而言，则是尝试剖析 `sbt` 的初始化加载过程：

```text
$ sbt exit
```

在另一个终端中，运行：

```text
$ jps
92746 sbt-launch.jar
92780 Jps
```

由此我们知道应用的进程 ID。对于我而言，目标进程 ID 是 `92746`。在应用运行的同时，运行如下命令：

```text
$ profiler.sh -d 60 <process id>
Started [cpu] profiling
--- Execution profile ---
Total samples:         31602
Non-Java:              3239 (10.25%)
GC active:             46 (0.15%)
Unknown (native):      14667 (46.41%)
Not walkable (native): 3 (0.01%)
Unknown (Java):        433 (1.37%)
Not walkable (Java):   8 (0.03%)
Thread exit:           1 (0.00%)
Deopt:                 9 (0.03%)

Frame buffer usage:    55.658%

Total: 1932000000 (6.11%)  samples: 1932
  [ 0] java.lang.ClassLoader$NativeLibrary.load
  [ 1] java.lang.ClassLoader.loadLibrary0
  [ 2] java.lang.ClassLoader.loadLibrary
  [ 3] java.lang.Runtime.loadLibrary0
  [ 4] java.lang.System.loadLibrary
....
```

命令会输出一大堆有用的堆栈跟踪信息（stacktraces），为将这些信息可视化为一张火焰图，运行如下命令：

```text
profiler.sh -d 60 -f /tmp/flamegraph.svg <process id>
```

命令最后应该会产出文件 `/tmp/flamegraph.svg`。

![flamegraph](https://i.loli.net/2020/07/13/dZVkB9xq2Gy6NfA.png)

你自己来体验一下 [flamegraph.svg](https://downloads.lightbend.com/website/blog/2019/flamegraph.svg?_ga=2.187105832.1642569835.1594378538-197429397.1594378538) 输出的信息。

## 火焰图（使用 perf-map-agent 生成）

虽然 async-profiler 上手使用很简单，但火焰图真正有趣之处在于可以混合展现 JVM 堆栈追踪和原生代码（native code）堆栈跟踪信息，让开发者可以看到 CPU 实际消耗在程序的何处。Lightbend 公司的 Johannes Rudolph 为此写了一个工具 - [perf-map-agent](https://github.com/jvm-profiling-tools/perf-map-agent)。该工具在 macOS 环境下会使用 `dtrace`，在 Linux 环境下会使用 `perf`。如果你想确认瓶颈是否出现在原生代码中，这个工具会特别有用。

我们先要编译 [perf-map-agent](https://github.com/jvm-profiling-tools/perf-map-agent)。对于 macOS 环境，在运行 `cmake .` 之前需要先设置 `JAVA_HOME` 环境变量：

```text
$ cd work
$ git clone https://github.com/brendangregg/FlameGraph.git

$ git clone https://github.com/jvm-profiling-tools/perf-map-agent.git
$ cd perf-map-agent
$ export JAVA_HOME=$(/usr/libexec/java_home)
$ cmake .
-- The C compiler identification is AppleClang 9.0.0.9000039
-- The CXX compiler identification is AppleClang 9.0.0.9000039
...
$ make
```

在一个新的终端中，带 `-XX:+PreserveFramePointer` 标记参数运行 sbt：

```text
$ sbt -J-Dsbt.launcher.standby=20s -J-XX:+PreserveFramePointer exit
```

在另一个终端中运行：

```text
$ cd quicktest/
$ export JAVA_HOME=$(/usr/libexec/java_home)
$ export FLAMEGRAPH_DIR=$HOME/work/FlameGraph
$ jps
94592 Jps
94549 sbt-launch.jar
$ $HOME/work/perf-map-agent/bin/dtrace-java-flames 94549
dtrace: system integrity protection is on, some features will not be available

dtrace: description 'profile-99 ' matched 2 probes
Flame graph SVG written to DTRACE_FLAME_OUTPUT='/Users/xxx/work/quicktest/flamegraph-94549.svg'.
```

理论上这样会产出更全面的火焰图，不过对于 `sbt exit`，产出的火焰图看起来可能有点凌乱。

![flamegraph-2](https://i.loli.net/2020/07/13/s9BbWAnluTPwE3J.png)

如果 sbt 操作已经经过即时编译器优化（the operations are already JITed），或者操作比较特殊（the operation is more specific），那么火焰图的效果会更好。为了得到效果更好的火焰图，我们可以将相同的操作多重复几次：

```text
$ sbt -J-Dsbt.launcher.standby=20s -J-XX:+PreserveFramePointer reload reload reload reload exit
```

这样就可以产出程序的稳定态火焰图，逐步放大火焰图，就可以找到执行的热点路径。

![flamegraph-3](https://i.loli.net/2020/07/13/93YyjxR5ATS1Dnq.png)

## Flamescope

Netflix 公司近期发布了一个新的火焰图可视化工具 - [Flamescope](https://medium.com/netflix-techblog/netflix-flamescope-a57ca19d47bb)，可以将火焰图过滤限制在一个特定的时间范围内。

![FlameScope|400](https://i.loli.net/2020/07/13/djm2hkvC98irQu7.png)

Martin Spier 和 Brendan Gregg 为研究扰动以及其他时间相关的问题（perturbations and other time-based issues）而开发了这个工具。常规的火焰图是聚合了所有堆栈追踪抽样数据，如果系统中发生了一个短时小故障，就会被深埋于其他追踪信息中，这个工具就是为了解决这个问题。

## JMH (sbt-jmh)

因为 JIT 存在预热等特点，增大了基准测试的困难。JMH 会将相同的测试运行多次，消除 JIT 预热等特点造成的影响，从而更准确地测量代码的性能。

对于 sbt 用户而言，Lightbend 公司 Konrad Malawski 编写的 [sbt-jmh](https://github.com/ktoso/sbt-jmh) 进一步简化了 JMH 测试。并且它也集成了 async-profiler。

## VisualVM

我也想提一下传统的 JVM 剖析工具。因为 [VisualVM](https://visualvm.github.io/) 开源了，所以就来说说它。

1、先打开 VisualVM
2、在一个终端中启动 sbt
3、从 VisualVM 界面的 Local 应用目录下应该可以看到 `xsbt.boot.Boot`

![](https://i.loli.net/2020/07/13/EB7vsuo1HjaTtQw.jpg)

4、打开它，选择 抽样功能（sampler） 或 剖析功能（profiler），在你想要开始的时间点点击 CPU 按钮

![](https://i.loli.net/2020/07/13/aoiIDfMKw9pjY4Z.jpg)

如果你对 [YourKit](https://www.yourkit.com/) 比较熟悉，也可以使用它，用法比较相似。

## 总结

火焰图对堆栈跟踪抽样数据进行可视化，方便识别应用代码中的热点路径。也有助于确认代码变更是否实际影响了应用性能。