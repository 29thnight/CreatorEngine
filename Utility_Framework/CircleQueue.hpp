#include <iostream>
#include <vector>
template <typename T>
class CircularQueue {
private:
    std::vector<T> arr; // 큐 데이터를 저장할 배열
    int capacity;       // 큐의 최대 용량 (실제 저장 가능한 요소는 capacity - 1)
    int head;           // 큐의 첫 번째 요소를 가리키는 인덱스
    int tail;           // 큐에 다음 요소가 추가될 위치를 가리키는 인덱스
    int count;          // 현재 큐에 저장된 요소의 개수

public:
    CircularQueue() :
        capacity(1), // 최소 용량은 1 (실제 저장 가능 요소 0)
        arr(1),
        head(0),
        tail(0),
        count(0)
    {
    }
    // 생성자: 큐의 용량을 초기화합니다.
    // 실제 저장 가능한 요소의 개수는 capacity - 1 입니다.
    // 이는 큐가 가득 찼는지 (head == tail && count == capacity - 1)와
    // 큐가 비었는지 (head == tail && count == 0)를 구분하기 위함입니다.
    explicit CircularQueue(int size) :
        capacity(size + 1), // 큐가 가득 찼을 때 한 칸을 비워두기 위해 +1
        arr(size + 1),
        head(0),
        tail(0),
        count(0)
    {
        if (size <= 0) {
            throw std::invalid_argument("Queue size must be greater than 0.");
        }
    }

    // 큐에 요소를 추가합니다. (Enqueue)
    void enqueue(const T& item) {
        if (isFull()) {
            throw std::overflow_error("Queue is full. Cannot enqueue item.");
        }
        arr[tail] = item;
        tail = (tail + 1) % capacity;
        count++;
    }

    // 큐에서 요소를 제거하고 반환합니다. (Dequeue)
    T dequeue() {
        if (isEmpty()) {
            throw std::underflow_error("Queue is empty. Cannot dequeue item.");
        }
        T item = arr[head];
        head = (head + 1) % capacity;
        count--;
        return item;
    }

    // 큐의 가장 앞 요소를 반환하지만 제거하지는 않습니다. (Front/Peek)
    T front() const {
        if (isEmpty()) {
            throw std::underflow_error("Queue is empty. No front item.");
        }
        return arr[head];
    }

    // 큐가 비어있는지 확인합니다.
    bool isEmpty() const {
        return count == 0;
    }

    // 큐가 가득 찼는지 확인합니다.
    bool isFull() const {
        return count == capacity - 1;
    }

    // 현재 큐에 저장된 요소의 개수를 반환합니다.
    int size() const {
        return count;
    }

    // 큐의 최대 용량을 반환합니다.
    int getCapacity() const {
        return capacity - 1; // 사용자가 지정한 실제 용량 반환
    }

    void resize(int newSize) {
        if (newSize < 0) {
            throw std::invalid_argument("New queue size cannot be negative.");
        }

        if (newSize == getCapacity()) {
            return; // 용량이 동일하면 아무것도 하지 않음
        }

        std::vector<T> newArr(newSize + 1); // 새로운 용량 + 1 크기의 배열
        int newCount = 0;

        // 기존 큐의 요소들을 새로운 배열로 복사 (순서 유지)
        // head부터 count만큼의 요소를 순서대로 복사
        for (int i = 0; i < count; ++i) {
            newArr[i] = std::move(arr[(head + i) % capacity]); // move semantic 적용
            newCount++;
        }

        // 새 배열로 교체
        arr = std::move(newArr);
        capacity = newSize + 1;
        head = 0;
        tail = newCount; // 새 배열에서는 요소들이 0부터 순서대로 있으므로 tail은 newCount가 됨
        count = newCount;

        // 만약 새로운 용량이 기존 요소 개수보다 작다면, 초과된 요소를 버려야 함
        if (count > newSize) {
            count = newSize; // 새로운 용량까지만 유효하게 남김
            tail = newSize;
        }
    }

	std::vector<T>& getArray() {
		return arr; // 내부 배열에 접근할 수 있는 메서드
	}
};