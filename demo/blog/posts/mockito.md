---
title: Java 单测伴侣 - mockito
date: 2019-06-17
id: mockito
---

其实工作以来，我很少写测试/单测代码，一方面是大部分互联网公司团队对测试的要求不高，另一方面是想写好测试代码还挺难的，挺花时间，其中最麻烦的是待测代码可能会访问外部资源（比如数据库、HTTP API），如果不能方便地进模拟访问这些外部资源，那么测试起来会非常麻烦。

但，对于复杂逻辑，如果不经过严格测试，发布到生产环境，又有些不放心，没底气，或者在代码重构时，如果没有覆盖全面的测试，很难评估代码变动带来的影响。

直到遇到 [mockito](https://site.mockito.org/)，我才觉得是时候认真写写测试代码了。

---

[mockito](https://site.mockito.org/) 提供两种对象模拟方式：**mock** 和 **spy**。

简单来说，mock 模拟的对象是一个完全假的对象，只是具备指定类型的接口，以 `java.util.List` 为例：

```java
import static org.mockito.Mockito.mock;

List mockedList = mock(List.class);
```

虽然 List 是一个 interface，也可以模拟出一个对象实例，这个 mockedList 对象具备 List 接口定义的所有方法，但所有方法都不具备实际的行为操作，对于有返回值的方法，则默认返回方法返回类型的默认值，没有返回值的方法，则纯粹是一个空方法。比如：

```java
// mockedList 并不会真的把 1 存下来
mockedList.add(1);
// 所以，size() 返回默认值，输出 0
System.out.println(mockedList.size());
// 输出 null
System.out.println(mockedList.get(0));
// 输出 null
System.out.println(mockedList.get(1));
```

对于模拟出来的对象，可以任意指定其方法的返回值，比如：

```java
import static org.mockito.Mockito.when;

// 调用 size() 方法时，返回 10
when(mockedList.size()).willReturn(10);
when(mockedList.get(0)).willReturn("Hello World!");
when(mockedList.get(1)).thenReturn("您好！");

// 输出 10
System.out.println(mockedList.size());
// 输出 Hello World!
System.out.println(mockedList.get(0));
// 输出 您好！
System.out.println(mockedList.get(1));
```

当然我们写测试代码时，并不会使用 System.out.println，然后看输出，而是使用**断言**：

```java
import static org.junit.Assert.assertEquals;

assertEquals(10, mockedList.size());
assertEquals("Hello World!", mockedList.get(0));
assertEquals("您好！", mockedList.get(1));
```

断言方法非常多，不仅仅只是 assertEquals。

对于同一个方法，可以模拟多次调用返回不同的值：

```java
// 会覆盖之前 mock 的行为：when(mockedList.size()).willReturn(10);
// 或者这么写：when(mockedList.size()).willReturn(0, -1, 10);
when(mockedList.size()).thenReturn(0).thenReturn(-1).thenReturn(10);
assertEquals(0, mockedList.size());
assertEquals(-1, mockedList.size());
assertEquals(10, mockedList.size());
// 第 3 次之后的 mockedList.size() 调用都返回 10
assertEquals(10, mockedList.size());

Iterator iterator = mock(Iterator.class);
// 或者这么写：when(iterator.next()).thenReturn(0, 1, 10, 1000);
when(iterator.next()).thenReturn(0).thenReturn(1).thenReturn(10).thenReturn(1000);
assertEquals(0, iterator.next());
assertEquals(1, iterator.next());
assertEquals(10, iterator.next());
assertEquals(1000, iterator.next());
// 第 4 次之后的 iterator.next() 调用都返回 1000
assertEquals(1000, iterator.next());
```

还可以模拟异常抛出：

```java
List mockedList = mock(List.class);

when(mockedList.get(-1000)).thenThrow(new RuntimeException("参数异常！"));
try {
    mockedList.get(-1000);
} catch (Exception e) {
    assertTrue(e instanceof RuntimeException);
    assertEquals("参数异常！", e.getMessage());
}
```

也可以基于复杂的逻辑来构造返回值：

```java
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;

List<Integer> mockedList = mock(List.class);
when(mockedList.get(anyInt())).thenAnswer(new EchoAnswer());

assertTrue(1 == mockedList.get(1));
assertTrue(10 == mockedList.get(10));

public class EchoAnswer implements Answer<Integer> {

    public Integer answer(InvocationOnMock var) {
        return var.getArgument(0);
    }
}
```

除了 `when(...).thenReturn(...)` 风格的测试模拟方式，还有 BDD（Behavior Driven Development 行为驱动开发）风格的：

```java
import static org.mockito.BDDMockito.given;

// given
given(mockedList.get(0)).willReturn(100);
// when
int v = (int) mockedList.get(0);
// then
assertEquals(100, v);
```

如果方法没有返回值，或者其它奇葩的需求，则没法使用 when.thenReturn / willReturn 这样的模拟方法，可以使用 `doReturn(...).when(...)...`：

```java
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.doReturn;

ArrayList mockedList = mock(ArrayList.class);
// clear 方法无返回值
doThrow(new RuntimeException("清除失败")).when(mockedList).clear();

try {
    mockedList.clear();
} catch (Exception e) {
    assertTrue(e instanceof RuntimeException);
    assertEquals("清除失败", e.getMessage());
}

// 没有意义，因为没法使用 断言 来验证，实际运行时会抛异常
doReturn(10).when(mockedList).clear();
```

从示例代码可以看出，`doReturn(...).when(...)....` 不会做类型校验，mockedList.clear() 返回值类型为 void，但我们模拟让其返回 10；所以，正常情况应该尽可能使用 `when(...).thenReturn(...)` 或 `given(...).willReturn(...)`。

---

前述代码示例中，模拟方法的参数都做了硬编码，实际情况通常都不是这么测试，而是模拟方法的参数符合一定的要求即可，比如：在某个范围之内、符合类型的任何值：

```java
import static org.mockito.Mockito.anyInt;

/*
以任何 int 类型的参数调用 mockedList.get 方法，都返回 100

如果写成 when(mockedList.get(0)).thenReturn(100)，则只有以 0 为参数调用 mockedList.get 方法，才会返回100，其他参数值，返回的都是默认值 0
*/
when(mockedList.get(anyInt())).thenReturn(100);

assertEquals(100, mockedList.get(0));
assertEquals(100, mockedList.get(1000));
```

可用的参数匹配器，见 org.mockito.ArgumentMatchers 类的静态方法列表，也可以自己实现 ArgumentMatcher 接口：

```java
package org.mockito;

public interface ArgumentMatcher<T> {
    boolean matches(T var1);
}
```

```java
import org.mockito.ArgumentMatcher;
import static org.mockito.Mockito.intThat;

when(mockedList.get(intThat(new LimitedInt()))).thenReturn(10);

assertEquals(null, mockedList.get(-1));
assertEquals(10, mockedList.get(1));
assertEquals(10, mockedList.get(99));
assertEquals(null, mockedList.get(100));

public class LimitedInt implements ArgumentMatcher<Integer> {

    public boolean matches(Integer var) {
        return var > 0 && var < 100;
    }
}
```

如果被模拟的方法包含多个参数，那么这些参数要么全部使用匹配器，要么全部不使用。

---

模拟某些类（A）的方法，通常会将 mock 出来的对象注入到依赖该类实例的其他类（B）中，来替代真实的依赖，这种方式的目的是为了测试类 B 的行为是否符合预期。

另一个测试需求是，测试某个类 A' 在某个上下文环境中的行为是否符合预期，比如： A' 的某个方法是否被调用过、调用过几次、调用参数是否符合预期、几个方法之间的调用次序是否符合预期、方法调用耗时是否符合预期等等。

```java
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verifyZeroInteractions;

List mocked = mock(List.class);

Caller caller = new Caller();
caller.setList(mocked);

// 调用 0 次
caller.run(0);
// 验证是否从来没调用过 mocked.size()
verify(mocked, never()).size();
// 验证 没有和 mocked 产生过任何交互
// 因为 Caller.run 中调用了 list.isEmpty()，实际产生了交互，所以这行测试会失败
verifyZeroInteractions(mocked);

// 调用 10 次
caller.run(10);
// 验证是否调用 mocked.size() 10 次
verify(mocked, times(10)).size();

// 再调用一次
caller.run(1);
// 所以是 11 次了
verify(mocked, times(11)).size();

@Data
public class Caller {
    List list;

    public void run(int count) {
        for (int idx=0; idx < count; idx++) {
            list.size();
        }
        //
        list.isEmpty();
    }
}
```

```java
List mocked = mock(List.class);

mocked.add(1);
mocked.add(2);

verify(mocked).add(1);

// 是否有其他交互没有验证过？因为 mocked 还调用过 mocked.add(2)，所以这句测试会失败
verifyNoMoreInteractions(mocked);
```

```java
import org.mockito.InOrder;

// 也可以验证调用次序
List mocked1 = mock(List.class);
List mocked2 = mock(List.class);

mocked1.size();
mocked1.isEmpty();
mocked2.isEmpty();

// 会记录 mocked1、mocked2 中方法的调用/交互次序，要求：与 mocked1 的交互先于 mocked2
InOrder inOrder = inOrder(mocked1, mocked2);
// mocked1、mocked2 的交互顺序必须和 inOrder.verify 之间的顺序一致
inOrder.verify(mocked1).size();
inOrder.verify(mocked1).isEmpty();
inOrder.verify(mocked2).isEmpty();
```

---

也可以验证某个方法被调用时所使用的参数是否符合预期：

```java
import org.mockito.ArgumentCaptor;

List mockedlist = mock(List.class);

Caller caller = new Caller();
caller.setList(mockedlist);
caller.run();

// 捕获 mockedList.add 的调用参数
ArgumentCaptor<Integer> argumentCaptor = ArgumentCaptor.forClass(Integer.class);
verify(mockedlist).add(argumentCaptor.capture());
assertTrue(100 == argumentCaptor.getValue());

@Data
public class Caller {
    List list;

    public void run() {
        list.add(100);
    }
}
```

---

前面的内容都是以 mock 为例，我们再来说说 spy，与 mock 的区别：

mock 出来的对象是一个完全假的对象，但 spy 通常是基于一个具体的类或类实例，对其篡改某些方法，对于被篡改方法之外的方法，其行为都和调用真实对象的方法一样，不过并没有调用真实对象的方法，也不会对真实对象产生影响：

```java
// 基于一个实际的类实例
List<Integer> realList = new ArrayList<>(10);
List<Integer> spy = spy(realList);
        
spy.add(1);

// 被窃听的对象并没有发生变化
assertEquals(0, realList.size());
// 间谍对象确实将 1 存了下来
assertEquals(1, spy.size());
// 这句会抛出 java.lang.IndexOutOfBoundsException，因为 realList 还是为空
assertTrue(1 == realList.get(0));
assertTrue(1 == spy.get(0));
```

也可以基于一个具体的类来构造 spy，但这样无法使用带参数的构造方法，也无法指定类型参数：

```java
List<Integer> = spy(ArrayList.class);
assertEquals(0, spy.size());
spy.add(100);
assertEquals(1, spy.size());
assertTrue(100 == spy.get(0));

// 篡改方法
when(spy.size()).thenReturn(-1);
assertEquals(-1, spy.size());
```

实际上，mock 也可以基于具体的类来构造，这时可以指定某些方法实际调用具体类的方法。

---

除了使用 mock、spy 方法来构造模拟对象，还可以通过注解来构造，但这样的话得指定 JUnit 的 Runner 为 `org.mockito.junit.MockitoJUnitRunner`：

```java
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.List;

import static org.mockito.Mockito.when;
import static org.junit.Assert.assertTrue;

@RunWith(MockitoJUnitRunner.class)
public class testTester {

    @Mock
    private List<Integer> mocked;

    @Spy
    private ArrayList<Integer> spyed;

    @Test
    public void test() {
        when(mocked.isEmpty()).thenReturn(false);
        when(spyed.isEmpty()).thenReturn(false);

        assertTrue(!mocked.isEmpty());
        assertTrue(!spyed.isEmpty());

        mocked.add(0);
        spyed.add(0);

        assertTrue(0 == mocked.size());
        assertTrue(1 == spyed.size());
    }
}
```