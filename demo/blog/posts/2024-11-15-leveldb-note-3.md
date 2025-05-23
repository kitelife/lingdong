---
title: 读码：LevelDB - Compaction 流程
date: 2024-11-15
id: leveldb-note-3
---

## 6、$level_0$ & $level_1$~$level_n$

### 6.1 $level_0$

$level_0$ ldb 数据文件，是将内存中不可变 memtable 的数据落到磁盘生成的。**这些数据文件的 key 范围会有重合**。leveldb 会记录每个 ldb 文件对应的 key 最小值和最大值等元信息。

```cpp
class Version {
private:
  // List of files per level  
  std::vector<FileMetaData*> files_[config::kNumLevels];
}

struct FileMetaData {  
  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) {}  
  
  int refs;  
  int allowed_seeks;  // Seeks allowed until compaction  
  uint64_t number;  
  uint64_t file_size;    // File size in bytes  
  InternalKey smallest;  // Smallest internal key served by table  
  InternalKey largest;   // Largest internal key served by table  
};
```

$level_0$ ldb 文件的数据内容源自：遍历“不可变 memtable” 中跳表的 $L_0$ 层节点，插入“data 块”。跳表的 $L_0$ 层节点链表本就是有序的，第一个节点的 key 即为 最小 key，最后一个节点的 key 即为 最大 key。

Get key 检索时，对于 $level_0$ 层，先对所有 ldb 文件的最小 key 最大 key 进行比较，匹配到所有目标 ldb 文件，并按文件 id 序号从大到小排序（<u>文件 id 序号越大，说明文件越新</u>），从最新的目标 ldb 文件开始检索，找到了就返回。

为了保障检索效率，leveldb 会通过 major compaction 过程尽量控制住 level0 的文件数：

- 如果$level_0$ 文件数超过 kL0_CompactionTrigger（=4），就可以触发 compaction。
- 如果 $level_0$ 文件数超过 kL0_SlowdownWritesTrigger（=8），就会对写入进行限速和攒批。
- 如果 $level_0$ 文件数超过 kL0_StopWritesTrigger（=12），写入就会停顿，等待 compaction。

### 6.2 $level_1$~$level_n$

$level_1$~$level_n$ 的数据文件是从 $level_0$ 开始逐层 compaction 而来，这些文件：

- 同一层内的文件之间 key 的区间不会出现重合，这样可以减少磁盘读取，提升检索效率；不同层的文件之间 key 区间可以重合。 <u>compaction 的逻辑需要保障这个性质</u>。
- $level_k$ 文件的内容一定比 $level_{k-1}$ 文件的内容旧，所以如果在 $level_{k-1}$ 层检索有了结果，就不必到 $level_k$ 层检索。
- 在 compaction 过程中对于同一个 key 会尽可能丢掉旧的键值数据，以此，减少磁盘占用，提升检索效率。

## 7、Compaction

compaction 过程，作为任务，由一个独立的后台线程来执行。

compaction 主要分两 2 种：

- 1、“minor compaction” - 内存中的 “不可变 memtable” 转存到 $level_0$ 文件。这个 compaction 优先级更高，所以一旦检测到存在“不可变 memtable”就优先处理。
- 2、“major compaction” - $level_0$ ~ $level_n$ 逐层之间的 compaction。其中 $level_0$ 与 $level_1$ ~ $level_n$ 不同，$level_0$ 数据文件之间 key 区间存在重合，处理逻辑上存在特殊之处。

“minor compaction” 流程比较简单直接，后续不细说。

### 7.1 major compaction 触发条件

```cpp title:"compaction触发的基础条件"
// We prefer compactions triggered by too much data in a level over  
// the compactions triggered by seeks.  
const bool size_compaction = (current_->compaction_score_ >= 1);  
const bool seek_compaction = (current_->file_to_compact_ != nullptr);
```

