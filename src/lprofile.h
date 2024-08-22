#pragma once

#include <set>
#include <deque>
#include <string>
#include <chrono>
#include <unordered_map>

#include "lua_kit.h"

using namespace std;
using namespace luakit;
using namespace std::chrono;

using cpchar = const char*;

namespace lprofile {
    struct call_frame {
        bool inlua = true;
        bool tail = false;
        cpchar name = nullptr;
        cpchar source = nullptr;
        uint32_t line = 0;
        uint64_t call_tick;
        uint64_t pointer = 0;
    };

    struct call_info {
        lua_State* co = nullptr;
        uint64_t sub_cost = 0;
        deque<call_frame> call_list;
    };

    class eval_data {
    public:
        //自定义比较函数
        bool operator<(const eval_data& b) const {
            if (total_time == b.total_time) return pointer > b.pointer;
            return total_time > b.total_time;
        }

        bool inlua = true;
        uint32_t line = 0;
        uint64_t min_time = 0;
        uint64_t max_time = 0;
        uint64_t pointer = 0;
        uint64_t call_tick = 0;
        uint64_t call_count = 0;
        uint64_t total_time = 0;
        cpchar source = nullptr;
        cpchar name = nullptr;
    };

    class profile {
    public:
        void start() {
            m_profilable = true;
        }

        int ignore(lua_State* L, cpchar library) {
            lua_guard g(L);
            lua_getglobal(L, library);
            if (lua_istable(L, -1)) {
                lua_pushnil(L);
                while (lua_next(L, -2) != 0) {
                    if (lua_isfunction(L, -1)) {
                        uint64_t pointer = (uint64_t)lua_topointer(L, -1);
                        m_ignores.emplace(pointer, true);
                    }
                    lua_pop(L, 1);
                }
                return 0;
            }
            if (lua_isfunction(L, -1)) {
                uint64_t pointer = (uint64_t)lua_topointer(L, -1);
                m_ignores.emplace(pointer, true);
            }
            return 0;
        }

        void ignore_file(cpchar filename) {
            m_filters.emplace(filename, true);
        }

        void ignore_func(cpchar funcname) {
            m_names.emplace(funcname, true);
        }

        void watch_file(cpchar filename) {
            m_watchs.emplace(filename, true);
        }

        int hook(lua_State* L) {
            auto luahook = [](lua_State* DL, lua_Debug* ar) {
                lua_guard g(DL);
                lua_getfield(DL, LUA_REGISTRYINDEX, "profile");
                profile* prof = (profile*)lua_touserdata(DL, -1);
                if (prof) prof->prof_hook(DL, ar);
            };
            lua_guard g(L);
            lua_getglobal(L, "coroutine");
            lua_push_function(L, [&](lua_State* L) -> int {
                luaL_checktype(L, 1, LUA_TFUNCTION);
                lua_State* NL = lua_newthread(L);
                lua_pushvalue(L, 1);  /* move function to top */
                lua_sethook(NL, luahook, LUA_MASKCALL | LUA_MASKRET, 0);
                lua_xmove(L, NL, 1);  /* move function from L to NL */
                return 1;
            });
            lua_setfield(L, -2, "create");
            //save profile context
            lua_pushlightuserdata(L, this);
            lua_setfield(L, LUA_REGISTRYINDEX, "profile");
            //init lua ignore
            init_lua_ignore(L);
            //hook self
            lua_sethook(L, luahook, LUA_MASKCALL | LUA_MASKRET, 0);
            return 0;
        }

        int dump(lua_State* L, int top = 0) {
            set<eval_data> evals;
            for (auto& [_, data] : m_evals) {
                if (top > 0 && m_evals.size() > top) break;
                evals.insert(data);
            }
            int i = 1;
            lua_newtable(L);
            for (auto& data : evals) {
                lua_newtable(L);
                native_to_lua(L, data.name);
                lua_setfield(L, -2, "name");
                native_to_lua(L, data.line);
                lua_setfield(L, -2, "line");
                native_to_lua(L, data.source);
                lua_setfield(L, -2, "src");
                native_to_lua(L, data.inlua ? "L" : "C");
                lua_setfield(L, -2, "flag");
                native_to_lua(L, round(data.total_time * 1000 / data.call_count) / 1000000);
                lua_setfield(L, -2, "avg");
                native_to_lua(L, round(data.total_time * 1000 / m_all_times) / 10);
                lua_setfield(L, -2, "per");
                native_to_lua(L, double(data.min_time) / 1000);
                lua_setfield(L, -2, "min");
                native_to_lua(L, double(data.max_time) / 1000);
                lua_setfield(L, -2, "max");
                native_to_lua(L, data.call_count);
                lua_setfield(L, -2, "count");
                lua_seti(L, -2, i++);
                if (top > 0 && i >= top) break;
            }
            m_evals.clear();
            return 1;
        }

