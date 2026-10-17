#include "Trie.h"

Trie::TrieNode::TrieNode() : isEnd(false) {
    for (int i = 0; i < 256; ++i) next[i] = nullptr;
}

Trie::TrieNode::~TrieNode() {
    for (int i = 0; i < 256; ++i) if (next[i]) delete next[i];
}

Trie::Trie() { root = new TrieNode(); }

Trie::Trie(const Trie& other) {
    root = new TrieNode();
    copyHelper(other.root, root);
}

Trie& Trie::operator=(const Trie& other) {
    if (this == &other) return *this;
    delete root;
    root = new TrieNode();
    copyHelper(other.root, root);
    return *this;
}

Trie::~Trie() { delete root; }

// ---------------------------------------------------------- 只读统计

int Trie::countNodes(TrieNode* node) {
    if (!node) return 0;
    int total = 1;
    for (int i = 0; i < 256; ++i) {
        if (node->next[i]) total += countNodes(node->next[i]);
    }
    return total;
}

int Trie::countEnds(TrieNode* node) {
    if (!node) return 0;
    int total = node->isEnd ? 1 : 0;
    for (int i = 0; i < 256; ++i) {
        if (node->next[i]) total += countEnds(node->next[i]);
    }
    return total;
}

int Trie::nodeCount() const {
    // 根结点不算：它只是入口，不代表任何字符。
    return root ? countNodes(root) - 1 : 0;
}

int Trie::wordCount() const {
    return root ? countEnds(root) : 0;
}

bool Trie::hasPrefixPath(const std::string& prefix) const {
    TrieNode* node = root;
    for (unsigned char c : prefix) {
        if (!node || !node->next[c]) return false;
        node = node->next[c];
    }
    return node != nullptr;
}

int Trie::subtreeNodeCount(const std::string& prefix) const {
    if (prefix.empty()) return nodeCount();
    TrieNode* node = root;
    for (unsigned char c : prefix) {
        if (!node || !node->next[c]) return 0;
        node = node->next[c];
    }
    return countNodes(node);
}

void Trie::copyHelper(TrieNode* src, TrieNode* dst) {
    dst->isEnd = src->isEnd;
    for (int i = 0; i < 256; ++i) {
        if (src->next[i]) {
            dst->next[i] = new TrieNode();
            copyHelper(src->next[i], dst->next[i]);
        }
    }
}

void Trie::insert(const std::string& word) {
    TrieNode* node = root;
    for (unsigned char c : word) {
        if (!node->next[c]) node->next[c] = new TrieNode();
        node = node->next[c];
    }
    node->isEnd = true;
}

bool Trie::search(const std::string& word) const {
    TrieNode* node = root;
    for (unsigned char c : word) {
        if (!node->next[c]) return false;
        node = node->next[c];
    }
    return node->isEnd;
}

// Returns true when `node` is now childless and may be pruned, so the caller
// (its parent) can drop the pointer. Note that this is about pruning, not about
// whether the word existed: a word whose path has branching siblings is still
// deleted successfully while its nodes are kept.
bool Trie::deleteHelper(TrieNode* node, const std::string& word, int depth) {
    if (!node) return false;

    if (depth == (int)word.size()) {
        if (!node->isEnd) return false;    // the word was never stored
        node->isEnd = false;
        for (int i = 0; i < 256; ++i) {
            if (node->next[i]) return false;   // prefix of longer words: keep it
        }
        return true;
    }

    const unsigned char c = word[depth];
    if (!node->next[c]) return false;
    if (!deleteHelper(node->next[c], word, depth + 1)) return false;

    // The child reported that it became childless and holds no word.
    delete node->next[c];
    node->next[c] = nullptr;

    if (node->isEnd) return false;         // this node is a word of its own
    for (int i = 0; i < 256; ++i) {
        if (node->next[i]) return false;   // still needed by another word
    }
    return true;
}

bool Trie::deleteWord(const std::string& word) {
    if (word.empty() || !search(word)) {
        return false;
    }
    // Ask for pruning, then report what the caller actually cares about: the
    // word was present and is now gone. The root itself is never removed.
    deleteHelper(root, word, 0);
    return !search(word);
}

std::vector<std::string> Trie::getWordsWithPrefix(const std::string& prefix) const {
    std::vector<std::string> results;
    TrieNode* node = root;
    for (unsigned char c : prefix) {
        if (!node->next[c]) return results;
        node = node->next[c];
    }
    std::string current = prefix;
    collectWords(node, current, results);
    return results;
}

void Trie::collectWords(TrieNode* node, std::string& current,
                        std::vector<std::string>& results) const {
    if (!node) return;
    if (node->isEnd) results.push_back(current);
    for (int i = 0; i < 256; ++i) {
        if (node->next[i]) {
            current.push_back((char)i);
            collectWords(node->next[i], current, results);
            current.pop_back();
        }
    }
}