```cpp title:"compaction_score_的计算逻辑"
void VersionSet::Finalize(Version* v) {  
  // Precomputed best level for next compaction  
  int best_level = -1;  
  double best_score = -1;  
  
  for (int level = 0; level < config::kNumLevels - 1; level++) {  
    double score;  
    if (level == 0) {  
      // We treat level-0 specially by bounding the number of files  
      // instead of number of bytes for two reasons:
      // 
      // (1) With larger write-buffer sizes, it is nice not to do too
      // many level-0 compactions.
      //
      // (2) The files in level-0 are merged on every read and
      // therefore we wish to avoid too many files when the individual
      // file size is small (perhaps because of a small write-buffer
      // setting, or very high compression ratios, or lots of
      // overwrites/deletions).
      score = v->files_[level].size() /  
              static_cast<double>(config::kL0_CompactionTrigger);  
    } else {  
      // Compute the ratio of current size to size limit.  
      const uint64_t level_bytes = TotalFileSize(v->files_[level]);  
      score =  
          static_cast<double>(level_bytes) / MaxBytesForLevel(options_, level);  
    }  
  
    if (score > best_score) {  
      best_level = level;  
      best_score = score;  
    }  
  }  
  
  v->compaction_level_ = best_level;  
  v->compaction_score_ = best_score;  
}

static double MaxBytesForLevel(const Options* options, int level) {  
  // Note: the result for level zero is not really used since we set  
  // the level-0 compaction threshold based on number of files.  
  // Result for both level-0 and level-1
  double result = 10. * 1048576.0;  
  while (level > 1) {  
    result *= 10;  
    level--;  
  }  
  return result;  
}
```

leveldb 打开/初始化期间或者每次 compaction 结束时都会计算下次 compaction 最应该处理的哪个 level 的数据文件以及紧迫程度（`compaction_score_`），不过对于 $level_0$ 和 其他 level 的计算逻辑有区别：

- 1、$level_0$ compaction 紧迫程度，取决于文件数和 compaction 阈值（kL0_CompactionTrigger = 4）的倍数
- 2、其他 level 则取决于文件占用的存储空间和阈值的倍数，level 越大，阈值越大 - $level_1$ 阈值为 `10. * 1048576.0` 字节（10MB），相邻层之间的阈值为 10 倍关系。

```cpp title:"file_to_compact_计算逻辑"
bool Version::UpdateStats(const GetStats& stats) {  
  FileMetaData* f = stats.seek_file;  
  if (f != nullptr) {  
    f->allowed_seeks--;  // allowed_seeks 初始值 1 << 30
    if (f->allowed_seeks <= 0 && file_to_compact_ == nullptr) {  
      file_to_compact_ = f;  
      file_to_compact_level_ = stats.seek_file_level;  
      return true;  
    }  
  }  
  return false;  
}
```

每次 Get 检索的最后都会对本次检索涉及的<u>第一个数据文件</u>进行计数，如果文件的检索次数达到上限，就伺机进行 compaction。不过这个条件触发 compaction 的优先级比较低。

### 7.2 major compaction 过程

不考虑一些细节优化之处，compaction 的核心流程为：

- 1、根据触发条件，确定本次 compaction 对哪个 level 的数据库文件处理，然后进一步确定从 level  和 level+1 层中选择哪些数据库文件 compaction：
    - （1）、最简单的情况是， 如果该 level 是第一次做 compaction，则选择该 level 的第一个数据文件（也就是最先生成的那个），不过：
        - 1)、如果之前该 level 做过 major compaction，暂存了状态（处理到哪个最大 key），那么根据暂存状态最大 key 选择下一个数据文件即可，如果没有下一个数据文件了，则从头选择该 level 的第一个数据文件（也就是最先生成的那个）。
        - 2)、`seek_compaction` 触发的 compaction 是针对特定数据库文件的。
    - (2)、如果 level = 0，因为 $level_0$ 的数据库文件之间 key 的范围可以重合，所以根据 (1) 选定数据文件的 key 最大值和最小值，还需要进一步确定是否应该把 $level_0$ 的其他数据库文件包含进来。
    - (3)、compaction 输出的数据库属于 level+1 层，因为 $level_1$ ~ $level_n$ 同一层的数据库文件之间 key 区间不能有重合，所以还需要根据 (1) 和 (2) 选定的数据库文件的 key 最大值和最小值，确定 level+1 层有哪些数据库文件需要加入到本次 compaction

```cpp title:"Compaction元信息封装"
// A Compaction encapsulates information about a compaction.
class Compaction {
private:
  int level_;  // compaction 目标 level
  uint64_t max_output_file_size_;  // compaction 产生的新文件大小阈值，如果达到阈值，就结束 compaction
  Version* input_version_;  // 当前对哪个 Version 做 compaction？
  VersionEdit edit_; // 每次 compaction 会产生一个新的 Version

  // Each compaction reads inputs from "level_" and "level_+1"
  // compaction 只会在目标层和相邻下一层之间进行
  std::vector<FileMetaData*> inputs_[2];  // The two sets of inputs

  // State used to check for number of overlapping grandparent files  
  // (parent == level_ + 1, grandparent == level_ + 2)  
  std::vector<FileMetaData*> grandparents_;  // 用于判断是否可以将 level 层的文件直接移动到 level+1 层，见 bool Compaction::IsTrivialMove() const
  size_t grandparent_index_;  // Index in grandparent_starts_  
  bool seen_key_;             // Some output key has been seen  
  int64_t overlapped_bytes_;  // Bytes of overlap between current output  
                            // and grandparent files

  // State for implementing IsBaseLevelForKey  
  
  // level_ptrs_ holds indices into input_version_->levels_: our state  
  // is that we are positioned at one of the file ranges for each  
  // higher level than the ones involved in this compaction (i.e. for  
  // all L >= level_ + 2).  
  size_t level_ptrs_[config::kNumLevels];
}
```

