
//#include <stdlib.h>
#include "FreeRTOS.h"

// Override malloc and free
void *malloc(size_t size) {
    return pvPortMalloc(size);
}

void free(void *ptr) {
    vPortFree(ptr);
}

void *operator new(size_t size) {
    return pvPortMalloc(size);
}

void *operator new[](size_t size) {
    return pvPortMalloc(size);
}

void operator delete(void *p) {
    vPortFree(p);   
}

void operator delete[](void *p) {
    vPortFree(p);
}

void operator delete(void *p, unsigned int) {
    vPortFree(p);
}

void operator delete[](void *p, unsigned int) {
    vPortFree(p);
}

extern "C" int __aeabi_atexit(void *object, void (*destructor)(void *), void *dso_handle) {
    return 0;
}

#ifdef CPP_NO_HEAP
extern "C" void *malloc(size_t) {
    return reinterpret_cast<void *>(0);
}

extern "C" void free(void *) {
}
#endif

#ifndef CPP_USE_CPPLIBRARY_TERMINATE_HANDLER
/******************************************************************
 * __verbose_terminate_handler()
 *
 * This is the function that is called when an uncaught C++
 * exception is encountered. The default version within the C++
 * library prints the name of the uncaught exception, but to do so
 * it must demangle its name - which causes a large amount of code
 * to be pulled in. The below minimal implementation can reduce
 * code size noticeably. Note that this function should not return.
 ******************************************************************/
namespace __gnu_cxx {
    void __verbose_terminate_handler() {
        while (1) {
        }
    }
} // namespace __gnu_cxx
#endif
