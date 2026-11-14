## kilons
一个遵循 Unix 设计哲学、以 POSIX 兼容为目标的操作系统。

内核是自己写的，系统也是。

## 这玩意儿到底是什么？
它是一个正在开发的自由开源操作系统，采用单体内核架构。

本项目遵循以下原则：

- 遵循 Unix 哲学

- 以 POSIX 为标准，优先实现标准化的系统接口。

- 拒绝不必要的私有接口，不为某个平台的专有 API 牺牲系统设计。

## 支持的平台

| 架构 | 支持情况 |
| --- | --- |
| AMD64 / x86_64 | 基本完成 |
| AArch64 | 计划支持 |
| LoongArch64 | 计划支持 |

（其实论完成度的话，AMD64已经完成了，就是100%的级别，但怕被当作那种java那样的“一次编译，到处调试”的情况，就给个“基本完成”来降低一下期待吧。而且我都跑过ASan和UBSan了，都是PASS的！）

## 怎么编译？

Ubuntu 平台：

Ubuntu 24.04

```bash
sudo apt update
sudo apt install make build-essential nasm qemu-system-x86 xorriso mtools grub-pc-bin grub-efi-amd64-bin
```

Ubuntu 26.04

```bash
sudo apt update
sudo apt install make build-essential nasm qemu-system-x86 xorriso mtools grub-pc-bin grub-efi-amd64-bin
```

Arch 平台：

无支持测试

Debian 平台：

无支持测试

克隆仓库：

```bash
git clone https://github.com/97neko75/kilons.git
cd kilons
```

在包含 Makefile 的源码版本中执行：

```bash
make
make iso
make run
```

如果报错，请先检查构建依赖、编译器版本.

别看见错误就直接认定是内核炸了。说不定只是你没装全工具呢？

## 开源许可证

本项目使用 Mozilla Public License 2.0（MPL-2.0）。

完整许可条款请参阅仓库中的 LICENSE。

## 注意

本项目仍处于早期实验阶段。

## 最后

如果你看到了这里，说明你至少愿意了解这个还没长大的操作系统。

欢迎研究源码、提交问题，以及参与开发。

喜欢的话，可以给个 Star。（给star的会变可爱哦～）

不喜欢也没关系。（呜呜呜呜）
