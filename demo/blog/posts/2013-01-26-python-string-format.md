---
title: Python 格式字符串（译）
date: 2013-01-26
id: python-string-format
---

原文：[Python String Format](http://mkaz.com/solog/python-string-format)

每次使用Python的格式字符串（string formatter），2.7及以上版本的，我都会犯错，并且有生之年，我想我都理解不了它们的文档。我非常习惯于更老的 `%` 方法。所以着手编写自己的格式字符串手册。若你有一些其他好的示例请告知我。

## 格式字符串手册

**数字格式化**

下面的表格展示了使用 Python 的后起新秀 `str.format()` 格式化数字的多种方法，包含浮点数格式化与整数格式化示例。可使用 `print("FORMAT".format(NUMBER));` 来运行示例，因此你可以运行： `print("{:.2f}".format(3.1415926));` 来得到第一个示例的输出。

| 数字         | 格式 | 输出 | 描述 |
|------------| --- | -- | --- |
| 3.1415926  | `{:.2f}` | `3.14` | 保留小数点后两位 |
| 3.1415926  | `{:+.2f}` | `+3.14` | 带符号保留小数点后两位 |
| -1         | `{:+.2f}` | `-1.00` | 带符号保留小数点后两位 |
| 2.71828    | `{:.0f}` | `3` | 不带小数 |
| 5          | `{:0>2d}` | `05` | 数字补零 (填充左边, 宽度为2) |
| 5          | `{:x<4d}` | `5xxx` | 数字补x (填充右边, 宽度为4) |
| 10         | `{:x<4d}` | `10xx` | 数字补x (填充右边, 宽度为4) |
| 1000000    | `{:,}` | `1,000,000` | 以逗号分隔的数字格式 |
| 0.25       | `{:.2%}` | `25.00%` | 百分比格式 |
| 1000000000 | `{:.2e}` | `1.00e+09` | 指数记法 |
| 13         | `{:10d}` | &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;13 | 右对齐 (默认, 宽度为10) |
| 13         | `{:<10d}` | 13&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | 左对齐 (宽度为10) |
| 13         | `{:^10d}` | &nbsp;&nbsp;&nbsp;&nbsp;13&nbsp;&nbsp;&nbsp;&nbsp; | 中间对齐 (宽度为10) |

## `string.format()` 基础

如下是两个基本字符串替换的示例，符号 `{}`
是替换变量的占位符。若没有指定格式，则直接将变量值作为字符串插入。

```python
s1 = "so much depends upon {}".format("a red wheel barrow")
s2 = "glazed with {} water beside the {} chickens".format("rain", "white")
```

你也可以使用变量的位置数值，在字符串中改变它们，进行格式化时，会更加灵活。如果搞错了顺序，你可以轻易地修正而不需要打乱所有的变量。

```python
s1 = " {0} is better than {1} ".format("emacs", "vim")
s2 = " {1} is better than {0} ".format("emacs", "vim")
```

## 更老的格式字符串符号"%"

Python 2.6 之前，格式字符串的使用方法相对更简单些，虽然其能够接收的参数数量有限制。这些方法在 Python 3.3 中仍然有效，但已有含蓄的警告称将完全淘汰这些方法，目前还没有明确的时间进度表。[PEP-3101](http://www.python.org/dev/peps/pep-3101/)

**格式化浮点数：**

```python
pi = 3.14159
print(" pi = %1.2f ", % pi)
```

**多个替换值**

```python
s1 = "cats"
s2 = "dogs"
s3 = " %s and %s living together" % (s1, s2)
```

**没有足够的参数**

使用老的格式化方法，我经常犯错“TypeError: not enough arguments for formating string”，因为我数错了替换变量的数量，编写如下这样的代码很容易漏掉变量。

```python
set =  (%s, %s, %s, %s, %s, %s, %s, %s) " % (a,b,c,d,e,f,g,h,i)
```

对于新的Python格式字符串，可以使用编号的参数，这样你就不需要统计有多少个参数。

```python
set = set = " ({0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}) ".format(a,b,c,d,e,f,g)
```

## 更多`.format()`的格式字符串方法

`format()` 函数提供了相当多的附加特性和功能，如下是一些有用的使用 `.format()` 的技巧。

**命名参数**

你可以将新的格式字符串用作模板引擎，使用命名参数，这样就不要求有严格的顺序。

```text
madlib = " I {verb} the {object} off the {place} ".format(verb="took", object="cheese", place="table")
>>> I took the cheese off the table
```

**多次复用同一个变量**

使用`%` 格式字符串，要求变量有严格的次序，而`.format()`方法允许如上所示那样任意排列参数，也允许复用。

```text
str = "Oh {0}, {0}! wherefore art thou {0}?".format("Romeo")
>>> Oh Romeo, Romeo! wherefore art thou Romeo?
```

**将数值转换为不同的进制**

可以使用如下字母来将数字转换成字母代表的进制，**d**ecimal，he**x**，**o**ctal, **b**inary。

```text
print("{0:d} - {0:x} - {0:o} - {0:b} ".format(21))
>>> 21 - 15 - 25 -10101
```

**将格式作为函数来使用**

可以将`.format()`用作函数，这就允许在代码中将普通文本和格式区分开来。例如，你可以在程序的开头包含所有需要使用的格式，然后在后面使用。这也是一种处理国际化的好方法，国际化不仅要求不同的文本，且常常要求不同的数字格式。

```python
## 定义格式
email_f = "Your email address was {email}".format
### 在另一个地方使用
print(email_f(email="bob@example.com"))
```

感谢 [earthboundkid](http://www.reddit.com/r/Python/comments/174e1i/python_string_format_cookbook/c82ot0h) 在 reddit 上提供这一技巧。

## 其他技巧

**转义大括号**

使用`str.format()`时，若你需要使用大括号，只要写两次就可以了：

```text
print(" The {} set is often represented as { {0} } ".format("empty"))
>>> The empty set is often represented as {0}
```

## 参考资料
- [Python String Library](http://docs.python.org/3/library/string.html) - 标准库文档