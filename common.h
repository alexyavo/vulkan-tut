#ifndef VULKAN_TUT_COMMON_H
#define VULKAN_TUT_COMMON_H

#include <memory>
#include <spdlog/spdlog.h>


template<typename T>
using sptr = std::shared_ptr<T>;

template<typename T>
using vec = std::vector<T>;

using u32 = uint32_t;

using str = std::string;

u32 u32_max = std::numeric_limits<u32>::max();

#endif //VULKAN_TUT_COMMON_H
