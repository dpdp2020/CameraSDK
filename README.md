# CameraSDK

本项目主要开发摄像机嵌入式SDK，用于低延迟音视频传输应用场景，目前支持多种芯片方案，包括瑞芯微1109、1126芯片等方案。

## 一、安装方法

#### 1、安装瑞芯微交叉编译工具

1）在本项目的安装路径CameraSDK下，创建compiler子目录，

2）下载瑞芯微交叉编译工具：
    
    #linux命令行下输入  
    wget http://www.baozan.cloud/download/arm32.tar.gz
    
    #windows操作系统，在浏览器中输入 http://www.baozan.cloud/download/arm32.tar.gz

3）把下载的arm32.tar.gz文件拷贝到compiler子目录里。

4）解压交叉编译工具：

    #linux命令行下输入
    tar zfvx arm32.tar.gz
    
    #windows操作系统，用winRAR软件解压