#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <aeron/Aeron.h>
#include <mpsc/multiRingQueue.hpp>
#include "aeron_common.hpp"

constexpr auto mainLoopCoreID{9};
constexpr decltype(mainLoopCoreID) brokerCoreID[] = {4, 5, 11, 12, 13};
constexpr const char* targetChannel[] = {
        "aeron:udp?endpoint=127.0.0.1:40001",
        "aeron:udp?endpoint=127.0.0.1:40002",
        "aeron:udp?endpoint=127.0.0.1:40003",
        "aeron:udp?endpoint=127.0.0.1:40004",
        "aeron:ipc?endpoint=IPC"
    };
constexpr auto publisherPoolSize{sizeof(targetChannel) / sizeof(targetChannel[0])};
constexpr auto queueSize{1 << 16};
constexpr auto streamID{73};

using namespace aeron;

int main(int argc, char** argv)
{
    // Pin the main thread to mainLoopCoreID
    pinThreadToCore(pthread_self(), mainLoopCoreID);

    auto lockFreeQueue = std::make_unique<mpsc::multiRingQueue<std::string, queueSize, publisherPoolSize>>();
    Context context; 
    Aeron aeron(context);
    std::mutex outputMutex;
    std::vector<std::jthread> brokerThread(publisherPoolSize);
    
    // Define the thread function for each publisher channel
    const auto threadFunction = [&aeron, &lockFreeQueue, &outputMutex](const auto& channel, const auto streamID)
    {
        try
        {
            const auto subscriptionID = aeron.addSubscription(channel, streamID);
            auto subscription = aeron.findSubscription(subscriptionID);
            while (subscription == nullptr)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                subscription = aeron.findSubscription(subscriptionID);
            }
            
            {
                std::unique_lock lock(outputMutex);
                std::cout << "[Server] listen to: " << channel << ", added subscription ID: " << subscriptionID << std::endl;
            }

            auto producer = lockFreeQueue->acquire();
            if (producer.has_value())
            {
                auto queue = std::move(producer.value());
                const auto onMessageReceived = [&queue](auto& buffer, auto offset, auto length, auto& header)
                {
                    static thread_local uint64_t counter{0};
                    std::string message(reinterpret_cast<const char*>(buffer.buffer() + offset), length);
                    auto result = queue.push(message);
                };

                while (true)
                {
                    if (subscription->poll(onMessageReceived, 10) == 0)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }
            else
            {
                std::unique_lock lock(outputMutex);
                std::cout << "Queue pool is empty" << std::endl;
            }
        } 
        catch (const std::exception& e)
        {
            std::unique_lock lock(outputMutex);
            std::cerr << "Error: " << e.what() << std::endl;
        }
    };

    // Start the threads for each publisher channel
    for (auto i = 0; i < publisherPoolSize; ++i)
    {
        brokerThread[i] = std::jthread(threadFunction, targetChannel[i], streamID);
        // Pin each broker thread to brokerCoreID[i]
        pinThreadToCore(brokerThread[i].native_handle(), brokerCoreID[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Start the thread to consume messages from the lock-free queue
    while (true)
    {
        const auto result = lockFreeQueue->pop();
        if (result.has_value())
        {
            // here we do some magic with message received from the queue
            std::unique_lock lock(outputMutex);
            std::cout << result.value() << std::endl;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    return 0;
}
