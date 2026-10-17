#ifndef SEGMENTTREE_H
#define SEGMENTTREE_H

// ============================================================================
//  SegmentTree.h
//  职责：线段树：区间和/区间最大值；截止日期区间统计与热力图
//  依赖：无
// ============================================================================

#include <algorithm>
#include <stdexcept>
#include <vector>

// 线段树（区间和 / 区间最大值）。
//
// 用途：统计任意日期区间内的任务数量（"未来 7 天有多少件事"）、
//       按天着色画出热力图、统计逾期数量。
// 设计要点：
//   * 叶结点对应一天（下标即"距离基准日的天数"）；
//   * 建树 O(n)，单点更新 O(log n)，区间查询 O(log n)；
//   * 用 4n 的数组存放，避免动态指针。
class SegmentTree {
public:
    explicit SegmentTree(long long days = 0) {
        resize(days);
    }

    // 重置为 days 天，全部清零。
    void resize(long long days) {
        m_days = days < 0 ? 0 : days;
        m_size = m_days == 0 ? 0 : 1;
        while (m_size < m_days) m_size <<= 1;
        const std::size_t nodes = m_size == 0 ? 0 : m_size * 2;
        m_sum.assign(nodes, 0);
        m_max.assign(nodes, 0);
    }

    long long days() const { return m_days; }

    // 第 day 天增加 delta（可为负）。
    void add(long long day, long long delta) {
        if (day < 0 || day >= m_days) {
            throw std::out_of_range("SegmentTree::add day out of range");
        }
        addImpl(1, 0, m_size - 1, day, delta);
    }

    // 把第 day 天设为 value。
    void set(long long day, long long value) {
        add(day, value - pointValue(day));
    }

    long long pointValue(long long day) const {
        if (day < 0 || day >= m_days) {
            throw std::out_of_range("SegmentTree::pointValue day out of range");
        }
        return querySum(day, day);
    }

    // 闭区间 [from, to] 的和。
    long long querySum(long long from, long long to) const {
        if (m_days == 0) return 0;
        if (from > to) std::swap(from, to);
        from = std::max<long long>(from, 0);
        to = std::min<long long>(to, m_days - 1);
        if (from > to) return 0;
        return querySumImpl(1, 0, m_size - 1, from, to);
    }

    // 闭区间 [from, to] 的最大值；全为 0 时返回 0。
    long long queryMax(long long from, long long to) const {
        if (m_days == 0) return 0;
        if (from > to) std::swap(from, to);
        from = std::max<long long>(from, 0);
        to = std::min<long long>(to, m_days - 1);
        if (from > to) return 0;
        return queryMaxImpl(1, 0, m_size - 1, from, to);
    }

    // 整棵树的和（等价于 querySum(0, days-1)，O(1)）。
    long long total() const { return m_size == 0 ? 0 : m_sum[1]; }

    // 大于 0 的天数：用于"有计划的天数"。
    long long nonZeroDays() const {
        long long count = 0;
        for (long long d = 0; d < m_days; ++d) {
            if (pointValue(d) > 0) ++count;
        }
        return count;
    }

    // ---- 只读统计（「数据结构可视化面板」用）----
    // 补齐到 2 的幂之后的叶子数（树的宽度）。
    long long leafSlots() const { return m_size; }
    // 整棵树的结点总数（含内部结点与未使用的填充叶子）。
    long long nodeSlots() const { return m_size == 0 ? 0LL : m_size * 2; }
    // 树的深度（层数）；m_size 是 2 的幂，所以 log2 + 1。
    int depth() const {
        int d = 0;
        for (long long n = m_size; n > 0; n >>= 1) ++d;
        return d;
    }
    // 内部结点当前的区间和（供面板自上而下展示，index 1 是根）。
    long long nodeSum(long long index) const {
        if (index <= 0 || static_cast<std::size_t>(index) >= m_sum.size()) return 0;
        return m_sum[static_cast<std::size_t>(index)];
    }
    // 内部结点当前的区间最大值。
    long long nodeMax(long long index) const {
        if (index <= 0 || static_cast<std::size_t>(index) >= m_max.size()) return 0;
        return m_max[static_cast<std::size_t>(index)];
    }

private:
    void addImpl(long long node, long long nodeLeft, long long nodeRight,
                 long long day, long long delta) {
        if (nodeLeft == nodeRight) {
            m_sum[node] += delta;
            m_max[node] = m_sum[node];
            return;
        }
        const long long mid = nodeLeft + (nodeRight - nodeLeft) / 2;
        if (day <= mid) {
            addImpl(node * 2, nodeLeft, mid, day, delta);
        } else {
            addImpl(node * 2 + 1, mid + 1, nodeRight, day, delta);
        }
        m_sum[node] = m_sum[node * 2] + m_sum[node * 2 + 1];
        m_max[node] = std::max(m_max[node * 2], m_max[node * 2 + 1]);
    }

    long long querySumImpl(long long node, long long nodeLeft, long long nodeRight,
                           long long from, long long to) const {
        if (to < nodeLeft || nodeRight < from) return 0;         // 完全不相交
        if (from <= nodeLeft && nodeRight <= to) return m_sum[node];   // 完全覆盖
        const long long mid = nodeLeft + (nodeRight - nodeLeft) / 2;
        return querySumImpl(node * 2, nodeLeft, mid, from, to)
               + querySumImpl(node * 2 + 1, mid + 1, nodeRight, from, to);
    }

    long long queryMaxImpl(long long node, long long nodeLeft, long long nodeRight,
                           long long from, long long to) const {
        if (to < nodeLeft || nodeRight < from) return 0;
        if (from <= nodeLeft && nodeRight <= to) return m_max[node];
        const long long mid = nodeLeft + (nodeRight - nodeLeft) / 2;
        // 用 max(左, 右) 即可：空区间返回 0，而计数不会为负。
        return std::max(queryMaxImpl(node * 2, nodeLeft, mid, from, to),
                        queryMaxImpl(node * 2 + 1, mid + 1, nodeRight, from, to));
    }

    long long m_days = 0;      // 实际天数
    long long m_size = 0;      // 补齐到 2 的幂后的叶子数
    std::vector<long long> m_sum;
    std::vector<long long> m_max;
};

#endif
