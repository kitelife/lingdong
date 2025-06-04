---
title: 一行式并行方案（译）
date: 2015-09-11
id: parallelism-in-one-line
---

原文：[Parallelism in one line](http://chriskiehl.com/article/parallelism-in-one-line)

在并行处理能力方面，Python的声名并不太好。不考虑关于线程和GIL（多数情况下是合理的）的标准论据，我认为Python中关于并行的真正问题并不是一个技术问题，而是教学问题。围绕Python线程和多进程的常见教程，一般都写得不错，但也令人乏味 - 激烈非凡，对日常真正有用的东西却很少涉及。

### 沿袭的例子

在DuckDuckGo（DDG）中搜索“Python多线程教程”，简单调查一下排在前面的结果，就会发现它们给出的都是同样基于Class + Queue的示例。

*介绍threading/multiprocessing、生产者/消费者的真实示例代码：*

```python
# coding: utf-8
# Example.py
'''
标准的多线程生产者/消费者模式
'''

import time 
import threading 
import Queue 

class Consumer(threading.Thread): 
  def __init__(self, queue): 
    threading.Thread.__init__(self)
    self._queue = queue 

  def run(self):
    while True: 
      # queue.get() 会阻塞当前线程，直到获取到一个数据项
      msg = self._queue.get() 
      # 检查当前消息是否是个“毒药丸”
      if isinstance(msg, str) and msg == 'quit':
        # 如果是，则退出循环
        break
      # “处理” (这里是打印)从队列中取出的数据项
      print "I'm a thread, and I received %s!!" % msg
    # 我始终是这么的友好
    print 'Bye byes!'


def Producer():
  # Queue用于在线程之间共享数据项
  queue = Queue.Queue()

  # 创建一个工作实例
  worker = Consumer(queue)
  # start方法会调用内部的run()方法来开启线程
  worker.start() 

  # 变量，用于追踪开始的时间
  start_time = time.time() 
  # 在5秒之内
  while time.time() - start_time < 5: 
    # “生产”一块工作，放入队列中，由消费者来处理
    queue.put('something at %s' % time.time())
    # 睡眠一会儿，以避免过多的消息
    time.sleep(1)

  # 这是杀死线程的“毒药丸”方式
  queue.put('quit')
  # 等待线程关闭
  worker.join()


if __name__ == '__main__':
  Producer()
```

嗯...闻闻，代码中一股子Java的气息。

我不想让大家觉得好像我认为生产者/消费者是处理线程/多进程的错误方式 - 因为确实不是。实际上，对多种问题来说，这种方式非常适合。然而，我认为：对于日常的脚本程序来说，这种方式并非是最有用的。

### 问题（我认为的）

其一，为了做点有用的事情，你得搞一个公式化的类；其二，你得维护一个队列（Queue），用于传送对象；这些齐备之后，在队列管道的两端还得准备方法来做真正的工作（如果希望有两种方式来通信或者准备存储结果，可能还得引入另一个队列）。

**更多的工作者，更多的问题**

基于此，下一件你想要做的事情就是搞一个工作者类的池，来加速你的Python程序。在关于线程的IBM教程中，给出了一个示例代码，以下是其变种。这是一个常见的应用场景 - 在多个线程上分配获取网页的任务。

```python
# coding: utf-8
# Example2.py
'''
一个更加实际的线程池示例
'''

import time 
import threading 
import Queue 
import urllib2 

class Consumer(threading.Thread): 
  def __init__(self, queue): 
    threading.Thread.__init__(self)
    self._queue = queue 

  def run(self):
    while True: 
      content = self._queue.get() 
      if isinstance(content, str) and content == 'quit':
        break
      response = urllib2.urlopen(content)
    print 'Bye byes!'


def Producer():
  urls = [
    'http://www.python.org', 'http://www.yahoo.com'
    'http://www.scala.org', 'http://www.google.com'
    # 等等... 
  ]
  queue = Queue.Queue()
  worker_threads = build_worker_pool(queue, 4)
  start_time = time.time()

  # 加入待处理的URL
  for url in urls: 
    queue.put(url)
  # 加入毒药丸
  for worker in worker_threads:
    queue.put('quit')
  for worker in worker_threads:
    worker.join()

  print 'Done! Time taken: {}'.format(time.time() - start_time)

def build_worker_pool(queue, size):
  workers = []
  for _ in range(size):
    worker = Consumer(queue)
    worker.start() 
    workers.append(worker)
  return workers

if __name__ == '__main__':
  Producer()
```

奏效了，但是你看看这些代码！准备（setup）方法、一组要追踪的线程，最糟糕的是，若有任何地方易发生死锁，就会产生一堆的join状态说明。自此，一切只会更复杂！

到目前为止，完成了些什么？啥都没有。上面的代码纯粹只是把所有东西像用纸糊起来一样（Just about everything in the above code is pure plumbing，如何翻译？）。这是另一种公式化的写法，也易发生错误（嘿，在写这个代码时，我甚至忘了在队列对象上调用task_done()（我懒得去解决这个问题然后再搞个截图）），付出很多，得到的却很少。幸运的是，我们有更好的方式。

## 引入：Map

Map是一个酷酷的小东西，也是在Python代码轻松引入并行的关键。对此不熟悉的人会认为map是从函数式语言（如Lisp）借鉴来的东西。map是一个函数 - 将另一个函数映射到一个序列上。例如：

```python
urls = ['http://www.yahoo.com', 'http://www.reddit.com']
results = map(urllib2.urlopen, urls)
```

这段代码在传入序列的每个元素上应用方法*urlopen*，并将所有结果存入一个列表中。大致与下面这段代码的逻辑相当：

```python
results = []
for url in urls: 
    results.append(urllib2.urlopen(url))
```

Map会为我们处理在序列上的迭代，应用函数，最后将结果存入一个方便使用的列表。

这为什么重要呢？因为利用恰当的库，map让并行处理成为小事一桩！

![map-function|600](https://i.loli.net/2020/06/14/kqumnip3B7M2dcQ.png)

Python标准库中*multiprocessing*模块，以及极少人知但同样出色的子模块*multiprocessing.dummy*，提供了map函数的并行版本。

题外话：这是啥？你从未听说过这名为dummy的mulprocessing模块的线程克隆版本？我也是最近才知道的。在multiprocessing文档页中仅有一句提到这个子模块，而这句话基本可以归结为“哦，是的，存在这样一个东西”。完全低估了这个模块的价值！

Dummy是multiprocessing模块的精确克隆，唯一的区别是：multiprocessing基于进程工作，而dummy模块使用线程（也就带来了常见的Python限制）。因此，任何东西可套用到一个模块，也就可以套用到另一个模块。在两个模块之间来回切换也就相当容易，当你不太确定一些框架调用是IO密集型还是CPU密集型时，想做探索性质的编程，这一点会让你觉得非常赞！

### 开始

为了访问map函数的并行版本，首先需要导入包含它的模块：

```python
# 以下两行引入其一即可
from multiprocessing import Pool
from multiprocessing.dummy import Pool as ThreadPool
```

并实例化池对象：

```python
# 译注：这里其实是以dummy模块为例
pool = ThreadPool()
```

这一句代码处理了example2.py中7行的*build_worker_pool*函数完成的所有事情。如名所示，这句代码会创建一组可用的工作者，启动它们来准备工作，并将它们存入变量中，方便访问。

pool对象可以有若干参数，但目前，只需关注第一个：进程/线程数量。这个参数用于设置池中的工作者数目。如果留空，默认为机器的CPU核数。

一般来说，如果为CPU密集型任务使用进程池（multiprocessing pool），更多的核等于更快的速度（但有一些注意事项）。然而，当使用线程池（threading）处理网络密集型任务时，情况就很不一样了，因此最好试验一下池的最佳大小。

```python
pool = ThreadPool(4) # 将池的大小设置为4
```

如果运行了过多的线程，就会浪费时间在线程切换上，而不是做有用的事情，所以可以把玩把玩直到找到最适合任务的线程数量。

现在池对象创建好了，简单的并行也是弹指之间的事情了，那来重写example2.py吧。

```python
import urllib2 
from multiprocessing.dummy import Pool as ThreadPool 

urls = [
  'http://www.python.org', 
  'http://www.python.org/about/',
  'http://www.onlamp.com/pub/a/python/2003/04/17/metaclasses.html',
  'http://www.python.org/doc/',
  'http://www.python.org/download/',
  'http://www.python.org/getit/',
  'http://www.python.org/community/',
  'https://wiki.python.org/moin/',
  'http://planet.python.org/',
  'https://wiki.python.org/moin/LocalUserGroups',
  'http://www.python.org/psf/',
  'http://docs.python.org/devguide/',
  'http://www.python.org/community/awards/'
  # 等等...
  ]

# 创建一个工作者线程池
pool = ThreadPool(4) 
# 在各个线程中打开url，并返回结果
results = pool.map(urllib2.urlopen, urls)
#close the pool and wait for the work to finish
# 关闭线程池，等待工作结束
pool.close() 
pool.join()
```

看看！真正做事情的代码仅有4行，其中3行只是简单的辅助功能。*map*调用轻松搞定了之前示例40行代码做的事情！觉得好玩，我对两种方式进行了时间测量，并使用了不同的池大小。

```python
# 译注：我觉得与串行处理方式对比意义不大，应该和队列的方式进行性能对比
results = [] 
for url in urls:
  result = urllib2.urlopen(url)
  results.append(result)

# # ------- 对比 ------- # 


# # ------- 池的大小为4 ------- # 
pool = ThreadPool(4) 
results = pool.map(urllib2.urlopen, urls)

# # ------- 池的大小为8 ------- # 

pool = ThreadPool(8) 
results = pool.map(urllib2.urlopen, urls)

# # ------- 池的大小为13 ------- # 

pool = ThreadPool(13) 
results = pool.map(urllib2.urlopen, urls)
```

结果：

```text
单线程: 14.4 秒
池大小为4时：3.1 秒
池大小为8时：1.4 秒
池大小为13时：1.3秒
```

真是呱呱叫啊！也说明了试验不同的池大小是有必要的。在我的机器上，池的大小大于9后会导致性能退化（译注：咦，结果不是显示13比8的性能要好么？）。

## 现实中的Example 2

为千张图片创建缩略图。

来做点CPU密集型的事情！对于我，在工作中常见的任务是操作大量的图片目录。其中一种图片转换是创建缩略图。这项工作适于并行处理。

**基本的单进程设置**

```python
from multiprocessing import Pool 
from PIL import Image

SIZE = (75,75)
SAVE_DIRECTORY = 'thumbs'

def get_image_paths(folder):
  return (os.path.join(folder, f) 
      for f in os.listdir(folder) 
      if 'jpeg' in f)

def create_thumbnail(filename): 
  im = Image.open(filename)
  im.thumbnail(SIZE, Image.ANTIALIAS)
  base, fname = os.path.split(filename) 
  save_path = os.path.join(base, SAVE_DIRECTORY, fname)
  im.save(save_path)

if __name__ == '__main__':
  folder = os.path.abspath(
    '11_18_2013_R000_IQM_Big_Sur_Mon__e10d1958e7b766c3e840')
  os.mkdir(os.path.join(folder, SAVE_DIRECTORY))

  images = get_image_paths(folder)

  for image in images: 
    create_thumbnail(image)
```

示例代码中用了一些技巧，但大体上是：向程序传入一个目录，从目录中获取所有图片，然后创建缩略图，并将缩略图存放到各自的目录中。

在我的机器上，这个程序处理大约6000张图片，花费27.9秒。

如果使用一个并行的map调用来替换*for*循环：

```python
from multiprocessing import Pool 
from PIL import Image

SIZE = (75,75)
SAVE_DIRECTORY = 'thumbs'

def get_image_paths(folder):
  return (os.path.join(folder, f) 
      for f in os.listdir(folder) 
      if 'jpeg' in f)

def create_thumbnail(filename): 
  im = Image.open(filename)
  im.thumbnail(SIZE, Image.ANTIALIAS)
  base, fname = os.path.split(filename) 
  save_path = os.path.join(base, SAVE_DIRECTORY, fname)
  im.save(save_path)

if __name__ == '__main__':
  folder = os.path.abspath(
    '11_18_2013_R000_IQM_Big_Sur_Mon__e10d1958e7b766c3e840')
  os.mkdir(os.path.join(folder, SAVE_DIRECTORY))

  images = get_image_paths(folder)

  pool = Pool()
  pool.map(create_thumbnail, images)
  pool.close() 
  pool.join()
```

**5.6秒！**

仅修改几行代码就能得到巨大的速度提升。这个程序的生产环境版本通过切分CPU密集型工作和IO密集型工作并分配到各自的进程和线程（通常是死锁代码的一个因素），获得更快的速度。然而，由于map性质清晰明确，无需手动管理线程，以干净、可靠、易于调试的方式混合匹配两者（译注：这里的“两者”是指什么？CPU密集型工作和IO密集型工作？），也是相当容易的。

就是这样了。（几乎）一行式并行解决方案。