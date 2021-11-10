



# windows 10下基于skia的文字设计（使用vs2019）



## 前言

我们项目的Github仓库地址：

https://github.com/zcy2466/bistu301

本文档是记录使用skia进行文字设计的开发过程

文档更新于2021年十一月

## 一、项目前期准备

### python2.7



python官网:

https://www.python.org/downloads/release/python-2718/ 

需要添加到环境变量

![](https://github.com/zcy2466/bistu301/blob/main/image/python.jpg)

### git

git官网：

```
https://git-scm.com/
```

一个分布式版本控制系统，由于skia没有提供官方的下载方法，只能通过git bash从开源项目库中克隆其源代码

### depot_tools

depot_tools是个python项目的工具包，用于仓库的管理

### visual studio 2019或以上版本
项目仓库：
```
git clone http://src.chromium.org/svn/trunk/tools/depot_tools.git
```

github镜像仓库

```
git clone https://github.com/cybertk/depot_tools.git
```

gittee镜像仓库

```
git clone https://gitee.com/de-user/depot_tools.git
```

需要为其添加环境变量

### visual studio 2019/2022
安装时需要勾选windows 10 sdk

## 二、编译skia

### 下载skia

skia官网：

https://skia.org/

官方仓库地址：

```
git clone https://skia.googlesource.com/skia.git
```

github镜像仓库：

```

```

gitee镜像仓库:

```

```

### 编译skia

在skia文件夹下使用

```
python2 tools/git-sync-deps
```

即可补全编译缺少的第三方库

//图片

由于官网提供的编译方法各种bug，这里使用vs对项目进行编译

```
bin\gn.exe gen out/vs2019x86d --sln="skia" --ide="vs2019"
```

提示done则编译成功

//图片

编译成功

在skia目录下的out目录下会出现一个vs2019x86d目录

其中会有skia.sln文件

使用vs打开点击生成即可编译

打开hellowrold运行编译成功
