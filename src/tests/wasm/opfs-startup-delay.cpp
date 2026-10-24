#include <emscripten/threading.h>
#include <pthread.h>

extern "C" int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);

// Give the OPFS worker time to run before its creator finishes construction.
// This makes the SDK's uninitialized startup flag race reproducible in the
// filesystem fixture; the emulator does not link this scheduling perturbation.
extern "C" int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*entry)(void *), void *arg) {
    const int result = __real_pthread_create(thread, attr, entry, arg);
    if (!result && !emscripten_is_main_browser_thread()) emscripten_thread_sleep(50);
    return result;
}
