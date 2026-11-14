#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <aeron/Aeron.h>
#include <random>
#include <algorithm>
#include "aeron_common.hpp"

constexpr auto streamID{73};
    
using namespace aeron;

auto generateRandomString(const size_t size)
{
    static const std::string characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::mt19937 generator(std::random_device{}());
    static std::uniform_int_distribution<size_t> distribution(0, characters.size() - 1);
    std::string random_string(size, '\0');
    std::generate_n(random_string.begin(), size, [&]() {
        return characters[distribution(generator)];
    });    
    return random_string;
}

int main(int argc, char** argv)
{
    // ./aeron_publisher <mainLoopCoreID> <port>
    if (1 < argc)
    {
        const auto mainLoopCoreID = std::stoi(argv[1]);
        // Pin the main thread to mainLoopCoreID
        pinThreadToCore(pthread_self(), mainLoopCoreID);
        std::cout << "[Client] pinned to core " << mainLoopCoreID << std::endl;
    }
    
    const std::string port{(2 < argc) ? argv[2] : "IPC"};
    const auto targetChannel{(2 < argc) ? ("aeron:udp?endpoint=127.0.0.1:" + port) : ("aeron:ipc?endpoint=" + port)};
    const auto ipPort{targetChannel.substr(19)};
    std::cout << "[Client] stream ID: " << streamID << std::endl;

    try
    {
        Context context;
        Aeron aeron(context);

        const auto publicationID = aeron.addPublication(targetChannel, streamID);
        auto publication = aeron.findPublication(publicationID);
        while (publication == nullptr)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            publication = aeron.findPublication(publicationID);
        }
        std::cout << "[Client] connecting to server..." << std::endl;
        
        while (!publication->isConnected())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "[Client] connected to: " << ipPort << ", added publication ID: " << publicationID << std::endl;
        std::cout << "[Client] sending message..." << std::endl;
        
        for (auto i = 0; i < 100'000'000; ++i)
        {
            const auto message{port + ":" + generateRandomString(32)};
            std::vector<uint8_t> buffer(message.begin(), message.end());
            AtomicBuffer srcBuffer(buffer.data(), buffer.size());

            while (publication->offer(srcBuffer, 0, srcBuffer.capacity()) < 0)
            {
                std::this_thread::yield();
            }
        }
        
        std::cout << "[Client] sent successfully!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}