```cpp title:"PickCompaction-选择哪些数据文件做compaction"
Compaction* VersionSet::PickCompaction() {  
  Compaction* c;  
  int level;  
  
  // We prefer compactions triggered by too much data in a level over  
  // the compactions triggered by seeks.
  const bool size_compaction = (current_->compaction_score_ >= 1);
  const bool seek_compaction = (current_->file_to_compact_ != nullptr);  
  if (size_compaction) {  
    level = current_->compaction_level_;  
    assert(level >= 0);  
    assert(level + 1 < config::kNumLevels);  
    c = new Compaction(options_, level);  
  
    // Pick the first file that comes after compact_pointer_[level]  
    for (size_t i = 0; i < current_->files_[level].size(); i++) {  
      FileMetaData* f = current_->files_[level][i];  
      if (compact_pointer_[level].empty() ||  
          icmp_.Compare(f->largest.Encode(), compact_pointer_[level]) > 0) {  
        c->inputs_[0].push_back(f);  // 选择该 level 上次 compaction 的文件的下一个文件
        break;  
      }  
    }  
    if (c->inputs_[0].empty()) {  
      // Wrap-around to the beginning of the key space  
      c->inputs_[0].push_back(current_->files_[level][0]);  // 如果该 level 整个 key 空间都 compaction 过，则从头开始选择第一个文件
    }  
  } else if (seek_compaction) {  
    level = current_->file_to_compact_level_;  
    c = new Compaction(options_, level);  
    c->inputs_[0].push_back(current_->file_to_compact_);  
  } else {  
    return nullptr;  
  }  
  
  c->input_version_ = current_;  
  c->input_version_->Ref();  
  
  // Files in level 0 may overlap each other, so pick up all overlapping ones  
  if (level == 0) {  
    InternalKey smallest, largest;  
    GetRange(c->inputs_[0], &smallest, &largest);  
    // Note that the next call will discard the file we placed in  
    // c->inputs_[0] earlier and replace it with an overlapping set
    // which will include the picked file.
    current_->GetOverlappingInputs(0, &smallest, &largest, &c->inputs_[0]);  
    assert(!c->inputs_[0].empty());  
  }  
  
  SetupOtherInputs(c);  
  
  return c;  
}
```

- 2、如果选定的输入文件，仅有一个文件，且是 level 层文件，则直接将这个文件标记在为本次 compaction 后为 level+1 层文件，从而避免了不必要的文件读写。
- 3、否则，对选定好的 level 和 level+1 层数据库文件，打开文件，创建好数据遍历读取的迭代器：
    - (1)、单个文件的遍历读取实际是个两层遍历（`TwoLevelIterator`） - 第1层遍历针对“index 块”，第2层遍历针对“data 块”，根据“index 块”得到“data 块”的 offset 和 size，遍历“data 块”，获取实际的键值对数据，一个“data 块” 遍历完了，则从“index 块”获取下一个“data 块”的 offset 和 size 继续遍历。
    - （2）、因为 compaction 输出的数据库文件内容也需要保持按 key 有序的性质，所以 compaction 过程实际是对多个输入文件的有序遍历。不过 $level_0$ 数据库文件的 key 区间可以重合，所以处理方式也有所区别
        - 1)、$level_0$ 层的多个数据库文件有序遍历，类似于 N 路归并排序算法 - 每次取 N 个迭代器的头部元素进行比较，取最小的那个元素返回。
        - 2)、$level_1$ ~ $level_n$ 层的数据库文件 key 区间不会有重合，所以比较简单，按照 key 区间先做一下排序，然后逐个文件遍历读取就可以。

