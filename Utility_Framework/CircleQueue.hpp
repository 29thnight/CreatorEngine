#include <iostream>
#include <vector>
template <typename T>
class CircularQueue {
private:
    std::vector<T> arr; // ť �����͸� ������ �迭
    int capacity;       // ť�� �ִ� �뷮 (���� ���� ������ ��Ҵ� capacity - 1)
    int head;           // ť�� ù ��° ��Ҹ� ����Ű�� �ε���
    int tail;           // ť�� ���� ��Ұ� �߰��� ��ġ�� ����Ű�� �ε���
    int count;          // ���� ť�� ����� ����� ����

public:
    CircularQueue() :
        capacity(1), // �ּ� �뷮�� 1 (���� ���� ���� ��� 0)
        arr(1),
        head(0),
        tail(0),
        count(0)
    {
    }
    // ������: ť�� �뷮�� �ʱ�ȭ�մϴ�.
    // ���� ���� ������ ����� ������ capacity - 1 �Դϴ�.
    // �̴� ť�� ���� á���� (head == tail && count == capacity - 1)��
    // ť�� ������� (head == tail && count == 0)�� �����ϱ� �����Դϴ�.
    explicit CircularQueue(int size) :
        capacity(size + 1), // ť�� ���� á�� �� �� ĭ�� ����α� ���� +1
        arr(size + 1),
        head(0),
        tail(0),
        count(0)
    {
        if (size <= 0) {
            throw std::invalid_argument("Queue size must be greater than 0.");
        }
    }

    // ť�� ��Ҹ� �߰��մϴ�. (Enqueue)
    void enqueue(const T& item) {
        if (isFull()) {
            throw std::overflow_error("Queue is full. Cannot enqueue item.");
        }
        arr[tail] = item;
        tail = (tail + 1) % capacity;
        count++;
    }

    // ť���� ��Ҹ� �����ϰ� ��ȯ�մϴ�. (Dequeue)
    T dequeue() {
        if (isEmpty()) {
            throw std::underflow_error("Queue is empty. Cannot dequeue item.");
        }
        T item = arr[head];
        head = (head + 1) % capacity;
        count--;
        return item;
    }

    // ť�� ���� �� ��Ҹ� ��ȯ������ ���������� �ʽ��ϴ�. (Front/Peek)
    T front() const {
        if (isEmpty()) {
            throw std::underflow_error("Queue is empty. No front item.");
        }
        return arr[head];
    }

    // ť�� ����ִ��� Ȯ���մϴ�.
    bool isEmpty() const {
        return count == 0;
    }

    // ť�� ���� á���� Ȯ���մϴ�.
    bool isFull() const {
        return count == capacity - 1;
    }

    // ���� ť�� ����� ����� ������ ��ȯ�մϴ�.
    int size() const {
        return count;
    }

    // ť�� �ִ� �뷮�� ��ȯ�մϴ�.
    int getCapacity() const {
        return capacity - 1; // ����ڰ� ������ ���� �뷮 ��ȯ
    }

    void resize(int newSize) {
        if (newSize < 0) {
            throw std::invalid_argument("New queue size cannot be negative.");
        }

        if (newSize == getCapacity()) {
            return; // �뷮�� �����ϸ� �ƹ��͵� ���� ����
        }

        std::vector<T> newArr(newSize + 1); // ���ο� �뷮 + 1 ũ���� �迭
        int newCount = 0;

        // ���� ť�� ��ҵ��� ���ο� �迭�� ���� (���� ����)
        // head���� count��ŭ�� ��Ҹ� ������� ����
        for (int i = 0; i < count; ++i) {
            newArr[i] = std::move(arr[(head + i) % capacity]); // move semantic ����
            newCount++;
        }

        // �� �迭�� ��ü
        arr = std::move(newArr);
        capacity = newSize + 1;
        head = 0;
        tail = newCount; // �� �迭������ ��ҵ��� 0���� ������� �����Ƿ� tail�� newCount�� ��
        count = newCount;

        // ���� ���ο� �뷮�� ���� ��� �������� �۴ٸ�, �ʰ��� ��Ҹ� ������ ��
        if (count > newSize) {
            count = newSize; // ���ο� �뷮������ ��ȿ�ϰ� ����
            tail = newSize;
        }
    }

	std::vector<T>& getArray() {
		return arr; // ���� �迭�� ������ �� �ִ� �޼���
	}
};