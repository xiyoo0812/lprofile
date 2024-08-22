# lprofile
一个使用lua解析excel的xlsx/xlsm格式的库。

# 依赖
- [lua](https://github.com/xiyoo0812/lua.git)5.2以上
- [luakit](https://github.com/xiyoo0812/luakit.git)一个luabind库
- 项目路径如下<br>
  |--proj <br>
  &emsp;|--lua <br>
  &emsp;|--lprofile <br>
  &emsp;|--luakit

# 编译
- msvc: 准备好lua依赖库并放到指定位置，将proj文件加到sln后编译。
- linux: 准备好lua依赖库并放到指定位置，执行make -f lprofile.mak

# 用法
```lua
local profile = require('lprofile')

profile.hook()
profile.start()

profile.dump()
```
