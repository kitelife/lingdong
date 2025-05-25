---
id: reading-the-log-article
title: 读文：日志 - 每个软件工程师都应该了解的实时数据统一抽象
date: 2019-10-10
---

原文：[The Log: What every software engineer should know about real-time data's unifying abstraction](https://engineering.linkedin.com/distributed-systems/log-what-every-software-engineer-should-know-about-real-time-datas-unifying)

一句话概括，这篇文章细说了 Kafka 的本质原理、解决的问题、适用性等。

Kafka 本质上是提供日志数据流。

日志是客观世界的事件记录。

> A log is perhaps the simplest possible storage abstraction. It is an append-only, totally-ordered sequence of records ordered by time.

日志数据的特点是：只增不改，自带时间戳，数据存储的先后顺序即（大致）是实际发生的时间先后顺序。

数据库可以基于日志来还原历史操作行为，并最终生成最新状态，主从同步就是这么干的。

对于分布式系统而言，日志可以解决 2 个问题：按序改变状态和分发数据（ordering changes and distributing data）。

状态机复制原则：

> If two identical, deterministic processes begin in the same state and get the same inputs in the same order, they will produce the same output and end in the same state.

分布式系统中各个节点可以依据日志来同步状态，达到（最终）一致性。并且，可以依据节点处理到哪行日志即可确定/表达该节点的状态。

（日志）事件流（events）和数据表（tables）是一体两面（a facinating duality）：数据表的变更操作即是一个日志事件流，基于日志事件流可以生成数据表，并将其状态不断更新到最新，数据表的状态是日志事件流的在某个时间点的切面。

> events -> table -> events = events <-> table

> The magic of the log is that if it is a complete log of changes, it holds not only the contents of the final version of the table, but also allows recreating all other versions that might have existed. It is, effectively, a sort of backup of every previous state of the table.

源码版本控制系统（比如 git）也是基于日志实现的分布式系统，一次 commit 相当于一次日志记录。

---

对于互联网/金融等行业的公司来说，数据是重要资产，如何尽可能发挥数据的潜在价值为公司增收，至关重要。因为管理、技术上的原因，公司通常分多个业务部门，各业务部门提供若干服务，各个服务都会产出数据，这些数据很可能需要跨部门跨服务流通，流通的速度越快，周期越短，收益越大。

以前，数据的处理方式主要是批处理，并不是因为没有流处理的技术，而是数据流通的基础设施跟不上，没做到持续的数据流。**（注：这个说法，我个人只部分认同，很多时候，批处理的时延和收益可以满足大部分需求，实时流处理的边际效益可能并不明显）**

流式处理是批处理的泛化形式（stream processing is a generalization of batch processing, and, given the prevalence of real-time data, a very important generalization）。

为了避免因数据流通导致各个服务之间的直接耦合，新增一个统一的数据通道中间服务，各个服务只管对数据通道进行写入或读出，不用关心数据是哪个服务写入的，或者哪些服务在消费/使用自己产出的数据。**（解耦）**

另外，消费者消费数据的速率可能不一样，也可能会经历异常重启等情况，让消费者来控制速率，并且多个消费者之间不会相互干扰，会更好。

基于数据流通的需求和日志的理念，Linkedin 设计开发了 Kafka。

因为日志数据量可能会很大，日志数据本质上是有序串行的，如果支持数据分片，分片之间并行消费，分片内日志数据全局有序，数据流通的吞吐能力就可以无限扩展。

---

基于 Kafka 这类数据管道，服务之间可以实现多级串联。（注：我们现在做的服务就是这么干）

这种分布式系统架构中，至少涉及 producer（生产者）、broker（中间人）、consumer（消费者）三个角色，角色之间在某些工作上如何分工也是值得思考的：

生产者产生的数据应该是什么样的？ - 统一格式/编码、方便解析

中间人（kafka）需要解决什么问题？- 对于单个生产者写入的数据，保证按写入顺序有序地分发给消费者；解决数据高可用，高吞吐能力；支持回溯/重复消费（因此数据需要保留指定时间长度），从而消费者出问题后可以从头消费数据恢复状态。因为支持很多消费者消费同一个数据流，所以平均下来，Kafka 服务的成本会比较低。

消费者按各自的需求进行数据转换存储。

---

对于实现高吞吐能力，除了分片，Kakfa 还充分利用了攒批处理：生产者可以批量发送，中间人将数据攒批写入磁盘日志文件 等等。

此外，由于涉及大量的磁盘文件和网络之间数据读写，Kafka 还充分利用操作系统内核的零拷贝传输能力。