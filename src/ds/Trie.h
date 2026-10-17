#ifndef TRIE_H
#define TRIE_H

// ============================================================================
//  Trie.h
//  职责：256 叉前缀树：插入/查找/删词/前缀取词；标题前缀索引
//  依赖：无
// ============================================================================

#include <string>
#include <vector>

class Trie {
public:
    Trie();
    Trie(const Trie& other);
    Trie& operator=(const Trie& other);
    ~Trie();

    void insert(const std::string& word);
    bool search(const std::string& word) const;
    bool deleteWord(const std::string& word);
    std::vector<std::string> getWordsWithPrefix(const std::string& prefix) const;

    // ---- 只读统计（「数据结构可视化面板」用）----
    // 整棵树的结点数（不含根）。
    int nodeCount() const;
    // 以 prefix 结尾的那个结点为根的子树结点数；前缀不存在返回 0。
    // 用来直观说明「共享前缀省了多少结点」。
    int subtreeNodeCount(const std::string& prefix) const;
    // 已存单词数（isEnd 为真的结点数）。
    int wordCount() const;
    // 从根出发、以 prefix 为路径的结点是否存在。
    bool hasPrefixPath(const std::string& prefix) const;

private:
    struct TrieNode {
        TrieNode* next[256];
        bool isEnd;
        TrieNode();
        ~TrieNode();
    };

    TrieNode* root;

    void collectWords(TrieNode* node, std::string& current,
                      std::vector<std::string>& results) const;
    bool deleteHelper(TrieNode* node, const std::string& word, int depth);
    void copyHelper(TrieNode* src, TrieNode* dst);
    static int countNodes(TrieNode* node);
    static int countEnds(TrieNode* node);
};

#endif
