# lprofile
一个基于luahook的lua性能测试库！

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
local log_debug = logger.debug
local profile = require('lprofile')

profile.hook()
profile.enable()


local PROFDUMP  = "{:<25} {:^9} {:^9} {:^9} {:^12} {:^8} {:^12} [{}]{}:{}]"

log_debug("--------------------------------------------------------------------------------------------------------------------------------")
log_debug("{:<25} {:^9} {:^9} {:^9} {:^12} {:^8} {:^12} {:<10}", "name", "avg", "min", "max", "all", "per(%)", "count", "source")
log_debug("--------------------------------------------------------------------------------------------------------------------------------")
for _, ev in pairs(profile.dump(50)) do
    log_debug(PROFDUMP, ev.name, ev.avg, ev.min, ev.max, ev.all, ev.per, ev.count, ev.flag, ev.src, ev.line)
end
log_debug("--------------------------------------------------------------------------------------------------------------------------------")
```
