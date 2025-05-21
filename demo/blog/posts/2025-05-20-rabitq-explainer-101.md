---
title: RaBitQ 二值量化入门（译）
date: 2025-05-20
id: rabitq-explainer-101
---

原文:  [RaBitQ binary quantization 101](https://www.elastic.co/search-labs/blog/rabitq-explainer-101)

## 一、引言(Introduction)

正如我们之前在 [标量量化入门](https://blog.xiayf.cn/posts/scalar-quantization-101.html) 中讨论的那样，大多数嵌入模型输出$float32$类型向量值，这个精度对于表征向量空间来说通常过于冗余。标量量化技术大大减少了表征这些向量所需的存储空间。之前我们也讨论过 [Elasticsearch 中的比特向量](https://www.elastic.co/search-labs/blog/bit-vectors-in-elasticsearch)，以及二值量化导致的损失通常是不可接受的。

借助 [RaBitQ 论文](https://arxiv.org/pdf/2405.12497)中提出的二值量化技术，可以解决将数据简单量化为比特向量（bit vector）过程中所遇到的问题，通过更细致地划分空间以及保留变换的残差实现与标量量化相近的精度质量。相比其他类似技术，比如[乘积量化(PQ)](http://hal.archives-ouvertes.fr/docs/00/51/44/62/PDF/paper_hal.pdf)，这些新技术能够实现性能更优的距离计算，通常得到的计算结果也更准，也能实现标量量化通常不可能实现的32倍压缩（32x level of compression）。

本文中，我们将解释二值量化的一些核心要素，相关的数学细节请参阅 [RaBitQ 论文](https://arxiv.org/pdf/2405.12497)。

## 二、构建比特向量

对于距离计算，可以更高效地预计算某些部分，所以我们对索引构建和查询构造两个环节区分处理。

以三个非常简单的二维向量 $v_1$、$v_2$和$v_3$ 的索引构建（indexing）为例，看看如何对他们进行转换和存储，实现查询时的高效距离计算。

$$v_1=[0.56,0.82]$$
$$v_2=[1.23,0.71]$$
$$v_3=[-3.28,2.13]$$

我们的目标是将这些向量转换为更小的表征形式，支持：

- 以合理的形式快速地估算向量距离
- 也确保空间中的向量分布能够将召回实际的最近邻所需要处理的数据向量总数降下来

可以通过以下方式实现：

- 将每个向量变换移动到超球体内部，对于我们的例子来说，超球体是一个二维圆，单位圆
- 将每个向量分别吸附（snapping）到圆内某个区域的单个代表性点上
- 保留校正因子以便更准确地近似计算每个向量与查询向量之间的距离

下面分步骤解析这个过程。

## 三、找一个代表性质心

为了划分每一维，需要选择一个枢轴点（pivot point）。为了简化，将选择一个点来转换我们所有的数据向量。

对于示例向量$v_1$、$v_2$和$v_3$，选择它们的质心作为枢轴点。

$$v_{1}=[0.56,0.82]$$
$$v_{2}=[1.23,0.71]$$
$$v_{3}=[-3.28,2.13]$$
$$c=[(0.56+1.23+-3.28)/3,(0.82+0.71+2.13)/3]$$
$$c=[-0.49, 1.22]$$

将所有这些点一起绘制出来：

![rabitq-101-pic-1|600](../assets/rabitq-101-pic-1.webp)
>图 1：基于示例向量及其派生质心绘制的二维平面图。

将残差向量做归一化后，分别命名为 $v_{c1}$、$v_{c2}$和$v_{c3}$。

$$
\begin{array}{l}
v_{c1}=(v_{1}-c) /\| v_{1}-c\|\\
v_{c2}=(v_{2}-c) /\| v_{2}-c\|\\
v_{c3}=(v_{3}-c) /\| v_{3}-c\|
\end{array}
$$

对其中一个向量做数学运算：

$$v_{c1}=(v_{1}-c)/\| v_{1}-c\|$$

$$\begin{aligned}
v_{1}-c&=[0.56,0.82]-[-0.49,1.22]\\
&=[1.05,-0.39]
\end{aligned}$$

$$
\| v_{1}-c\| =1.13
$$

$$\begin{aligned}
v_{c1}&=(v_{1}-c)/\| v_{1}-c\|\\
&=([1.05,-0.39])/\| [1.05,-0.39]\|\\
&=([1.05,-0.39])/1.13\\
&=[0.94,-0.35]
\end{aligned}$$

对另外两个向量做同样的数学运算，结果分别为：

$$
\begin{aligned}v_{c2}&=\left[ 0.96,-0.28 \right]\\ v_{c3}&=\left[ -0.95,0.31 \right]\end{aligned}
$$

下面这个动态图，可以方便我们形象地理解这个转换和归一化过程。

![rabitq-101-pic-2|600](../assets/rabitq-101-pic-2.gif)
>图 2：示例向量及其派生质心在单位圆内的变换动画。

## 四、仅使用 1 比特

将数据向量集中和归一化之后，可以应用标准的[二值量化](https://www.elastic.co/search-labs/blog/bit-vectors-in-elasticsearch)编码 - 对于经过转换的向量的每个分量，如果其为负数，则编码为0，正数则编码为1。在我们的二维示例中，即将单位圆分成四个象限，$v_{c1}$、$v_{c2}$和$v_{c3}$对应的二值向量分别变为$r_1=[1,0],r_2=[1,0],r_3=[0,1]$。

最后将每个数据向量分别吸附（snapping）到对应区域的一个代表性点上，确切来说，这些代表性点为单位圆上与每个轴距离相等的点：$\pm \frac{1}{\sqrt{d}}$ [^1]

将经过的量化的三个向量分别表示为$\overline{v}_1$、$\overline{v}_2$和$\overline{v}_3$。

那么，以 $v_{c1}$  为例，将其吸附到对应区域中的代表性点 $r_1$，则得到：

$$\begin{aligned}
\overline{v}_1&=\frac{1}{\sqrt{d}} \left( 2r_{1}-1 \right)\\
&=\frac{1}{\sqrt{2}} [1,-1]\\
&=\left[ \frac{1}{\sqrt{2}} ,-\frac{1}{\sqrt{2}} \right]
\end{aligned}$$

如下是 $v_2$和$v_3$ 这2个原始数据向量（译注：也就是 $v_{c2}$ 和 $v_{c3}$）的量化形式：

$$\begin{aligned}
\overline{v}_2&=\left[ \frac{1}{\sqrt{2}} ,-\frac{1}{\sqrt{2}} \right]\\
\overline{v}_3&=\left[ -\frac{1}{\sqrt{2}} ,\frac{1}{\sqrt{2}} \right]
\end{aligned}$$

如 [RaBitQ 论文](https://arxiv.org/pdf/2405.12497) 所述，选取的这些代表性点具备一些不错的数学性质，与[乘积量化（PQ）](http://hal.archives-ouvertes.fr/docs/00/51/44/62/PDF/paper_hal.pdf) 的码本类似（not unlike）。

![rabitq-101-pic-3|600](../assets/rabitq-101-pic-3.gif)
>图 3：二值量化后的向量吸附到对应区域中代表性点的过程。

至此，对于向量的每个分量都有一个1比特的近似值，尽管直接用于计算比较距离有些不太准确（somewhat fuzzy）。显然，在当前这个量化状态下，$\overline{v}_1$和$\overline{v}_2$是相同的，这样不太理想，问题类似于之前讨论将浮点数向量编码为 [ElasticSearch 中的比特向量](https://www.elastic.co/search-labs/blog/bit-vectors-in-elasticsearch) 是说的。

这种方法的优雅之处在于，查询时可以使用类似于点积的东西计算近似距离来快速比较每个数据向量和每个查询向量。后文中讨论处理查询时会详细介绍。

## 五、问题与方案

如上所述，将浮点数向量转换为比特向量时会丢失大量信息。我们需要一些额外的信息来帮助补偿这种损失以及校正距离估算。

为了恢复保真度（fidelity），我们将每个向量到质心的距离以及向量（例如$v_{c1}$）与其量化形式（例如$\overline{v}_1$）的投影（点积）存储为两个$float32$值。

到质心的欧式距离比较简单直观，之前在量化每个向量时已经计算过：

$$\begin{aligned}
\left\| v_{1}-c \right\|&=1.13\\
\left\| v_{2}-c \right\|&=1.79\\
\left\| v_{3}-c \right\|&=2.92
\end{aligned}$$

预先计算每个数据向量到质心的距离可以恢复向量的中心化变换。类似地，我们将计算查询向量到质心的距离。直观上，质心充当了一个中介，而不是直接计算查询向量和数据向量之间的距离。

向量与其量化向量的点积是：

$$\begin{aligned}
v_{c1}\cdot \bar{v}_{1}&=v_{c1}\cdot \frac{1}{\sqrt{2}} \left( 2r_{1}-1 \right)\\
&=[0.94,-0.35]\cdot \left[ \frac{1}{\sqrt{2}} ,-\frac{1}{\sqrt{2}} \right]\\
&=0.90\end{aligned}
$$

另外两个向量的结果为：

$$\begin{aligned}
v_{c2}\cdot \overline{v}_2&=0.95\\
v_{c3}\cdot \overline{v}_3&=0.89
\end{aligned}$$

量化向量与原始向量的点积，作为第二个校正因子，捕捉了量化向量与其原始位置的距离。[RaBitQ 论文](https://arxiv.org/pdf/2405.12497) 的 3.2 节说明量化数据与查询向量之间以朴素方式（in a naive fashion）计算的点积存在偏差。这个因子正好补偿了它。

我们做这种量化转换是为了减少数据向量的总大小并降低向量比较的开销。虽然在我们二维示例中，这些校正因子看似很大，但随着向量维度的增加，它们会变得微不足道。例如，一个 1024 维的向量，如果以$float32$形式存储，需要占用 4096 字节。如果以这种比特压缩和校正因子形式存储，只占用 136 字节。

若想更好地理解我们为什么使用这些因子，请参阅 [RaBitQ 论文](https://arxiv.org/pdf/2405.12497)，它详细介绍了涉及的数学知识。

## 六、查询向量

$$q=[0.68, -1.72]$$

为了能够将我们的量化数据向量与查询向量进行比较，首先必须将查询向量相对于单位圆进行偏移，转换为量化形式。我们将查询向量称为$q$，转换后的向量称为$q_c$，标量量化后的向量称为$\overline{q}$。

$$\begin{aligned}
q-c&=[(0.68- -0.49),(-1.72-1.22)]\\
&=[1.17,-2.95]
\end{aligned}$$

$$\begin{aligned}
q_{c}&=(q-c)/\| q-c\|\\
&=[1.17,-2.95]/3.17\\
&=[0.37,-0.92]
\end{aligned}$$

接下来，我们将查询向量标量量化到4个比特，我们将这个向量称为$\overline{q}$。请注意，我们不是直接量化到比特表示，而是使用一个$int4$标量量化，$\overline{q}$作为 $int4$ 字节数组，用于估计距离。我们可以利用这种非对称量化在不增加额外存储的情况下保留更多信息。

$$lower = -0.92$$
$$upper = 0.37$$

$$\begin{aligned}
width&=\left( upper-lower \right) /\left( 2^{4}-1 \right)\\
&=(0.37--0.92)/15\\
&=0.08
\end{aligned}$$

$$\begin{aligned}
\bar{q}&=\lfloor (q_{c}-lower )/width \rfloor\\
&=\lfloor ([0.37,-0.92]-[-0.92,-0.92])/0.08\rfloor\\
&=[15,0]
\end{aligned}$$

![rabitq-101-pic-4|600](../assets/rabitq-101-pic-4.gif)
>图 4：对查询向量应用质心转换的过程。

如您所见，因为我们只有两个维度，我们的量化查询向量由处于$int4$取值范围内两个值组成。对于更长的向量，你会看到各种 $int4$ 值，其中有一个是取值范围的最大值，还有一个是最小值。

现在可以执行距离计算，对每个索引数据向量与这个查询向量做比较 - 将量化数据向量每一维分别与量化查询向量的对应维相乘，再将所有维的乘积相加。基本上，就是一个普通的点积，但使用的是比特值和字节值（bits and bytes）。

$$\begin{aligned}
\overline{q} \cdot r_{1}&=\left[ 15,0 \right] \cdot \left[ 1,0 \right]\\
&=15
\end{aligned}$$

接着应用校正因子来解开（unroll）量化，从而得到一个更准确的距离估计。

要实现这一点，我们要从量化查询向量中拿到取值范围的上下界（对查询向量做标量量化时衍生出来的）。此外，还需要查询向量到质心的距离。由于前面计算过查询向量与质心之间的距离，此处引用过来即可：

$$\|q-c\| = 3.17$$

## 七、估算距离

好的！我们已经对索引数据向量和查询向量做了量化并收集了校正因子。现在可以计算$v_1$和$q$之间的估计距离了。我们将欧几里得距离计算公式转换为一个具有更多计算友好项的方程：

$$\begin{aligned}dist(v_{1},q)&=\| v_{1}-q\|\\
&=\sqrt{\| (v_{1}-c)-(q-c)\|^{2}}\\
&=\sqrt{\| v_{1}-c\|^{2} +\| q-c\|^{2} -2\times \| v_{1}-c\| \times \| q-c\| \times (q_{c}\cdot v_{c1})}
\end{aligned}$$

这种形式中，多数因子，我们之前推导过，比如 $\|v_1−c\|$，明显可以在查询之前提前计算好，或者也不是直接对查询向量和任一给定数据向量（比如 $v_1$） 做比较计算。

然而我们仍然需要计算$q_c⋅v_{c1}$.。可以利用校正因子和二值量化距离度量$\overline{q}⋅r_1$来合理且快速地估算这个值。下面我们来详细解释一下。

$$q_{c}\cdot v_{c1}\approx (q_{c}\cdot \overline{v}_{1} )/(v_{c1}\cdot \overline{v}_{1} )$$

先估算$q_c⋅\overline{v}_1$，这个估算公式本质上是使用之前定义的代表点来解开（unroll）转换：

$$q_{c}\cdot \overline{v}_{1} \approx (lower+width\cdot \overline{q} )\cdot (\frac{1}{\sqrt{d}} (2r_{1}-1))$$

具体来说，$(\frac{1}{\sqrt{d}} (2r_{1}-1)$将二值化的数值映射回代表点，$lower+width\cdot \overline{q}$ 则撤销了用于计算标量量化查询向量部分的移位和缩放（shift and scale）。

可以将这个估算公式重写为更利于计算的形式，如下所示。

不过，我们先定义几个辅助变量：

- 将 $r_1$中 1 的总位数定义为$\overline{v}_{b1}$，当前例子中等于 $1$
- 将 $\overline{q}$[^2]中所有量化值的总和（total number）定义为$\overline{q}_b$，当前例子中等于 $15$

$$\begin{aligned}
q_{c}\cdot \overline{v}_{1}&\approx \frac{2\times \textit{width}}{\sqrt{d}} \times (\overline{q} \cdot r_{1})+\frac{2\times \textit{lower}}{\sqrt{d}} \times \overline{v}_{b1} -\frac{\textit{width}}{\sqrt{d}} \times \overline{q}_{b} -\sqrt{d} \times \textit{lower}\\
&\approx \frac{2\times 0.08}{\sqrt{2}} \times 15+\frac{2\times -0.92}{\sqrt{2}} \times 1-\frac{0.08}{\sqrt{2}} \times 15-\sqrt{2} \times -0.92\\
&\approx 0.92
\end{aligned}$$

利用这个值和$v_{c1}⋅\overline{v}_1$（在对数据向量建索引时预先计算好），代入公式，计算$q_c\cdot v_{c1}$的近似值：

$$\begin{aligned}
q_{c}\cdot v_{c1}&\approx (q_{c}\cdot \overline{v}_{1} )/(v_{c1}\cdot \overline{v}_{1} )\\
&\approx 0.92/0.90\\
&\approx 1.01
\end{aligned}$$

最后，将这个近似值代入前面那个更大的距离公式中（注意：我们使用的是$q_c⋅v_{c1}$的估计值）：

$$\begin{aligned}
dist(v_{1},q)&=\sqrt{\| v_{1}-c\|^{2} +\| q-c\|^{2} -2\times \| v_{1}-c\| \times \| q-c\| \times (q_{c}\cdot v_{c1})}\\ est\_ dist(v_{1},q)&=\sqrt{1.13^{2}+3.17^{2}-2\times 1.13\times 3.17\times 1.01}
\end{aligned}$$

应用所有校正因子后，我们得到了两个向量之间距离的一个合理的估计值。例如，当前例子中，原始数据向量$v_1$、$v_2$、$v_3$ 与$q$  之间的估计距离与真实距离的比较如下：

$$\begin{aligned}
est\_ dist(v_{1},q)&=2.02\\
est\_ dist(v_{2},q)&=1.15\\
est\_ dist(v_{3},q)&=6.15
\end{aligned}$$

$$\begin{aligned}
eucl\_ dist(v_{1},q)&=2.55\\
eucl\_ dist(v_{2},q)&=2.50\\
eucl\_ dist(v_{3},q)&=5.52
\end{aligned}$$

关于线性代数应用时如何推导或简化的详细信息，请参阅 [RaBitQ 论文](https://arxiv.org/pdf/2405.12497)。

## 八、重新排序

从上一节的结果可以看出，这些估计距离确实是估计值。二值量化产出的向量，对其计算出来的向量距离，即使应用额外的校正因子，也仅仅是原始向量之间距离的近似值。实验证明，通过引入一个多阶段过程，可以实现高召回率。这证实了[RaBitQ 论文](https://arxiv.org/pdf/2405.12497)中的发现（findings）。

因此，为了获得高质量的结果，二值量化召回的结果必须使用更精确的距离计算进行重新排序。实际应用中，这个候选子集可以很小，通常使用 100 个或更少的候选者即可实现大型数据集（>1m）>95%的召回率。

使用 RaBitQ，中间结果在搜索操作过程中会持续重新排序。在我们的实验中，为了实现更可扩展的二值量化，我们将重排序步骤解耦出来。虽然 RaBitQ 能够在搜索过程中通过重新排序来维护更好的前$N$候选列表，代价是需要不断加载完整的$float32$向量，这对于生产环境中一些较大的数据集来说是不可行的。

## 九、总结

哇！你顺利地读完了！这篇博客确实很长。对于这个新算法我们非常兴奋，它可以缓解乘积量化（例如码本构建成本、距离估计慢等）的许多痛点，并提供出色的召回率和检索速度。

[^1]: 译注：没看懂这个公式 😅
[^2]: 译注：原文有误，将 $\overline{q}$ 写成了 $\overline{q}_b$