#ifndef UNIONFIND_H
#define UNIONFIND_H

// ============================================================================
//  UnionFind.h
//  职责：并查集：路径压缩 + 按秩合并；列表分组
//  依赖：ds/Vector.h
// ============================================================================

#include <stdexcept>
#include "Vector.h"

// 并查集（不相交集合）。路径压缩 + 按秩合并，均摊复杂度接近 O(1)。
//
// 用途：把若干任务列表合并成「分组」（对应 Microsoft To Do 的列表分组），
//       以及标签合并（把"工作"和"Work"并到一起）。
// 设计要点：
//   * parent[i] 记录父结点，根结点指向自己；
//   * rank[i] 记录树高上界，合并时矮树挂到高树上；
//   * find 采用路径压缩，把路径上的结点直接挂到根，摊平后续查询。
class UnionFind {
public:
    explicit UnionFind(int count = 0) {
        reset(count);
    }

    // 重建为 count 个互不相交的单元素集合。
    void reset(int count) {
        if (count < 0) throw std::invalid_argument("UnionFind::reset negative count");
        m_count = count;
        m_sets = count;
        m_parent.clear();
        m_rank.clear();
        for (int i = 0; i < count; ++i) {
            m_parent.pushBack(i);
            m_rank.pushBack(0);
        }
    }

    int size() const { return m_count; }
    // 当前集合个数（每次成功合并都会减一）。
    int setCount() const { return m_sets; }

    // 查找所在集合的代表元；带路径压缩。
    int find(int x) {
        checkIndex(x);
        int root = x;
        while (m_parent[root] != root) {
            root = m_parent[root];
        }
        // 路径压缩：把沿途结点直接接到根上
        while (m_parent[x] != root) {
            const int next = m_parent[x];
            m_parent[x] = root;
            x = next;
        }
        return root;
    }

    // 合并两个元素所在集合。已经是同一集合则返回 false。
    bool join(int a, int b) {
        int rootA = find(a);
        int rootB = find(b);
        if (rootA == rootB) return false;

        // 按秩合并：矮树挂到高树下
        if (m_rank[rootA] < m_rank[rootB]) {
            m_parent[rootA] = rootB;
        } else if (m_rank[rootA] > m_rank[rootB]) {
            m_parent[rootB] = rootA;
        } else {
            m_parent[rootB] = rootA;
            ++m_rank[rootA];
        }
        --m_sets;
        return true;
    }

    bool sameSet(int a, int b) {
        return find(a) == find(b);
    }

    // 某个集合的大小（需要遍历统计）。
    int setSize(int x) {
        const int root = find(x);
        int total = 0;
        for (int i = 0; i < m_count; ++i) {
            if (find(i) == root) ++total;
        }
        return total;
    }

    // 每个集合的代表元列表。
    Vector<int> roots() {
        Vector<int> out;
        for (int i = 0; i < m_count; ++i) {
            const int root = find(i);
            if (!out.containsValue(root)) out.pushBack(root);
        }
        return out;
    }

    // 某个集合的全部成员。
    Vector<int> members(int x) {
        Vector<int> out;
        const int root = find(x);
        for (int i = 0; i < m_count; ++i) {
            if (find(i) == root) out.pushBack(i);
        }
        return out;
    }

    int rankOf(int x) {
        return m_rank[find(x)];
    }

    // ---- 只读统计（「数据结构可视化面板」用）----
    // parent[i]：元素 i 的父结点（根指向自己）。不触发路径压缩。
    int parentOf(int x) const {
        if (x < 0 || x >= m_count) return -1;
        return m_parent[x];
    }
    // rank[i]：按秩合并用的秩上界。
    int rankAt(int x) const {
        if (x < 0 || x >= m_count) return -1;
        return m_rank[x];
    }
    // 不看压缩、直接沿 parent 走到根需要几步：直观体现「路径压缩前后」的差别。
    int chainLengthToRoot(int x) const {
        if (x < 0 || x >= m_count) return -1;
        int steps = 0;
        int cur = x;
        while (cur >= 0 && cur < m_count && m_parent[cur] != cur) {
            cur = m_parent[cur];
            ++steps;
            if (steps > m_count) break;      // 防御：不该出现环
        }
        return steps;
    }

private:
    void checkIndex(int x) const {
        if (x < 0 || x >= m_count) {
            throw std::out_of_range("UnionFind index out of range");
        }
    }

    Vector<int> m_parent;
    Vector<int> m_rank;
    int m_count = 0;
    int m_sets = 0;
};

#endif
