#pragma once

#include "FreeRTOS.h"
#include "stdio.h"

#include "ArduinoJson.hpp"

struct FreeRTOSAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        return pvPortMalloc(size);
    }

    void deallocate(void *pointer) override {
        vPortFree(pointer);
    }

    void *reallocate(void *ptr, size_t new_size) override {
        if (new_size == 0) {
            vPortFree(ptr);
            return NULL;
        }

        void *new_ptr;
        new_ptr = pvPortMalloc(new_size);
        if (new_ptr) {
            if (ptr != NULL) {
                memcpy(new_ptr, ptr, new_size);
                vPortFree(ptr);
            }
        }
        return new_ptr;
    }

  public:
    virtual ~FreeRTOSAllocator() = default;
};

namespace ArduinoJson {
    class MyJsonDocument : public ArduinoJson::JsonDocument {
      public:
        MyJsonDocument() : ArduinoJson::JsonDocument(&allocator){};

      private:
        static FreeRTOSAllocator allocator;
    };

} // namespace ArduinoJson

// Define the static allocator outside the class
inline FreeRTOSAllocator ArduinoJson::MyJsonDocument::allocator;