```cpp title:"MakeInputIterator-为compaction的多个输入文件创建遍历迭代器"
Iterator* VersionSet::MakeInputIterator(Compaction* c) {  
  ReadOptions options;  
  options.verify_checksums = options_->paranoid_checks;  
  options.fill_cache = false;  
  
  // Level-0 files have to be merged together.  For other levels,  
  // we will make a concatenating iterator per level.  // TODO(opt): use concatenating iterator for level-0 if there is no overlap  
  const int space = (c->level() == 0 ? c->inputs_[0].size() + 1 : 2);  
  Iterator** list = new Iterator*[space];  
  int num = 0;  
  for (int which = 0; which < 2; which++) {  
    if (!c->inputs_[which].empty()) {  
      if (c->level() + which == 0) {  
        const std::vector<FileMetaData*>& files = c->inputs_[which];  
        for (size_t i = 0; i < files.size(); i++) {  
          list[num++] = table_cache_->NewIterator(options, files[i]->number,  
                                                  files[i]->file_size);  
        }  
      } else {  
        // Create concatenating iterator for the files from this level  
        list[num++] = NewTwoLevelIterator(  
            new Version::LevelFileNumIterator(icmp_, &c->inputs_[which]),  
            &GetFileIterator, table_cache_, options);  
      }  
    }  
  }  
  assert(num <= space);  
  Iterator* result = NewMergingIterator(&icmp_, list, num);  
  delete[] list;  
  return result;  
}
```

- 4、leveldb 基于的 lsm-tree（Log Structured Merge Tree） 结构，并不会对数据进行原地修改和删除 - 同 key 的多次写入（包括删除），都会在结构中增加一条键值记录，检索时以最后一次写入的内容为结果。那么如果系统 workloads 会对相同的 key 存在大量写入，就比较浪费存储空间。所以 compaction 过程中会对同一个 key 多次写入做消除合并，仅保留最后一次写入的（如果最后一次是删除操作，并且 level+2（包括）之后所有层的 key 区间都不会包含这个 key，则最后一次的删除操作也可以消除掉）。

> compaction 的输入遍历是有序的，那么同一个 key 的多次写入记录在遍历时是连续的，并且`InternalKeyComparator`比较器的比较逻辑决定了后写入的会被先遍历到。以如下写入序列来解释消除合并逻辑（以 UK 表示用户写入的原始 key，以$IK_k$ 表示带写入序列号的内部 key）：
>> - 假设写入序列为：“PUT $IK_1$” -> “PUT $IK_2$”，那么 compaction 遍历序列为“PUT $IK_2$” -> “PUT $IK_1$”。
>>>  - (1) 处理 “PUT $IK_2$” 时，发现前面没处理过 UK 的记录，则保留 “PUT $IK_2$”记录写入输出中，同时设置状态位 - 处理过 UK 的记录
>>>  - (2) 处理 “PUT $IK_1$” 时，发现前面处理过 UK 的记录（并且这条记录不再被任何快照依赖），则直接将 “PUT $IK_1$” 记录丢弃。
>> - 假设写入序列为：“PUT $IK_1$” -> “DEL $IK_2$”，那么 compaction 遍历序列为 “DEL $IK_2$” -> “PUT $IK_1$”。
>>>  - (1) 处理“DEL $IK_2$” 时，发现前面没处理过 UK 的记录，
>>>>   - 1) 如果 level+2（包括）之后所有层也都不包含 UK 的记录（并且这条记录不再被任何快照依赖），则直接丢弃（<u>从检索性能上来说，可能不丢弃更好一点？</u>）
>>>>   - 2) 如果 level+2（包括）之后层中包含 UK 的记录，则保留 “DEL $IK_2$”记录写到输出中，同时设置状态位  - 处理过 UK 的记录
>>>  - (2) 处理“PUT $IK_1$” 时，发现前面处理过 UK 的记录，则直接将 “PUT $IK_1$” 记录丢弃。
>> - 如果发现当前处理的 IK 的 UK 与前一次处理的 IK 的 UK 不相同，则重置状态位。

```cpp title:"当前读取到的key是否可以被丢弃的判断逻辑"
// Handle key/value, add to state, etc.  
bool drop = false;  
if (!ParseInternalKey(key, &ikey)) {  
  // Do not hide error keys  
  current_user_key.clear();  
  has_current_user_key = false;  
  last_sequence_for_key = kMaxSequenceNumber;  
} else {  
  if (!has_current_user_key ||  
      user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=  
          0) {  
    // First occurrence of this user key  
    current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());  
    has_current_user_key = true;  
    last_sequence_for_key = kMaxSequenceNumber;  
  }  
  
  if (last_sequence_for_key <= compact->smallest_snapshot) {  
    // Hidden by an newer entry for same user key  
    drop = true;  // (A)  
  } else if (ikey.type == kTypeDeletion &&  
             ikey.sequence <= compact->smallest_snapshot &&  
             compact->compaction->IsBaseLevelForKey(ikey.user_key)) {  
    // For this user key:  
    // (1) there is no data in higher levels
    // (2) data in lower levels will have larger sequence numbers
    // (3) data in layers that are being compacted here and have
    //     smaller sequence numbers will be dropped in the next
    //     few iterations of this loop (by rule (A) above).
    // Therefore this deletion marker is obsolete and can be dropped.
    drop = true;  
  }  
  
  last_sequence_for_key = ikey.sequence;  
}
```

