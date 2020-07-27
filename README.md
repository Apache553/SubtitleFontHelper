# SubtitleFontHelper

能够让你处理影片字幕的字体加载容易一些（大概）

## 主要功能

### 建立字体目录索引
扫描某个目录，使用FreeType库读取找到的字体文件(仅限ttf,ttc,otf)中的字体名，与对应的文件路径一起存储到XML中以便未来使用

### 处理ass字体文件
读取ass文件，找到其中引用的字体(其实是暴力字符串查找)，可以输出：
 - 其引用到的字体名
 - 在系统已安装字体和加载的前面建立的索引中的字体匹配后无法找到的字体名
 - 将能够找到的字体文件和无法找到的字体名的列表打包输出为zip
 - 从索引中查找字幕引用的字体，并加载到系统中（直到下一次重启之前有效）

### 动态监控进程加载并hook对应函数
在向操作系统请求字体之前先查询这里的字体索引，如果系统中未安装但索引中有就自动加载，以此实现按需加载字体

## 使用

目前只有命令行界面可用，以后再实现个界面吧(Qt都挂好了)🕊🕊🕊

## 配置文件

默认配置文件在`%LOCALAPPDATA%\SubtitleFontHelper.xml`
目前文档结构如下
```
<?xml version="1.0" encoding="UTF-8"?>
<ConfigFile>
<IndexFile path="索引文件路径1"/>
<IndexFile path="索引文件路径2"/>
<MonitorProcess name="可执行文件名"/>
</ConfigFile>
```
`IndexFile`项用于实现自动加载索引
`MonitorProcess`项用于把给定程序加入到监控列表中

## PS
我因为代码写得稀烂被打了.jpg

欢迎提出issue.webp
