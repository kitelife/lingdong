# LingDong（灵动）

静态博客生成器，以及其他。

## Road Map

- 生成器
  - Markdown 解析器
    - [x] 多级标题
    - [x] 单行解析（斜体/粗体/删除线/行内代码）
    - [x] 图片
      - [x] 支持设置宽度
    - [x] 列表
      - [x] 有序列表
      - [x] 无序列表
      - [x] 多级列表
    - [x] 代码块
    - [x] 引用块
    - [x] mathjax 数学公式
    - [x] 表格
    - [x] 单行单个 html 元素
    - [x] 脚注
    - [x] 单行文本对齐方式（右对齐，中间对齐）
  - 插件
    - [x] plantuml
    - [x] mermaid
    - [x] smms 图床
    - [x] 基于 typst + [cmarker](https://typst.app/universe/package/cmarker/) 导出 pdf 格式
      - 实现不太理想，存在兼容性问题，导致一些 markdown 文件无法转成 pdf
    - [x] mathjax
    - [x] katex（替换掉 mathjax，支持更多的 latex 能力）
    - [ ] 基于 markdown 抽象语法树，生成 typst 源码，然后编译出 pdf
    - [ ] 将 typst 算法伪代码块转换成图片（png/svg）
    - [ ] 自动配图 - 大模型文生图
  - [x] 默认主题
    - [x] 评论插件
      - [x] [giscus](https://giscus.app/zh-CN)
      - [x] [GitTalk](https://gitalk.github.io/)
  - [ ] 状态缓存
  - [ ] AI 简写
  - [ ] 并行化加速

- [x] 服务器
  - [x] 简化的 HTTP 协议支持
  - [x] 令牌桶限流器
  - [x] 基于 sqlite 的单机数据库存储
  - [ ] 工具列表
    - [x] 静态博客/文件服务
    - [x] base64 编码解码
    - [ ] pv & uv 统计数据
    - [ ] RSS 订阅&阅读器
    - [ ] 短地址服务
    - [ ] 计算器，中缀表示法&前缀表达式&后缀表达式&浮点数&整数&大整数

- [ ] 博文搜索子系统
- [ ] 基于个人行为（点击/停留时间/赞/踩）的 hacker news 推荐小系统

## 致谢

- 前端语法高亮库： https://highlightjs.org/
- 评论插件：https://gitalk.github.io/
- 默认主题引自 https://bitbashing.io/
