---
id: reading-kafka-design
title: 读文笔记：Kafka 官方设计文档
date: 2019-10-13
---

原文：[http://kafka.apache.org/documentation/#design](http://kafka.apache.org/documentation/#design)

## 数据持久化

#### 不用惧怕文件系统

磁盘的读写速度，取决于如何读写。对于线性读写方式，操作系统做了充分的优化：提前读 - 预取若干数据块，滞后写 - 将小的逻辑写操作合并成一个大的物理写操作。

[研究](http://queue.acm.org/detail.cfm?id=1563874)表明：[顺序读写磁盘（sequential disk access）的速度有些时候比随机访问内存还要快](http://deliveryimages.acm.org/10.1145/1570000/1563874/jacobs3.jpg)。

现代操作系统激进地尽可能将空闲内存用作磁盘缓存。所有磁盘读写都经过操作系统提供的统一缓存。这个特性没法轻易关闭，除非直接 I/O （direct I/O），因此，如果程序在用户进程中进行数据缓存，缓存的数据通常也是和操作系统页缓存重复的，缓存两遍，没啥意义，也浪费内存。

而且，Kafka 是构建在 JVM 之上的，了解 Java 内存使用方式的人应该都知道：

1. 对象的内存开销非常高，通常是实际数据大小的2倍（甚至更多）
2. 随着堆上数据量增大，Java 的 GC 表现也会更糟糕

因此，使用文件系统并依赖于操作系统内存页缓存，优于在程序中维护一块内存缓存或其它结构。至少操作系统内存页缓存的可用内存翻倍了。另外，如果使用紧凑的字节结构来缓存数据，相比使用对象，可用内存可能还会翻倍。在 32GB 内存的机器上这么搞，缓存可用到 20-30GB，还不会对 GC 造成了什么坏影响。并且，即使服务重启，这块缓存空间也是热的（除非机器重启），用户进程内的内存缓存在服务重启后得重建（10GB的数据缓存可能需要10分钟左右）。

这样也可以简化代码逻辑，因为缓存和文件系统之间的一致性由操作系统来保证了。

这样一分析，设计就简单了：我们反其道而行之，所有数据都直接写到文件系统上持久化日志文件中，不需要在程序中使用内存缓存，也不必确保将数据刷到磁盘。这实际意味着数据转移到了内核的内存页缓存。

#### 常量时间就能搞定

B 树的 O(log N) 时间复杂度，对于磁盘操作来说，并不能等同于常量时间复杂度。

Kafka 采用日志文件方式，确保读写操作的时间复杂度是 O(1)。

Kafka 不会在消息一被消费就立即删除，而是保留一段时间，这样对于消费者来说也更灵活一些。

## 效率

对于 Kafka 这类系统而言，即使像前述那样消除了糟糕的磁盘访问模式，也会遇到两个导致数据效率低的问题：**过多的小 I/O 操作**，以及**过多的字节拷贝**。

小 I/O 问题在客户端与服务端之间，以及服务端内部的数据持久化操作中都会发生。对此，Kafka 协议建立在 “消息集” （即一批消息）的抽象之上，这样网络请求读写的是一批一批的消息，减少了网络往返的时间开销（注：消息处理的实时性会相对差一点）。服务端也是一次将一批消息写到日志文件中，消费者也按序一次获取一批消息。这一简单的优化可以将吞吐能力提升几个数量级。

对于过多的字节拷贝问题，在消息量大的时候，影响比较明显。Kafka 采用了一种标准化的二进制消息格式，producer、broker、consumer 都使用这种格式，这样数据块在传输期间不需要变动。

broker 维护的消息日志只是一个目录下的一堆文件，文件内容是按序写入的消息集，消息集的数据格式同于 producer、consumer 使用的。共用一种数据格式方便了一个重要的操作优化：持久化日志块的网络传输。对于从内存页缓存（pagecache）到网络套接字（socket）的数据传输操作，现代 UNIX 操作系统提供了一种高度优化的代码执行路径。Linux 中使用 [sendfile 系统调用](http://man7.org/linux/man-pages/man2/sendfile.2.html) 可以利用这个优化。

要理解 sendfile 的收益，需要先理解从文件到套接字传输数据的常规代码执行路径：

1. 操作系统从磁盘将数据读到内核空间的内存页缓存（pagecache）
2. 应用程序从内核空间减数据读到用户空间缓冲区
3. 应用程序将数据从用户空间缓冲区读到内核空间的套接字缓冲区
4. 操作系统将数据从套接字缓冲区读到 NIC 缓冲区，网卡从 NIC 缓冲区读取数据通过网络发出去

这一代码执行路径，涉及 4 次数据拷贝和 2 次系统调用，很显然是低效的。使用 sendfile，可以避免内核空间和用户空间之间一些不必要的数据拷贝，操作系统可以直接将数据从内存页缓存发送到网络。

进一步了解 sendfile 以及 Java 平台如何支持零拷贝，可以阅读[这篇文章](https://developer.ibm.com/articles/j-zerocopy/)。

## 生产者（The Producer）

#### 负载均衡

消息应该发到哪个分区（partition）由客户端根据哈希算法（或者随机）决定，并且消息是直接由 producer 发到目标分区的 leader broker，没有任何中间路由层。

所有 Kafka 节点都可以响应元数据请求 - 告知客户端（producer 或 consumer）哪些服务节点还存活以及某个 topic 的各个分区 leader 分别是哪个节点（疑惑：如果某个分区 leader 节点挂掉之后，客户端如何获知？何时可以获知？）

## 消息交付语义

producer 和 consumer 之间的消息交付语义，分 3 种：

1. 最多消费一次 - 消息可能会丢失，但不会被重复消费
2. 最少消费一次 - 消息不会丢，但可能被重复消费
3. 仅消费一次 - 每个消息都会被消费且仅消费一次

这个问题可以分成两个阶段的问题：**producer 向 broker 发布一个消息时的持久性保证** 以及 **consumer 消费一个消息时的语义保证** （the durability guarantees for publishing a message and the guarantees when consuming a message）。

producer 向 Kafka 集群发消息时，会提供一个请求参数 `acks`：

1. acks=0：表示 producer 不需要等分区 leader broker 返回任何响应，将消息存入套接字缓冲区（socket buffer）就当做消息已经发送成功。所以可靠性是没有保证的。
2. acks=1：表示 分区 leader broker 将消息写入自己的本地日志文件，就向 producer 响应成功，不必等待分区副本 broker 同步好消息。
3. acks=-1 或 acks=all：表示 分区 leader broker 需要等待所有同步副本 broker 同步好消息并响应成功，才向 producer 响应成功

第 2 种情况，如果分区 leader broker 挂掉/不存活，则副本未来得及同步的消息会丢失。

第 3 种情况，只要有同步副本正常同步消息，那么即使 leader 挂了也不会丢数据。

如果 leader 被系统判定为不存活，则会从（同步）副本中选举一个新的 leader，那么 Kafka 如何判定一个节点是否存活？存活判定依赖 2 个条件：

1. 节点必须维持与 Zookeeper 的 session 连接（通过 Zookeeper 的心跳机制）
2. 如果是一个从节点（follower），则必须不断从 leader 节点同步消息数据，且同步进度没有落后太多

如果 producer 在发送消息的过程中发生网络问题，它没法判定分区 leader 是否收到消息。0.11.0.0 版本之前，producer 只能重发消息，别无他法，因此只能提供“最少消费一次的”交付语义。0.11.0.0 版本之后，Kafka producer 支持一个幂等交付功能选项，可以确保消息重发不会导致 Kafka 的消息日志中出现重复的条目：broker 为每个 producer 分配一个 ID，然后基于消息序号来去重。

也是从 0.11.0.0 版本开始，Producer 支持以类事务的语义向多个 topic 分区发送消息：要么所有消息都发送成功，要么都不成功。这个能力主要用于实现 Kafka topic 之间的仅处理一次语义。

从 consumer 角度来看，同一个分区的所有副本，日志数据相同，消费进度也一样。consumer 可以控制自己对分区日志数据的消费位置。

1. 如果 consumer 读取消息后，先向 kafka 提交消费位置，再处理消息；如果该 consumer 挂掉或重启，会可能导致丢消息，从而只能满足“最多处理一次”交付语义。
2. 如果 consumer 读取消息后，是先处理，再提交消费位置；如果该 consumer 挂掉或重启，则可能导致重复消费消息，从而只能满足“最少处理一次”交付语义。

如何实现“仅处理一次”语义？借助 Producer 的事务能力。

## 复制

复制的粒度/单元是 topic 分区。Kafka 集群中，每个分区都有一个 leader broker 节点，0个或多个从节点（follower）。分区读写都是由 leader broker 处理。

如同一个普通的 consumer，从节点从 leader broker 拉取（pull）消息，然后写到自己的消息日志文件中。让从节点以 pull 的方式获取 leader 的消息数据，好处在于批量读写。

对于 follower 节点而言，“是否存活”的实际含义是“是否顺利地从 leader 同步消息”，leader 节点会追踪“同步中”节点集（ISRs）。如果一个 follower 挂掉了/卡住了/同步落后太多了，则将其从这个 ISRs 中移除。follow 是否卡住或者同步落后太多，依据 `replica.lag.time.max.ms` 配置参数判定。

将某消息写到某个分区，如果该分区所有同步中副本都已经将该消息写到自己的消息日志文件中，则可以认为该消息的写操作已提交（committed），也就是真正的写成功。

只有写提交的消息才会分发给 consumer。

producer 可以选择是否等待消息写操作提交，在延迟（latency）和持久性（durability）之间权衡。

Kafka 集群在某分区的 leader 节点挂掉之后，会快速进行失败转移（a short fail-over period），选举出新的分区 leader 节点，可用性不会受到影响。但如果发生网络分区（network partitions）问题，则无法保证可用性。CAP - C（Consistency）：一致性，A（Availability）：可用性，P（Partition Tolerance）：分区容错性 - 放弃了 分区容错性。

#### 日志数据复制：仲裁成员集（Quorums）、同步中副本集（ISRs）和状态机

（备注：这一节我理解得还不太透彻。）

一类常见的分布式系统是主从模式的，由主节点决定状态变化的顺序（the order of a series of values）。从节点通过日志复制（replicated log）方式同步状态数据。对于提交决策（commit decision）和选主（leader election），通常是基于多数人投票的机制。假设副本个数（注：个人理解包含主节点）为 2f+1，那么只有当 f+1 个副本写入成功，主节点才会将这个写操作标记为已提交（committed）。当主节点挂掉之后，基于 f 个状态最新的副本节点，可以选举出新的主节点，且状态不会有任何丢失。

多数人投票方式，有一个优点：延迟取决于速度快的节点，而不是慢的。缺点是：对于实际的生产系统，抗风险能力还不够，而且不够灵活，不能让使用者做权衡。

Kafka 选择仲裁成员集（quorum set）的方式与此不同，而不是基于多数人投票，而是动态维护一组同步中副本（ISR），这些副本与主节点保持同步。只有这组副本中的成员才有资格当选为主节点。ISR 集发生变化时会持久化到 Zookeeper 上。

基于 ISR 模型，如果 topic 分区有 f+1 个副本，则可以容忍 f 个节点挂掉，也不会丢失任何已提交的消息。

与 Kafka ISR 模型实际实现最相近的学术论文是微软的 [PacificA](http://research.microsoft.com/apps/pubs/default.aspx?id=66814)。

#### 可用性和持久性保证

注意：producer 发送消息时设定 `acks=all` 并不是要求所有的副本都确认写入成功，而是在当前同步中副本（ISR）都确认写入成功时，分区 leader 就向 producer 响应成功。例如：某个 topic 被设置为 2 个副本，然后其中一个副本节点挂掉，此时要求 `acks=all` 的写操作也会成功。如果剩下的副本节点也挂了，那么就会丢消息啦。

为了方便用户在 可用性 和 持久性 之间权衡，Kafka 提供两个 topic 级别的配置，用于 持久性 比 可用性 重要的情况：

1. [禁用脏 leader 选举](http://kafka.apache.org/documentation/#design_uncleanleader)
2. 指定一个最小 ISR 集大小（`min.insync.replicas` 参数设置）：只有当 ISR 集大小大于设定的最小值，分区 [leader] 才会接受消息写入。这个设置只有当 producer 使用 `acks=all` 时才会生效。（注：在我们生产环境中，分区副本数通常申请为 3（包含 leader），那么 `min.insync.replicas` 应该设定为 2，但默认是 1。使用 1，那么当分区只有一个副本（即 leader），producer 也能写入成功，但如果这个副本又挂了，就会丢数据。）

#### 副本管理

一个 Kafka 集群上一般会有多个 topic，每个 topic 又有多个 partition，为了节点之间负载均衡，通常以**循环（round-robin）方式**在所有节点上分布 partition 和 分区 leader 角色。

另外，在分区 leader 节点之后重新选出 leader 之前，存在一段不可用的时间窗口，为了缩短这个时间窗口，Kafka 会从所有 broker 中选择一个作为“控制器（controller）”，这个控制器会检测 broker 级别的问题（failures），在发现某个 broker 挂掉之后，负责为受影响的分区指定新的 leader，而不是每个分区自己负责重新选主，这样的选主过程更轻量更快。如果控制器节点挂了，还存活的 broker 中的一个会成为新的控制器。

## 消费者消费进度跟踪

Kafka 为每个消费组（consumer group）指定一个 broker 来存储目标 topic 各个分区的消费进度（offsets），这个 broker 称为 **组协调器（group coordinator）**。这个消费组中的任一消费者实例都应该将消费进度提交到这个组协调器，或者从这个组协调器获取启动之前上次的消费进度。Kafka 基于消费组的名称为消费组分配协调器。消费者可以向任一 broker 发送 FindCoordinatorRequest 请求来查找自己的协调器，并从 FindCoordinatorResponse 响应中获取协调器的详细信息。

在组协调器接收到一个 OffsetCommitRequest 请求后，会将请求数据写到一个特殊的[经压实的（compacted）](http://kafka.apache.org/documentation/#compaction) Kafka topic - *__consumer_offsets*。在目标分区的所有副本都确认收到了，协调器才会向消费者发送进度提交成功的响应。这个 topic 的消息日志数据会定期进行压实（compact），因为只需要为每个分区维护最新的消费进度。协调器也会在内存中缓存消费进度，方便快速响应消费进度查询请求。

注：如果消费者/消费组特别多（例如：我们广告引擎服务，读取正排消息 topic，一个机器实例就是一个 consumer group，数量在几百到几千不等），那么组协调器的压力会比较大，那么确保组协调器的角色均匀分配到集群的所有 broker，比较关键。另外，*__consumer_offsets* 这个 topic 的分区数量不能太少，最好和 broker 数量相同或者整数倍数量。