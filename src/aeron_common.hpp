#include <pthread.h>
#include <string>
#define THREAD_PINNING_ENABLED

void pinThreadToCore(pthread_t thread, int coreID)
{
#ifdef THREAD_PINNING_ENABLED   
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreID, &cpuset);
    const auto rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
    {
        throw std::runtime_error(std::string("pthread_setaffinity_np: ") + std::strerror(rc));
    }
#endif
}