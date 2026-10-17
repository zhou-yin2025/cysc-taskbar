#ifndef VECTOR_H
#define VECTOR_H

// ============================================================================
//  Vector.h
//  职责：模板动态数组（深拷贝/扩容/归并排序）
//  依赖：无
// ============================================================================

#include <stdexcept>

template <typename T>
class Vector {
public:
    Vector(int initialCapacity = 10)
        : currentSize(0), currentCapacity(initialCapacity) {
        data = new T[currentCapacity];
    }

    ~Vector() {
        delete[] data;
    }

    // Deep-copy constructor
    Vector(const Vector& other) {
        currentSize = other.currentSize;
        currentCapacity = other.currentCapacity;
        data = new T[currentCapacity];
        for (int i = 0; i < currentSize; ++i) {
            data[i] = other.data[i];
        }
    }

    // Deep-copy assignment operator
    Vector& operator=(const Vector& other) {
        if (this == &other) return *this;
        delete[] data;
        currentSize = other.currentSize;
        currentCapacity = other.currentCapacity;
        data = new T[currentCapacity];
        for (int i = 0; i < currentSize; ++i) {
            data[i] = other.data[i];
        }
        return *this;
    }

    void pushBack(const T& item) {
        if (currentSize >= currentCapacity) {
            resize(currentCapacity * 2);
        }
        data[currentSize++] = item;
    }

    void insert(int index, const T& item) {
        if (index < 0 || index > currentSize) {
            throw std::out_of_range("Vector::insert index out of range");
        }
        if (currentSize >= currentCapacity) {
            resize(currentCapacity * 2);
        }
        for (int i = currentSize; i > index; --i) {
            data[i] = data[i - 1];
        }
        data[index] = item;
        currentSize++;
    }

    void remove(int index) {
        if (index < 0 || index >= currentSize) {
            throw std::out_of_range("Vector::remove index out of range");
        }
        for (int i = index; i < currentSize - 1; ++i) {
            data[i] = data[i + 1];
        }
        currentSize--;
    }

    T& at(int index) {
        if (index < 0 || index >= currentSize) {
            throw std::out_of_range("Vector::at index out of range");
        }
        return data[index];
    }

    const T& at(int index) const {
        if (index < 0 || index >= currentSize) {
            throw std::out_of_range("Vector::at index out of range");
        }
        return data[index];
    }

    T& operator[](int index) { return data[index]; }
    const T& operator[](int index) const { return data[index]; }

    // Raw-pointer iterators, so the container works with the standard algorithms.
    T* begin() { return data; }
    T* end() { return data + currentSize; }
    const T* begin() const { return data; }
    const T* end() const { return data + currentSize; }

    int size() const { return currentSize; }
    int capacity() const { return currentCapacity; }
    bool isEmpty() const { return currentSize == 0; }

    void clear() { currentSize = 0; }

    bool containsValue(const T& value) const {
        for (int i = 0; i < currentSize; ++i) {
            if (data[i] == value) return true;
        }
        return false;
    }

    int indexOf(const T& value) const {
        for (int i = 0; i < currentSize; ++i) {
            if (data[i] == value) return i;
        }
        return -1;
    }

    void sort(bool (*cmp)(const T&, const T&)) {
        if (currentSize > 1) {
            mergeSort(0, currentSize - 1, cmp);
        }
    }

private:
    T* data;
    int currentSize;
    int currentCapacity;

    void resize(int newCapacity) {
        T* newData = new T[newCapacity];
        for (int i = 0; i < currentSize; ++i) {
            newData[i] = data[i];
        }
        delete[] data;
        data = newData;
        currentCapacity = newCapacity;
    }

    void mergeSort(int left, int right, bool (*cmp)(const T&, const T&)) {
        if (left >= right) return;
        int mid = left + (right - left) / 2;
        mergeSort(left, mid, cmp);
        mergeSort(mid + 1, right, cmp);
        merge(left, mid, right, cmp);
    }

    void merge(int left, int mid, int right, bool (*cmp)(const T&, const T&)) {
        int n1 = mid - left + 1;
        int n2 = right - mid;

        T* leftArr = new T[n1];
        T* rightArr = new T[n2];

        for (int i = 0; i < n1; ++i) leftArr[i] = data[left + i];
        for (int i = 0; i < n2; ++i) rightArr[i] = data[mid + 1 + i];

        int i = 0, j = 0, k = left;
        while (i < n1 && j < n2) {
            if (cmp(leftArr[i], rightArr[j])) {
                data[k++] = leftArr[i++];
            } else {
                data[k++] = rightArr[j++];
            }
        }
        while (i < n1) data[k++] = leftArr[i++];
        while (j < n2) data[k++] = rightArr[j++];

        delete[] leftArr;
        delete[] rightArr;
    }
};

#endif
