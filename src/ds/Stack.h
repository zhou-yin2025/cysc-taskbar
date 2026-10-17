#ifndef STACK_H
#define STACK_H

// ============================================================================
//  Stack.h
//  职责：模板栈（自动扩容）
//  依赖：无
// ============================================================================

#include <stdexcept>

template <typename T>
class Stack {
public:
    Stack(int maxSize = 20)
        : capacity(maxSize), top(-1) {
        data = new T[capacity];
    }

    ~Stack() {
        delete[] data;
    }

    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;

    void push(const T& item) {
        if (isFull()) {
            resize(capacity * 2);
        }
        data[++top] = item;
    }

    T pop() {
        if (isEmpty()) {
            throw std::out_of_range("Stack::pop on empty stack");
        }
        return data[top--];
    }

    T& peek() {
        if (isEmpty()) {
            throw std::out_of_range("Stack::peek on empty stack");
        }
        return data[top];
    }

    const T& peek() const {
        if (isEmpty()) {
            throw std::out_of_range("Stack::peek on empty stack");
        }
        return data[top];
    }

    bool isEmpty() const { return top == -1; }
    bool isFull() const { return top == capacity - 1; }
    int size() const { return top + 1; }

    void clear() { top = -1; }

private:
    T* data;
    int capacity;
    int top;

    void resize(int newCapacity) {
        T* newData = new T[newCapacity];
        for (int i = 0; i <= top; ++i) {
            newData[i] = data[i];
        }
        delete[] data;
        data = newData;
        capacity = newCapacity;
    }
};

#endif