```cpp title:"内部key比较器"
int InternalKeyComparator::Compare(const Slice& akey, const Slice& bkey) const {  
  // Order by:  
  //    increasing user key (according to user-supplied comparator)
  //    decreasing sequence number
  //    decreasing type (though sequence# should be enough to disambiguate)
  int r = user_comparator_->Compare(ExtractUserKey(akey), ExtractUserKey(bkey));  // 先比较用户 key 部分
  if (r == 0) {  // 如果用户key 相同，则比较序列号，序列号大的排在前面
    const uint64_t anum = DecodeFixed64(akey.data() + akey.size() - 8);  
    const uint64_t bnum = DecodeFixed64(bkey.data() + bkey.size() - 8);  
    if (anum > bnum) {  
      r = -1;  
    } else if (anum < bnum) {  
      r = +1;  
    }  
  }  
  return r;  
}
```

- 5、如果 compaction 输出的数据文件大小达到阈值（2MB），则结束该文件的写入，并创建一个新的 ldb 数据文件，继续 compaction 写入。在 compaction 完成时，这些若干个输出的 ldb 数据文件会被标记为新版本的 level+1 层数据文件。

```cpp
// Leveldb will write up to this amount of bytes to a file before  
// switching to a new one.  
// Most clients should leave this parameter alone.  However if your  
// filesystem is more efficient with larger files, you could  
// consider increasing the value.  The downside will be longer  
// compactions and hence longer latency/performance hiccups.  
// Another reason to increase this parameter might be when you are  
// initially populating a large database.  
size_t max_file_size = 2 * 1024 * 1024;
```

- 6、一次 compaction 完成后，输入文件的有效内容都已经写到输出文件，如果不考虑其他因素，这些输入文件可以被删除，保留输出文件即可。不过可能存在检索操作依赖这些输入文件，所以不会即刻删除这些输入文件，而是以版本信息在逻辑上维护每一层最新包含哪些文件（以及如果本次 compaction 未能完整地完成，则还需要把处理到 level 的哪个 key 为止记录到版本信息中）。所以<u>每次 compaction 都会产生一个新的逻辑上的数据版本，将这个版本切换为 leveldb 运行时依赖的当前版本（`VersionSet::current_`），后续的检索操作就都在这个新版本所包含数据库文件中进行</u>。另外：
    - 1、重新计算下次 compaction 最应该处理的哪个 level 的数据文件以及紧迫程度（`compaction_score_`）
    - 2、将新版本信息写入版本描述 MANIFEST 文件中（如果尚不存在，则新建一个），并且如果版本描述 MANIFEST 文件是新创建的，则将该  MANIFEST 文件名存为 CURRENT 文件。这样，如果当前 leveldb 进程挂了或者机器节点宕机，还可以基于 CURRENT 文件指向的  MANIFEST 文件中的版本信息，恢复出最新最新版本状态。
    - 3、对当前不再依赖（依赖关系怎么维护的？）的 ldb 文件/MANIFEST 文件/“WAL log” 文件进行删除清理。
        - 不被任何版本依赖的 ldb 文件，都可以删除
        - 不被当前 CURRENT 文件指向的 MANIFEST 文件，可以删除
        - “WAL log” 文件保留最新的两个即可（memtable 和不可变 memtable 对应的“WAL log”文件）

```cpp title:"InstallCompactionResults-compaction结束时记录状态版本"
Status DBImpl::InstallCompactionResults(CompactionState* compact) {  
  mutex_.AssertHeld();  
  Log(options_.info_log, "Compacted %d@%d + %d@%d files => %lld bytes",  
      compact->compaction->num_input_files(0), compact->compaction->level(),  
      compact->compaction->num_input_files(1), compact->compaction->level() + 1,  
      static_cast<long long>(compact->total_bytes));  
  
  // Add compaction outputs  
  compact->compaction->AddInputDeletions(compact->compaction->edit());  
  const int level = compact->compaction->level();  
  for (size_t i = 0; i < compact->outputs.size(); i++) {  
    const CompactionState::Output& out = compact->outputs[i];  
    compact->compaction->edit()->AddFile(level + 1, out.number, out.file_size,  
                                         out.smallest, out.largest);  
  }  
  return versions_->LogAndApply(compact->compaction->edit(), &mutex_);  
}
```

