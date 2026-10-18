#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_sample.py -- 生成演示用的示例数据库 tools/sample/cysc.db

为什么需要这个脚本：
  示例数据里的截止时间必须相对「今天」，否则线段树面板永远是空的
  （线段树的窗口是「今天起 400 天」，写死的过去日期全部落在窗口外）。
  隔一段时间重新跑一次，就能得到一份时间新鲜的演示数据。

设计目标（让每个数据结构都演示得出效果）：
  线段树 / 热力图   截止时间从 -6 天铺到 +25 天，逾期、今天到期、最忙的一天都不为 0
  Trie              大量「复习…」「写…」「提交…」共同前缀，前缀补全才有候选
  哈希表            标签集中在 学习 / 工作 / 复习 / 答辩，倒排索引与冲突链都看得见
  并查集            两个分组（工作/学习/健康）、（毕业设计/论文投稿），另有未分组列表
  栈 / 撤销         状态、子任务、优先级都有梯度，方便演示改完再撤

用法：
  python tools/gen_sample.py                 # 覆盖 tools/sample/cysc.db
  python tools/gen_sample.py 出/来的/路径.db   # 输出到指定文件

不依赖任何第三方库，也不需要先有数据库文件。
"""
from __future__ import annotations

import datetime as dt
import sqlite3
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO / "tools" / "sample" / "cysc.db"

# 与 src/core/DatabaseManager.cpp 的建表语句保持一致
DDL = [
    "CREATE TABLE IF NOT EXISTS lists ("
    "id INTEGER PRIMARY KEY, name TEXT, color TEXT, group_id INTEGER, "
    "created_at TEXT, is_default INTEGER)",
    "CREATE TABLE IF NOT EXISTS tasks ("
    "id INTEGER PRIMARY KEY, title TEXT, details TEXT, priority INTEGER, "
    "deadline TEXT, category_id INTEGER, status INTEGER, created_at TEXT, "
    "completed_at TEXT, list_id INTEGER, starred INTEGER, in_my_day INTEGER, "
    "reminder TEXT, recurrence INTEGER, tags TEXT, manual_order INTEGER)",
    "CREATE TABLE IF NOT EXISTS subtasks ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, task_id INTEGER, position INTEGER, "
    "title TEXT, done INTEGER)",
]

TODAY = dt.date.today()

# group_id：-1 表示未分组；否则存「组内最小的列表 id」（与程序里的设计一致）
LISTS = [
    (1, "任务", "#3D6BFF", -1, 1),
    (2, "工作", "#FF007F", 2, 0),
    (3, "学习", "#00AA88", 2, 0),
    (4, "健康", "#0077FF", 2, 0),
    (5, "阅读", "#32C8FF", -1, 0),
    (6, "毕业设计", "#8B5CF6", 6, 0),
    (7, "论文投稿", "#F59E0B", 6, 0),
]

# (标题, 备注, 优先级0-3, 截止日偏移, 时, 分, 分类0-3, 状态0-3, 列表, 星标, 我的一天,
#  提醒偏移(天) 或 None, 重复0-5, 标签)
T = [
    # ---- 已逾期 ----
    ("提交中期检查报告", "含进度表与下阶段计划", 3, -6, 17, 0, 2, 0, 3, 1, 0, None, 0, ["学习", "中期"]),
    ("复习图论最短路", "Dijkstra 与 Bellman-Ford 对比", 2, -4, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("交实验报告初稿", "先给导师过一遍格式", 3, -2, 18, 0, 2, 0, 3, 1, 0, None, 0, ["学习", "课程设计"]),
    ("小组周会纪要", "整理成三条结论发给组员", 1, -1, 10, 0, 1, 0, 2, 0, 0, None, 0, ["工作"]),

    # ---- 今天 ----
    ("写课程设计报告", "补一节数据结构运用，准备答辩演示", 3, 0, 18, 0, 2, 1, 3, 1, 1, None, 0,
     ["学习", "答辩", "课程设计"]),
    ("答复导师邮件", "确认答辩时间与场地", 2, 0, 12, 0, 1, 0, 2, 0, 1, 0, 0, ["工作"]),
    ("跑步 5 公里", "配速 6 分以内", 1, 0, 7, 0, 3, 2, 4, 0, 0, None, 1, ["健康"]),
    ("背单词 50 个", "错词单独记一遍", 1, 0, 8, 0, 2, 1, 3, 0, 1, None, 2, ["学习"]),

    # ---- 未来一周 ----
    ("复习线段树", "区间和与区间最大值", 3, 1, 20, 0, 2, 0, 3, 1, 1, None, 0, ["学习", "复习", "答辩"]),
    ("写课程设计PPT", "一页一个结论，别堆文字", 3, 1, 21, 0, 2, 0, 3, 1, 1, None, 0,
     ["学习", "答辩", "课程设计"]),
    ("复习并查集", "路径压缩 + 按秩合并", 2, 2, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("整理答辩讲稿", "按老师可能问的顺序排", 2, 2, 15, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "答辩"]),
    ("复习哈希表", "冲突处理与装载因子", 2, 3, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("打印装订报告", "三份，封面用彩打", 1, 3, 16, 0, 2, 0, 3, 0, 0, None, 0, ["课程设计"]),
    ("复习 Trie", "共享前缀与删除剪枝", 2, 4, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("答辩彩排", "计时 11 分钟，组内互问", 3, 5, 14, 0, 2, 0, 3, 1, 1, 5, 0, ["学习", "答辩"]),
    ("复习排序算法", "快排、归并与稳定性", 2, 5, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("提交结题材料", "含源码、报告、演示视频", 3, 6, 17, 0, 2, 0, 3, 1, 0, 6, 0,
     ["学习", "课程设计", "答辩"]),
    ("体检预约", "空腹，上午", 1, 6, 9, 0, 3, 0, 4, 0, 0, None, 0, ["健康"]),
    ("写周报", "本周进展 + 下周计划", 2, 7, 18, 0, 1, 0, 2, 0, 0, None, 3, ["工作"]),
    ("读《算法导论》第 3 章", "函数的增长", 1, 7, 21, 0, 2, 0, 5, 0, 0, None, 0, ["阅读", "学习"]),

    # ---- 未来两周 ----
    ("复习动态规划", "背包与最长公共子序列", 2, 8, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("论文投稿材料", "正文 + 图表 + 作者信息", 3, 10, 17, 0, 1, 0, 7, 1, 0, 10, 0, ["论文", "工作"]),
    ("组会汇报幻灯片", "讲清这周做了什么", 2, 10, 10, 0, 1, 0, 2, 0, 0, None, 0, ["工作"]),
    ("论文图表重绘", "统一字号与配色", 2, 12, 15, 0, 1, 0, 7, 0, 0, None, 0, ["论文"]),
    ("月度复盘", "看看哪些事真的推进了", 1, 14, 18, 0, 1, 0, 2, 0, 0, None, 4, ["工作"]),
    ("半程马拉松训练", "15 公里长距离", 1, 14, 7, 0, 3, 0, 4, 0, 0, None, 3, ["健康"]),
    ("复习字符串匹配", "KMP 的 next 数组", 2, 16, 20, 0, 2, 0, 3, 0, 0, None, 0, ["学习", "复习"]),
    ("毕业论文开题", "与导师确认选题范围", 3, 18, 10, 0, 1, 0, 6, 1, 0, None, 0, ["论文", "工作"]),
    ("投稿截止前复核", "检查参考文献格式", 3, 21, 23, 0, 1, 0, 7, 1, 0, None, 0, ["论文"]),
    ("学期总结", "课程设计写进简历", 1, 25, 18, 0, 1, 0, 2, 0, 0, None, 0, ["工作"]),

    # ---- 无截止时间 ----
    ("图书馆还书", "两本，别逾期", 2, None, 0, 0, 0, 0, 5, 0, 1, None, 0, ["阅读"]),
    ("整理桌面文件", "", 1, None, 0, 0, 0, 2, 1, 0, 0, None, 0, ["工作"]),
    ("更新简历", "补上课程设计项目", 2, None, 0, 0, 1, 0, 6, 0, 0, None, 0, ["工作"]),
    ("学习 Git 高级用法", "rebase、cherry-pick", 1, None, 0, 0, 2, 1, 3, 0, 0, None, 0, ["学习"]),
    ("买显示器和键盘", "", 0, None, 0, 0, 0, 2, 1, 0, 0, None, 0, ["生活"]),
    ("备份实验数据", "本地 + 网盘各一份", 2, None, 0, 0, 0, 2, 1, 0, 0, None, 0, ["工作"]),
    ("整理答辩问题清单", "预估 20 个问题", 2, None, 0, 0, 2, 1, 3, 0, 0, None, 0, ["答辩"]),
    ("给导师发进度汇报", "暂缓，等数据齐了再发", 1, None, 0, 0, 1, 3, 2, 0, 0, None, 0, ["工作"]),
    ("读完《数据结构与算法分析》", "第 6 章优先", 1, None, 0, 0, 2, 1, 5, 0, 0, None, 0, ["阅读", "学习"]),
]

SUBTASKS = {
    "写课程设计报告": [("收集资料", True), ("写正文", True), ("补数据结构一节", False),
                       ("统一图表编号", False)],
    "复习线段树": [("区间和", True), ("区间最大值", False), ("热力图怎么画", False)],
    "写周报": [("汇总本周提交", True), ("列下周计划", False)],
    "论文投稿材料": [("正文定稿", True), ("图表导出 300dpi", True), ("作者信息核对", False),
                     ("投稿系统填表", False)],
    "毕业论文开题": [("读 5 篇近三年论文", True), ("写开题报告", False), ("做开题 PPT", False)],
    "复习并查集": [("路径压缩", True), ("按秩合并", False)],
}


def stamp(day_offset: int, hour: int, minute: int = 0) -> str:
    d = TODAY + dt.timedelta(days=day_offset)
    return d.strftime("%Y-%m-%d") + " %02d:%02d" % (hour, minute)


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    con = sqlite3.connect(str(out))
    cur = con.cursor()
    for sql in DDL:
        cur.execute(sql)

    for (lid, name, color, group, is_default) in LISTS:
        cur.execute(
            "INSERT INTO lists (id,name,color,group_id,created_at,is_default) "
            "VALUES (?,?,?,?,?,?)",
            (lid, name, color, group, "2026-10-01 09:%02d" % (10 + lid), is_default))

    manual_order = {}
    for tid, row in enumerate(T):
        (title, details, prio, off, hh, mm, cat, status, lst,
         star, my_day, remind_off, recurr, tags) = row
        deadline = "" if off is None else stamp(off, hh, mm)
        created = stamp(-8 + tid % 5, 9, tid % 60)
        reminder = "" if remind_off is None else stamp(remind_off, 16, 0)
        completed = ""
        if status == 2:
            completed = stamp(-1 if off is None or off > 0 else off, 20, 0)
        order = manual_order.get(lst, 0)
        manual_order[lst] = order + 1
        cur.execute(
            "INSERT INTO tasks (id,title,details,priority,deadline,category_id,status,"
            "created_at,completed_at,list_id,starred,in_my_day,reminder,recurrence,"
            "tags,manual_order) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (tid, title, details, prio, deadline, cat, status, created, completed, lst,
             star, my_day, reminder, recurr, "\x1f".join(tags), order))

    sid = 0
    for tid, row in enumerate(T):
        for pos, (st, done) in enumerate(SUBTASKS.get(row[0], [])):
            cur.execute(
                "INSERT INTO subtasks (id,task_id,position,title,done) VALUES (?,?,?,?,?)",
                (sid, tid, pos, st, 1 if done else 0))
            sid += 1

    con.commit()
    cur.execute("VACUUM")
    con.commit()

    n_task = cur.execute("SELECT COUNT(*) FROM tasks").fetchone()[0]
    n_list = cur.execute("SELECT COUNT(*) FROM lists").fetchone()[0]
    n_sub = cur.execute("SELECT COUNT(*) FROM subtasks").fetchone()[0]
    overdue = cur.execute("SELECT COUNT(*) FROM tasks WHERE deadline<>'' AND deadline<?",
                          (stamp(0, 0, 0),)).fetchone()[0]
    today_due = cur.execute(
        "SELECT COUNT(*) FROM tasks WHERE deadline LIKE ? AND status<>2",
        (TODAY.strftime("%Y-%m-%d") + "%",)).fetchone()[0]
    con.close()

    print("已生成 %s" % out)
    print("  任务 %d 条 / 列表 %d 个 / 子任务 %d 条" % (n_task, n_list, n_sub))
    print("  逾期 %d 条，今天到期 %d 条（相对 %s）" % (overdue, today_due, TODAY))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
