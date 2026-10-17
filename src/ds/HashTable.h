#ifndef HASHTABLE_H
#define HASHTABLE_H

// ============================================================================
//  HashTable.h
//  职责：链式哈希表：质数桶容量/装载因子扩容/可换哈希函子；标签倒排索引与重复标题检测
//  依赖：无
// ============================================================================

#include <cstddef>
#include <functional>
#include <list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 链式哈希表（separate chaining）。
//
// 用途：标签 -> 任务编号 的倒排索引、任务标题去重。
// 设计要点：
//   * 桶数组容量保持为质数，配合除留余数法，减少聚集；
//   * 装载因子超过阈值就扩容并把所有元素重新散列（rehash）；
//   * 每个桶用 std::list 存冲突元素，链长即冲突次数的直观体现；
//   * 哈希函数可替换：默认 std::hash<K>，测试或特殊键可传入自己的函子
//     （注意标准库对 int 之类的哈希实现相关，并不保证是恒等映射）。
template <typename K, typename V, typename Hash = std::hash<K>>
class HashTable {
public:
    using Pair = std::pair<K, V>;
    using Bucket = std::list<Pair>;

    explicit HashTable(std::size_t initialBuckets = 17, double maxLoad = 0.75)
        : m_buckets(nextPrime(initialBuckets < 3 ? 3 : initialBuckets)),
          m_maxLoad(maxLoad > 0.1 ? maxLoad : 0.75) {
        m_table.resize(m_buckets);
    }

    // 插入或覆盖。返回 true 表示新建了键，false 表示覆盖了旧值。
    bool insert(const K& key, const V& value) {
        Bucket& bucket = m_table[bucketIndex(key)];
        for (Pair& entry : bucket) {
            if (entry.first == key) {
                entry.second = value;
                return false;
            }
        }
        bucket.emplace_back(key, value);
        ++m_size;
        if (loadFactor() > m_maxLoad) {
            rehash(nextPrime(m_buckets * 2 + 1));
        }
        return true;
    }

    bool contains(const K& key) const {
        const Bucket& bucket = m_table[bucketIndex(key)];
        for (const Pair& entry : bucket) {
            if (entry.first == key) return true;
        }
        return false;
    }

    // 查找；未命中返回 fallback，避免对 V 要求默认构造。
    V value(const K& key, const V& fallback = V()) const {
        const Bucket& bucket = m_table[bucketIndex(key)];
        for (const Pair& entry : bucket) {
            if (entry.first == key) return entry.second;
        }
        return fallback;
    }

    // 命中则写入 out 并返回 true。
    bool tryGet(const K& key, V& out) const {
        const Bucket& bucket = m_table[bucketIndex(key)];
        for (const Pair& entry : bucket) {
            if (entry.first == key) {
                out = entry.second;
                return true;
            }
        }
        return false;
    }

    // 引用式访问：不存在则插入默认值（与 std::unordered_map 语义一致）。
    V& operator[](const K& key) {
        Bucket& bucket = m_table[bucketIndex(key)];
        for (Pair& entry : bucket) {
            if (entry.first == key) return entry.second;
        }
        bucket.emplace_back(key, V());
        ++m_size;
        if (loadFactor() > m_maxLoad) {
            rehash(nextPrime(m_buckets * 2 + 1));
            // rehash 后原引用可能失效，重新定位
            Bucket& moved = m_table[bucketIndex(key)];
            for (Pair& entry : moved) {
                if (entry.first == key) return entry.second;
            }
        }
        return bucket.back().second;
    }

    bool remove(const K& key) {
        Bucket& bucket = m_table[bucketIndex(key)];
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if (it->first == key) {
                bucket.erase(it);
                --m_size;
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (Bucket& bucket : m_table) {
            bucket.clear();
        }
        m_size = 0;
    }

    std::size_t size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    std::size_t bucketCount() const { return m_buckets; }
    double loadFactor() const {
        return m_buckets == 0 ? 0.0
                              : static_cast<double>(m_size) / static_cast<double>(m_buckets);
    }

    // 最长的冲突链长度：散列质量差时这个值会明显变大。
    std::size_t longestChain() const {
        std::size_t longest = 0;
        for (const Bucket& bucket : m_table) {
            longest = bucket.size() > longest ? bucket.size() : longest;
        }
        return longest;
    }

    // 所有键（顺序不保证，主要用于遍历与测试）。
    std::vector<K> keys() const {
        std::vector<K> out;
        out.reserve(m_size);
        for (const Bucket& bucket : m_table) {
            for (const Pair& entry : bucket) {
                out.push_back(entry.first);
            }
        }
        return out;
    }

    // 所有键值对。
    std::vector<Pair> entries() const {
        std::vector<Pair> out;
        out.reserve(m_size);
        for (const Bucket& bucket : m_table) {
            for (const Pair& entry : bucket) {
                out.push_back(entry);
            }
        }
        return out;
    }

    // 遍历某个桶（功能演示"冲突链"用）。
    const Bucket& bucketAt(std::size_t index) const {
        if (index >= m_buckets) throw std::out_of_range("HashTable::bucketAt out of range");
        return m_table[index];
    }

    // 只读统计：给「数据结构可视化面板」用。
    // 返回每个桶的长度（下标 = 桶序号），不改动表本身。
    std::vector<std::size_t> bucketSizes() const {
        std::vector<std::size_t> out;
        out.reserve(m_buckets);
        for (const Bucket& bucket : m_table) out.push_back(bucket.size());
        return out;
    }

    // 非空桶的数量：衡量散列是否均匀。
    std::size_t usedBucketCount() const {
        std::size_t n = 0;
        for (const Bucket& bucket : m_table) {
            if (!bucket.empty()) ++n;
        }
        return n;
    }

    // 命中某键需要比较的次数（1 表示第一个就中）。未命中返回链长。
    std::size_t probeLength(const K& key) const {
        const Bucket& bucket = m_table[bucketIndex(key)];
        std::size_t steps = 0;
        for (const Pair& entry : bucket) {
            ++steps;
            if (entry.first == key) return steps;
        }
        return steps;
    }

    // 质数序列：容量取质数能降低除留余数法的聚集。
    static std::size_t nextPrime(std::size_t n) {
        if (n < 2) return 2;
        while (!isPrime(n)) ++n;
        return n;
    }

    static bool isPrime(std::size_t n) {
        if (n < 2) return false;
        if (n % 2 == 0) return n == 2;
        for (std::size_t d = 3; d * d <= n; d += 2) {
            if (n % d == 0) return false;
        }
        return true;
    }

private:
    std::size_t bucketIndex(const K& key) const {
        return static_cast<std::size_t>(m_hasher(key) % m_buckets);
    }

    void rehash(std::size_t newBuckets) {
        std::vector<Bucket> fresh(newBuckets);
        for (Bucket& bucket : m_table) {
            for (Pair& entry : bucket) {
                const std::size_t index =
                    static_cast<std::size_t>(m_hasher(entry.first) % newBuckets);
                fresh[index].push_back(std::move(entry));
            }
            bucket.clear();
        }
        m_table.swap(fresh);
        m_buckets = newBuckets;
    }

    std::vector<Bucket> m_table;
    std::size_t m_buckets;
    std::size_t m_size = 0;
    double m_maxLoad;
    Hash m_hasher = Hash();
};

#endif