```cpp title:"废弃文件的删除清理逻辑"
void DBImpl::RemoveObsoleteFiles() {  
  mutex_.AssertHeld();  
  
  if (!bg_error_.ok()) {  
    // After a background error, we don't know whether a new version may  
    // or may not have been committed, so we cannot safely garbage collect.
    return;  
  }  
  
  // Make a set of all of the live files  
  std::set<uint64_t> live = pending_outputs_;  
  versions_->AddLiveFiles(&live);  
  
  std::vector<std::string> filenames;  
  env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose  
  uint64_t number;  
  FileType type;  
  std::vector<std::string> files_to_delete;  
  for (std::string& filename : filenames) {  
    if (ParseFileName(filename, &number, &type)) {  
      bool keep = true;  
      switch (type) {  
        case kLogFile:  
          keep = ((number >= versions_->LogNumber()) ||  
                  (number == versions_->PrevLogNumber()));  
          break;  
        case kDescriptorFile:  
          // Keep my manifest file, and any newer incarnations'  
          // (in case there is a race that allows other incarnations)
		  keep = (number >= versions_->ManifestFileNumber());  
          break;  
        case kTableFile:  
          keep = (live.find(number) != live.end());  
          break;  
        case kTempFile:  
          // Any temp files that are currently being written to must  
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());  
          break;  
        case kCurrentFile:  
        case kDBLockFile:  
        case kInfoLogFile:  
          keep = true;  
          break;  
      }  
  
      if (!keep) {  
        files_to_delete.push_back(std::move(filename));  
        if (type == kTableFile) {  
          table_cache_->Evict(number);  
        }  
        Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),  
            static_cast<unsigned long long>(number));  
      }  
    }  
  }  
  
  // While deleting all files unblock other threads. All files being deleted  
  // have unique names which will not collide with newly created files and
  // are therefore safe to delete while allowing other threads to proceed.
  mutex_.Unlock();  
  for (const std::string& filename : files_to_delete) {  
    env_->RemoveFile(dbname_ + "/" + filename);  
  }  
  mutex_.Lock();  
}
```

## 8、版本

### 8.1 MANIFEST - descriptor log 文件

如前所述，数据版本变更信息（VersionEdit）会持久化存储到 MANIFEST 文件中。MANIFEST 文件内容的编码结构同 WAL log。

数据版本变更信息包含8个部分：
- 1、用户自定义 key 比较器的名称（如果用户配置的话）
- 2、最新 WAL log 文件的 id
- 3、前一个 WAL log 文件的 id
- 4、下一个可用的文件 id
- 5、下一个可用的序列号
- 6、每个 level 最近一次的 compaction 暂存信息（compaction 遍历的最后一个 key）
- 7、每个 level 可以删除的数据文件的元数据（compaction 的输入文件）
- 8、每个 level 新增的数据文件的元数据（compaction 的输出文件）

```cpp title:"版本信息编码"
// Tag numbers for serialized VersionEdit.  These numbers are written to  
// disk and should not be changed.  
enum Tag {  
  kComparator = 1,  
  kLogNumber = 2,  
  kNextFileNumber = 3,  
  kLastSequence = 4,  
  kCompactPointer = 5,  
  kDeletedFile = 6,  
  kNewFile = 7,  
  // 8 was used for large value refs  
  kPrevLogNumber = 9  
};

void VersionEdit::EncodeTo(std::string* dst) const {  
  if (has_comparator_) {  
    PutVarint32(dst, kComparator);  
    PutLengthPrefixedSlice(dst, comparator_);  
  }  
  if (has_log_number_) {  
    PutVarint32(dst, kLogNumber);  
    PutVarint64(dst, log_number_);  
  }  
  if (has_prev_log_number_) {  
    PutVarint32(dst, kPrevLogNumber);  
    PutVarint64(dst, prev_log_number_);  
  }  
  if (has_next_file_number_) {  
    PutVarint32(dst, kNextFileNumber);  
    PutVarint64(dst, next_file_number_);  
  }  
  if (has_last_sequence_) {  
    PutVarint32(dst, kLastSequence);  
    PutVarint64(dst, last_sequence_);  
  }  
  
  for (size_t i = 0; i < compact_pointers_.size(); i++) {  
    PutVarint32(dst, kCompactPointer);  
    PutVarint32(dst, compact_pointers_[i].first);  // level  
    PutLengthPrefixedSlice(dst, compact_pointers_[i].second.Encode());  
  }  
  
  for (const auto& deleted_file_kvp : deleted_files_) {  
    PutVarint32(dst, kDeletedFile);  
    PutVarint32(dst, deleted_file_kvp.first);   // level  
    PutVarint64(dst, deleted_file_kvp.second);  // file number  
  }  
  
  for (size_t i = 0; i < new_files_.size(); i++) {  
    const FileMetaData& f = new_files_[i].second;  
    PutVarint32(dst, kNewFile);  
    PutVarint32(dst, new_files_[i].first);  // level  
    PutVarint64(dst, f.number);  
    PutVarint64(dst, f.file_size);  
    PutLengthPrefixedSlice(dst, f.smallest.Encode());  
    PutLengthPrefixedSlice(dst, f.largest.Encode());  
  }  
}
```

