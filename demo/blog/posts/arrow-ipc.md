---
id: arrow-ipc
name: Arrow 列存格式-序列化与进程间通信（译）
date: 2024-12-07
---

原文：[Arrow Columnar Format-Serialization and Interprocess Communication (IPC)](https://arrow.apache.org/docs/format/Columnar.html#serialization-and-interprocess-communication-ipc)

## 序列化与进程间通信(IPC)

本列存格式定义中，序列化数据的基本单元是“成批记录（record batch）”。语义上，一个成批记录是若干数组的一个有序集合，一个数组对应一个字段列（field），这些数组的长度相同，但数据类型可能不同。一个成批记录中字段列的名称和类型信息共同形成该批的 *schema*。

本小节，我们将定义一种协议，约定如何将若干记录批序列化成一个二进制载荷的流，以及如何无需内存拷贝就能从这些载荷重建出记录批。

本列存进程间通信协议使用如下这些类型的二进制消息格式来构建一个单向流的定义：

- Schema
- RecordBatch
- DictionaryBatch

这种我们称之为进程间通信密封消息的格式，包含一个经序列化的 Flatbuffer 类型元数据，后接一个可选的消息体。在描述如何序列化如上三种进程间通信消息类型之前，我们先定义清楚这种消息格式。

### 密封消息格式

对于简单的流式序列化和基于文件的序列化，我们为进程间通信定义一种“密封的”消息格式。这种消息，仅需检查消息的元数据，就能“被反序列化”成内存中的 Arrow 数组对象，无需对实际数据进行拷贝或移动。

这种密封二进制消息格式如下所述：

- 一个 32 比特长度的再开始标识。其值为 `0xFFFFFFFF`，表示重新开始一个有效消息。这一部分是在版本 0.15.0 引入的，部分原因是为了解决 Flatbuffers 要求8字节对齐的问题。
- 消息元数据部分的大小，32 比特长度，小端编码。
- 消息元数据，类型为 [Message.fbs]("https://github.com/apache/arrow/blob/main/format/Message.fbs")文件中定义的 `Message` 类型。
- 消息体，其长度必须是8字节的倍数。

语义上，消息格式形如：

```text
<再开始标识: 0xFFFFFFFF>
<元数据大小: int32>
<flatbuffer 序列化的元数据: bytes>
<填充>
<消息体>
```

经序列化的完整消息，长度必须是8字节的倍数，这样消息可以跨多个流实现内存重定位（译注：怎么理解？）。否则，元数据和消息体之间填充量是不确定的。

“元数据大小” 等于 `Message` 类型的大小加上填充的大小。“flatbuffer 序列化的元数据”即是一个 Flatbuffer `Message` 类型的值序列化后的结果，其内部包含如下部分：

- 版本号
- 特定的消息类型值（`Schema`、`RecordBatch`、`DictionaryBatch` 三者之一）
- 消息体的大小
- 应用设置的“自定义元数据”字段。

在读取一个输入流时，通常先解析 `Message` 元数据，经验证后获取到消息体的大小，然后读取消息体。

### Schema 消息

[Schema.fbs]("https://github.com/apache/arrow/blob/main/format/Schema.fbs") 这个 Flatbuffers 文件包含所有内置类型的定义，以及用于表达一个给定成批记录 schema 的 `Schema` 元数据类型。schema 是若干字段列（`Field`）定义的有序序列，每个字段列定义包含列名称和列数据类型。`Schema` 类型的值经序列化后不会包含任何数据缓冲区，仅包含类型元数据。

`Field` 这个 Flatbuffers 类型包含单个数组的的元数据，包括如下信息：

- 字段列的名称
- 字段列的数据类型
- 该字段列语义上是否可以为 null。这个和数组的物理内存布局无关，一些系统会明确区分可为 null 的字段列和不可为 null 的字段列，我们希望保留这个元数据以便完整无缺地表达 schema
- 对于嵌套类型，还包含一组子类型 `Field` 元数据
- 一个名为 `dictionary` 的属性，标识当前字段列是否字典编码过的。如果是的话，会有一个字典“id” 赋值于此，如此便可为这个字段列匹配后续的字典编码的 IPC 消息。

另外，我们还提供 schema 级别和字段列级别的 `custom_metadata` 属性字段，方便应用系统插入自己的应用元数据，以此自定义行为。

### RecordBatch 消息

一个 RecordBatch 消息包含若干实际的数据缓冲区，其物理内存布局由 schema 决定。这种消息的元数据提供了每个缓冲区的位置和大小信息，如此，使用指针计算就能重建出那些数组数据结构，也无需内存拷贝。

成批记录的序列化后形式如下所示：

- “消息头部”部分，定义见 [Message.fbs]("https://github.com/apache/arrow/blob/main/format/Message.fbs") 中的 `RecordBatch` 类型。
- “消息体”部分，若干内存缓冲区的一个平铺序列，依次逐个写入，中间加上适当的填充以确保8字节对齐。

数据头部包含如下信息：

- 成批记录中，每个平铺字段列的长度和 null 值的数量。
- 成批记录消息体中每个“缓冲区”的内存偏移位置和长度。

这些字段列信息和缓冲区是对成批记录中的字段列按照原有顺序进行深度优先遍历平铺得到的。例如，我们来看看如下 schema：

```text
col1: Struct<a: Int32, b: List<item: Int64>, c: Float64>
col2: Utf8
```

其平铺版本如下所示：

```text
FieldNode 0: Struct name='col1'
FieldNode 1: Int32 name='a'
FieldNode 2: List name='b'
FieldNode 3: Int64 name='item'
FieldNode 4: Float64 name='c'
FieldNode 5: Utf8 name='col2'
```

对应生成的缓冲区平铺序列，则如下所示（参考上面的表定义）：

```text
buffer 0: field 0 validity
buffer 1: field 1 validity
buffer 2: field 1 values
buffer 3: field 2 validity
buffer 4: field 2 offsets
buffer 5: field 3 validity
buffer 6: field 3 values
buffer 7: field 4 validity
buffer 8: field 4 values
buffer 9: field 5 validity
buffer 10: field 5 offsets
buffer 11: field 5 data
```

`Buffer` 的 Flatbuffers 值描述了每块内存的位置和大小，按照前文定义的密封消息格式进行解析。

### 可变数量缓冲区（Variadic buffers）

> Arrow 列存格式 1.4 版本新增。

诸如 Utf8View 这些类型，使用不定数量的缓冲区来表现。按照预先顺序拍平的逻辑 schema 中的这类字段列在 RecordBatch 的`variadicBufferCounts` 属性中都对应一个值来表示当前 RecordBatch 中属于那个字段列的缓冲区的数量。

例如，来看看如下 schema：

```text
col1: Struct<a: Int32, b: BinaryView, c: Float64>
col2: Utf8View
```

其中有两个字段列是有可变数量缓冲区的，因此 RecordBatch 的 `variadicBufferCounts` 属性中对应有2个值。若该 schema 的一个 RecordBatch 中 `variadicBufferCounts = [3, 2]`，那么平铺的缓冲区序列如下所示：

```text
buffer 0:  col1    validity
buffer 1:  col1.a  validity
buffer 2:  col1.a  values
buffer 3:  col1.b  validity
buffer 4:  col1.b  views
buffer 5:  col1.b  data
buffer 6:  col1.b  data
buffer 7:  col1.b  data
buffer 8:  col1.c  validity
buffer 9:  col1.c  values
buffer 10: col2    validity
buffer 11: col2    views
buffer 12: col2    data
buffer 13: col2    data
```

### 压缩

对于成批记录的消息体缓冲区内容有3种压缩方式可选：不压缩、使用 `lz4` 压缩、使用 `zstd` 压缩。消息体中平铺的缓冲区序列，每个缓冲区需要使用相同的压缩编码方式单独压缩。压缩处理后的缓冲区序列中某些缓冲区可能没有被压缩（例如，某些缓冲区经压缩后其大小不会明显变小）。

RecordBatch “消息头”中的 `compression` 属性用于标记使用的压缩类型，该属性可选，默认值为不压缩。

对缓冲区进行压缩或不进行压缩，区别之处在：

- 如果 [RecordBatch 消息]("https://arrow.apache.org/docs/format/Columnar.html#ipc-recordbatch-message")中缓冲区经过压缩
  - “消息头”中除了包含成批记录消息体中每个压缩过的缓冲区的大小和内存偏移量之外，还会包含使用的压缩类型。
  - “消息体”包含经过压缩的缓冲区平铺序列，序列中每个缓冲区的起始8个字节存储缓冲区未经压缩时的长度，这个长度是小端字节序编码的64比特有符号整数。如果这个长度为 `-1`，则表示当前 buffer 实际未经压缩。

- 如果 [RecordBatch 消息]("https://arrow.apache.org/docs/format/Columnar.html#ipc-recordbatch-message")中缓冲区未经压缩
  - “消息头”中仅包含成批记录消息体中每个未经压缩缓冲区的大小和内存偏移量。
  - “消息体”则简单地包含未经压缩缓冲区的平铺序列。

### [字节序]("https://en.wikipedia.org/wiki/Endianness")

Arrow 列存格式默认使用小端序字节编码。

Schema 序列化后的元数据中包含一个 `endianness` 属性，表示成批记录使用哪种字节序编码。通常就是生成该 RecordBatch 的系统使用的字节序。该属性的主要用处是确保在使用相同字节序的系统之间传输成批记录数据。如果系统在读取 Schema 时发现字节序和自己不匹配，则应该报错。

### IPC 流式编码格式

我们为成批记录序列提供了一种流式编码协议或者说“格式”，其表现为一个密封消息序列，每个密封消息都遵循前文所属的格式。流中，先放入 schema，后面放入的所有成批记录 schema 都是同一个。如果 schema 中任一字段列使用字典编码，那么流中会包含一个或多个 `DictionaryBatch` 消息。`DictionaryBatch` 消息和 `RecordBatch` 消息可能会交织出现，但是 `RecordBatch` 中使用的所有字典 id 都应该在其前面的 `DictionaryBatch` 消息中定义好。

```text
<SCHEMA>
<DICTIONARY 0>
...
<DICTIONARY k - 1>
<RECORD BATCH 0>
...
<DICTIONARY x DELTA>
...
<DICTIONARY y DELTA>
...
<RECORD BATCH n - 1>
<EOS [optional]: 0xFFFFFFFF 0x00000000>
```

> 注解：
> 字典和数据成批记录交织出现的规则有一个特殊情况 - 如果字典成批记录中的向量完全为空，那么数据列所使用的字典可能会出现首个数据成批记录的后面。

实现一个流读取器，在读取每条消息后，需要先读取接下来的8个字节来确定流是否继续以及下一条消息的元数据大小。一旦读到了消息的 flatbuffer 编码元数据，就可以继续读取消息体部分了。

流写入器，可以写入 4字节的再开始标识（`0xFFFFFFFF`）拼接上4字节的元数据长度 0（`0x00000000`） 来标识流结束（EOS），或者简单关闭流接口。对于流格式，我们推荐使用 “.arrows” 文件扩展名，虽然许多情况下流并不会存为文件。

### IPC 文件格式

我们定义一种支持随机访问的“文件格式”，作为流式编码格式的一种扩展。文件的起始和末尾均是一个魔术字符串 `ARROW1`(加上填充)。起始魔术字符串之后紧跟是流式编码格式的内容，之后在末尾魔术字符串之前，先写入一个尾部（footer） - 包含 schema（流式编码格式的一部分） 的一个拷贝，加上文件中每个数据块的内存偏移量和大小信息。这样就能够随机访问文件中的任一成批记录。可以查看 [File.fbs]("https://github.com/apache/arrow/blob/main/format/File.fbs") 文件了解文件尾部的定义细节。

语义上，文件格式如下所示：

```text
<magic number "ARROW1">
<empty padding bytes [to 8 byte boundary]>
<STREAMING FORMAT with EOS>
<FOOTER>
<FOOTER SIZE: int32>
<magic number "ARROW1">
```

这个文件格式并不要求 `RecordBatch` 中使用的字典 id 要定义在前面的 `DictionaryBatch` 中，主要这些 id 定义在文件的某处即可。此外，每个字典 ID 如果存在多个非增量字典也是无效的（比如：不支持字典覆盖替换）。增量字典按照他们在文件尾部中出现的顺序应用生效。以这种格式创建的文件推荐使用 “.arrow” 文件扩展名。请注意这种格式创建的文件有时也被称为“Feature V2”，使用 “.feature” 文件扩展名，这个名称和扩展名源自“Feature （V1）” - Arrow 项目早期为 Python（Pandas） 和 R 语言的语言无关快速数据框（data frame）存储做的一个概念验证。

另附 - File.fbs 中 Footer 定义：

```text
include "Schema.fbs";

namespace org.apache.arrow.flatbuf;

/// ----------------------------------------------------------------------
/// Arrow 文件元数据
///

table Footer {
  version: org.apache.arrow.flatbuf.MetadataVersion;
  schema: org.apache.arrow.flatbuf.Schema;
  dictionaries: [ Block ];
  recordBatches: [ Block ];
  /// 用户自定义元数据
  custom_metadata: [ KeyValue ];
}

struct Block {
  /// Index to the start of the RecordBlock (note this is past the Message header)
  offset: long;
  /// Length of the metadata
  metaDataLength: int;
  /// Length of the data (this is aligned so there can be a gap between this and
  /// the metadata).
  bodyLength: long;
}

root_type Footer;
```

### 字典编码消息

字典是以成批记录序列的形式写入流或者文件格式的，其成批记录中仅包含单个字段列。因此，一个字典成批记录序列的完整语义 schema 包括所有字典带的 schema。所以必须先从字典成批记录的 schema 中读取字典类型信息，才能正确地对字典数据进行解析翻译：

```text
table DictionaryBatch {
  id: long;
  data: RecordBatch;
  isDelta: boolean = false;
}
```

字典消息元数据中的字典 `id` 可以在数据成批记录的 schema 中被多次引用，因此同一个字典可以被多个数据字段列使用。可以阅读[字典编码内存布局]("https://arrow.apache.org/docs/format/Columnar.html#dictionary-encoded-layout")一节了解字典编码数据的语义。

字典 `isDelta` 标志位允许对前面存在的字典进行扩展，以便支持后续成批记录的解析。如果一个字典成批记录的 `isDelta` 设置为真（true），则表示它的向量数据应该和前面同 id 的字典成批记录拼接在一起。假设对一列数据进行流式编码，该列数据为一个字符串列表 `["A", "B", "C", "B", "D", "C", "E", "A"]`，其增量（delta）字典成批记录的形式可能如下所示：

```text
<SCHEMA>
<DICTIONARY 0>
(0) "A"
(1) "B"
(2) "C"

<RECORD BATCH 0>
0
1
2
1

<DICTIONARY 0 DELTA>
(3) "D"
(4) "E"

<RECORD BATCH 1>
3
2
4
0
EOS
```

或者，如果 `isDelta` 被设置为假（false），那么同 ID 的字典，后面的会覆盖替换前面的。同样使用如上的例子，对应编码形式可能如下所示：


```text
<SCHEMA>
<DICTIONARY 0>
(0) "A"
(1) "B"
(2) "C"

<RECORD BATCH 0>
0
1
2
1

<DICTIONARY 0>
(0) "A"
(1) "C"
(2) "D"
(3) "E"

<RECORD BATCH 1>
2
1
3
0
EOS
```