    protected:
        void prof_hook(lua_State* L, lua_Debug* arv) {
            if (!m_profilable) return;
            if (arv->event == LUA_HOOKCALL || arv->event == LUA_HOOKTAILCALL) {
                if (m_call_infos.empty() || m_call_infos.back().co != L) {
                    m_call_infos.push_back(call_info{ L });
                }
                lua_Debug ar;
                lua_getstack(L, 0, &ar);
                lua_getinfo(L, "nSlf", &ar);
                call_frame frame;
                frame.name = ar.name;
                frame.source = ar.source;
                frame.line = ar.linedefined;
                frame.pointer = (uint64_t)lua_topointer(L, -1);
                frame.tail = arv->event == LUA_HOOKTAILCALL;
                frame.call_tick = now();
                if (ar.what[0] == 'C') {
                    frame.inlua = false;
                    lua_Debug arv;
                    int i = 0;
                    while(true) {
                        if (!lua_getstack(L, ++i, &arv)) break;
                        lua_getinfo(L, "Sl", &arv);
                        if (arv.what[0] != 'C') {
                            frame.line = arv.currentline;
                            frame.source = arv.source;
                            break;
                        }
                    }
                }
                call_info& ci = m_call_infos.back();
                ci.call_list.push_back(frame);
                return;
            }
            if (arv->event == LUA_HOOKRET) {
                if (!m_call_infos.empty()) {
                    call_info& ci = m_call_infos.back();
                    if (ci.co != L) return;
                    while (true) {
                        if (ci.call_list.empty()) {
                            m_call_infos.pop_back();
                            break;
                        }
                        call_frame& cur_frame = ci.call_list.back();
                        uint64_t call_cost = now() - cur_frame.call_tick - ci.sub_cost;
                        ci.sub_cost += call_cost;
                        record_eval(cur_frame, call_cost);
                        ci.call_list.pop_back();
                        if (ci.call_list.empty()) break;
                        if (!ci.call_list.back().tail) break;
                    }
                    ci.sub_cost = 0;
                }
            }
        }

        void init_lua_ignore(lua_State* L) {
            //忽略系统库
            ignore(L, "io");
            ignore(L, "os");
            ignore(L, "math");
            ignore(L, "utf8");
            ignore(L, "debug");
            ignore(L, "table");
            ignore(L, "string");
            ignore(L, "package");
            ignore(L, "profile");
            ignore(L, "coroutine");
            //忽略系统函数
            ignore(L, "type");
            ignore(L, "next");
            ignore(L, "load");
            ignore(L, "print");
            ignore(L, "pcall");
            ignore(L, "pairs");
            ignore(L, "error");
            ignore(L, "assert");
            ignore(L, "rawget");
            ignore(L, "rawset");
            ignore(L, "rawlen");
            ignore(L, "xpcall");
            ignore(L, "ipairs");
            ignore(L, "select");
            ignore(L, "dofile");
            ignore(L, "require");
            ignore(L, "loadfile");
            ignore(L, "tostring");
            ignore(L, "rawequal");
            ignore(L, "getmetatable");
            ignore(L, "setmetatable");
            ignore(L, "collectgarbage");
            //忽略一些特殊函数
            m_names.emplace("for iterator", true);
        }

        bool is_filter(call_frame& frame) {
            //匿名函数, 不记录
            if (!frame.name) return false;
            //命中函数名过滤，不记录
            if (m_names.find(frame.name) != m_filters.end()) return false;
            //命中系统函数，不记录
            if (!frame.inlua) {
                if (m_ignores.find(frame.pointer) != m_ignores.end()) return false;
            }
            if (m_watchs.empty()) {
                //关注文件为空，检查过滤文件
                return m_filters.find(frame.source) == m_filters.end();
            }
            return m_watchs.find(frame.source) != m_watchs.end();
        }

        void record_eval(call_frame& frame, uint64_t call_cost) {
            //总时间累加
            m_all_times += call_cost;
            if (is_filter(frame)) return;
            auto id = (uint64_t)frame.pointer;
            auto handle = m_evals.extract(id);
            if (handle.empty()) {
                eval_data edata;
                edata.call_count++;
                edata.name = frame.name;
                edata.line = frame.line;
                edata.inlua = frame.inlua;
                edata.source = frame.source;
                edata.pointer = frame.pointer;
                edata.min_time = call_cost;
                edata.max_time = call_cost;
                edata.total_time += call_cost;
                m_evals.emplace(id, edata);
                return;
            }
            eval_data& edata = handle.mapped();
            edata.call_count++;
            edata.total_time += call_cost;
            if (call_cost > edata.max_time) edata.max_time = call_cost;
            if (call_cost < edata.min_time) edata.min_time = call_cost;
            m_evals.emplace(id, edata);
        }

        uint64_t now() {
            system_clock::duration dur = system_clock::now().time_since_epoch();
            return duration_cast<microseconds>(dur).count();
        }

    protected:
        uint64_t m_all_times = 0;
        bool m_profilable = false;
        deque<call_info> m_call_infos;
        std::unordered_map<uint64_t, eval_data> m_evals;
        std::unordered_map<uint64_t, bool> m_ignores;
        std::unordered_map<string, bool> m_filters;
        std::unordered_map<string, bool> m_watchs;
        std::unordered_map<string, bool> m_names;
    };
}