### 8.2 CURRENT 文件

CURRENT 文件中存储的是最新有效的 MANIFEST 文件名。

数据库打开，恢复状态的流程为：
- 1、从 CURRENT 中获取最新有效的 MANIFEST 文件名
- 2、从最新有效的 MANIFEST 文件中读取到<u>最后一个 VersionEdit 版本信息</u>
- 3、根据 VersionEdit 版本信息，计算出最新可用的 Version 信息（仅包含每个 level 包含哪些数据文件），并计算该版本下一次 compaction 应该处理哪个 level 的数据文件（`compaction_level_`）和紧迫程度（`compaction_score_`）
- 4、将最新可用的 Version 加入到 `versions_`（`VersionSet`） 的版本链中，并恢复  `versions_` 的一些状态信息，以及确定是否复用 CURRENT指向的那个 MANIFEST 文件（如果 MANIFEST 文件大小超过阈值（`size_t max_file_size = 2 * 1024 * 1024`） 则不复用，而是创建一个新的）
- 5、根据从“最后一个 VersionEdit 原始版本信息” 恢复到 `versions_` 的“前一个 WAL log 文件 id”- `min_log` 和“最新一个 WAL log 文件 id” - `prev_log`，<u>将文件 id 大于等于 `min_log` 或者等于 `pre_log` 的所有 WAL log 文件找出来，按照文件 id 从小大到排序， 然后顺序遍历解析，恢复出 memtable 内存状态</u>，并在最后确保所有内存状态持久化落到 $level_0$ 文件中。
    - “将文件 id 大于等于 `min_log` 或者等于 `pre_log` 的所有 WAL log 文件找出来，按照文件 id 从小大到排序， 然后顺序遍历解析，恢复出 memtable 内存状态”：这个逻辑也意味着并**不需要**在创建一个新的 WAL log 文件时就产生一个新的版本，**也不需要**把最新版本信息持久化写到  MANIFEST 文件 中。因此也不会和 compaction 版本变更存在操作冲突。

### 8.3 VersionEdit & VersionSet & Version

```cpp title:VersionEdit
class VersionEdit {
public:
  // Add the specified file at the specified number.  
  // REQUIRES: This version has not been saved (see VersionSet::SaveTo)  
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file  
  void AddFile(int level, uint64_t file, uint64_t file_size,  
               const InternalKey& smallest, const InternalKey& largest) {  
    FileMetaData f;  
    f.number = file;  
    f.file_size = file_size;  
    f.smallest = smallest;  
    f.largest = largest;  
    new_files_.push_back(std::make_pair(level, f));  
  }  
  
  // Delete the specified "file" from the specified "level".  
  void RemoveFile(int level, uint64_t file) {  
    deleted_files_.insert(std::make_pair(level, file));  
  }

private:
  typedef std::set<std::pair<int, uint64_t>> DeletedFileSet;  
  
  std::string comparator_;  
  uint64_t log_number_;  
  uint64_t prev_log_number_;  
  uint64_t next_file_number_;  
  SequenceNumber last_sequence_;  
  bool has_comparator_;  
  bool has_log_number_;  
  bool has_prev_log_number_;  
  bool has_next_file_number_;  
  bool has_last_sequence_;  
  
  std::vector<std::pair<int, InternalKey>> compact_pointers_;  
  DeletedFileSet deleted_files_;  
  std::vector<std::pair<int, FileMetaData>> new_files_;
}
```

VersionEdit 是 Version 版本信息处于编辑/变更的状态，是 Version 版本信息操作的一种日志结构，比如：
- compaction 过程涉及对最新版本的两个 level 可删除文件和新增文件的记录，以及 compaction 暂存状态的记录。
-  持久化记录到 MANIFEST 文件中，用于恢复数据库的状态。

