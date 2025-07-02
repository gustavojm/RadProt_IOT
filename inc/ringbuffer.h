template <typename T>
class RingBuffer {
private:
    char *buffer;
    size_t capacity;
    size_t read_ptr;
    size_t write_ptr;
    size_t current_size; // To track if full/empty

public:
    explicit RingBuffer(size_t capacity) :
        capacity(capacity),
        buffer(new char[capacity]),
        read_ptr(0),
        write_ptr(0),
        current_size(0) {}

    bool push(const T& item) {
        if (is_full()) {
            return false; // Buffer is full
        }
        buffer[write_ptr] = item;
        write_ptr = (write_ptr + 1) % capacity;
        current_size++;
        return true;
    }

    bool pop(T& item) {
        if (is_empty()) {
            return false; // Buffer is empty
        }
        item = buffer[read_ptr];
        read_ptr = (read_ptr + 1) % capacity;
        current_size--;
        return true;
    }

    bool is_empty() const {
        return current_size == 0;
    }

    bool is_full() const {
        return current_size == capacity;
    }

    int space_left() const {
        return capacity - current_size;
    }


    size_t size() const {
        return current_size;
    }
   
};