```cpp title:VersionSet
class VersionSet {
public:
  VersionSet(const std::string& dbname, const Options* options,  
           TableCache* table_cache, const InternalKeyComparator*);

  // Apply *edit to the current version to form a new descriptor that  
  // is both saved to persistent state and installed as the new  
  // current version.  Will release *mu while actually writing to the file.  
  // REQUIRES: *mu is held on entry.  
  // REQUIRES: no other thread concurrently calls LogAndApply()  
  Status LogAndApply(VersionEdit* edit, port::Mutex* mu)  
      EXCLUSIVE_LOCKS_REQUIRED(mu);

  // Recover the last saved descriptor from persistent storage.  
  Status Recover(bool* save_manifest);

  // Return the current version.  
  Version* current() const { return current_; }  
  
  // Return the current manifest file number  
  uint64_t ManifestFileNumber() const { return manifest_file_number_; }  
  
  // Allocate and return a new file number  
  uint64_t NewFileNumber() { return next_file_number_++; }

  // Pick level and inputs for a new compaction.  
  // Returns nullptr if there is no compaction to be done.  
  // Otherwise returns a pointer to a heap-allocated object that  
  // describes the compaction.  Caller should delete the result.  
  Compaction* PickCompaction();

  // Create an iterator that reads over the compaction inputs for "*c".  
  // The caller should delete the iterator when no longer needed.  
  Iterator* MakeInputIterator(Compaction* c);

private:
  Env* const env_;  
  const std::string dbname_;  
  const Options* const options_;  
  TableCache* const table_cache_;  
  const InternalKeyComparator icmp_;  
  uint64_t next_file_number_;  
  uint64_t manifest_file_number_;  
  uint64_t last_sequence_;  
  uint64_t log_number_;  
  uint64_t prev_log_number_;  // 0 or backing store for memtable being compacted  
  
  // Opened lazily  
  WritableFile* descriptor_file_;  
  log::Writer* descriptor_log_;  
  Version dummy_versions_;  // Head of circular doubly-linked list of versions.  
  Version* current_;        // == dummy_versions_.prev_  
  
  // Per-level key at which the next compaction at that level should start.  
  // Either an empty string, or a valid InternalKey.  
  std::string compact_pointer_[config::kNumLevels];
}
```

VersionSet 对象维护着版本双向链表以及其他必要信息，用于支持 compaction 操作、对 $level_0$~$level_n$ 的检索操作 等。

```cpp title:Version
class Version {
public:
  // Lookup the value for key.  If found, store it in *val and  
  // return OK.  Else return a non-OK status.  Fills *stats.  
  // REQUIRES: lock is not held  
  Status Get(const ReadOptions&, const LookupKey& key, std::string* val,  
             GetStats* stats);

private:
  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  VersionSet* vset_;  // VersionSet to which this Version belongs  
  Version* next_;     // Next version in linked list  
  Version* prev_;     // Previous version in linked list  
  int refs_;          // Number of live refs to this version  
  
  // List of files per level  
  std::vector<FileMetaData*> files_[config::kNumLevels];  
  
  // Next file to compact based on seek stats.  
  FileMetaData* file_to_compact_;  
  int file_to_compact_level_;  
  
  // Level that should be compacted next and its compaction score.  
  // Score < 1 means compaction is not strictly needed.  These fields  
  // are initialized by Finalize().  
  double compaction_score_;  
  int compaction_level_;
}
```

compaction 的输入文件在 compaction 之后本来是可以删除的，但是<u>这些文件可能被检索/遍历等操作所依赖引用</u>，所以基于 Version 版本来解决文件粒度的并发操作冲突。只有当一个版本不被任何操作所引用时，才会被释放，只有这个版本依赖的数据文件也才可以被删除。

和 leveldb 中很多类实现的对象生命周期维护方式一样， Version 也采用引用计数的方式来维护生命周期，如果版本对象的引用计数归零，则自动析构自己，析构的逻辑包括从版本双链表中移除自己（这里可能存在并发冲突吗？），以及移除对各 level 文件元信息的引用。

```cpp title:"Version的引用和引用释放"
void Version::Ref() { ++refs_; }  
  
void Version::Unref() {  
  assert(this != &vset_->dummy_versions_);  
  assert(refs_ >= 1);  
  --refs_;  
  if (refs_ == 0) {
    delete this;  
  }  
}

Version::~Version() {  
  assert(refs_ == 0);  
  
  // Remove from linked list  
  prev_->next_ = next_;  
  next_->prev_ = prev_;  
  
  // Drop references to files  
  for (int level = 0; level < config::kNumLevels; level++) {  
    for (size_t i = 0; i < files_[level].size(); i++) {  
      FileMetaData* f = files_[level][i];  
      assert(f->refs > 0);  
      f->refs--;  
      if (f->refs <= 0) {  
        delete f;  
      }  
    }  
  }  
}
```