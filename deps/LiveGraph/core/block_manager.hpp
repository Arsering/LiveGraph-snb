/* Copyright 2020 Guanyu Feng, Tsinghua University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <tbb/enumerable_thread_specific.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "blocks.hpp"
#include "types.hpp"
#include "utils.hpp"

#include "GoCache/include/buffer_pool_manager.h"

namespace livegraph
{
    class BlockManager
    {
    public:
        constexpr static uintptr_t NULLPOINTER = 0; // UINTPTR_MAX;

        BlockManager(std::string path, size_t _capacity = 1ul << 40)
            : capacity(_capacity),
              mutex(),
              free_blocks(std::vector<std::vector<uintptr_t>>(LARGE_BLOCK_THRESHOLD, std::vector<uintptr_t>())),
              large_free_blocks(MAX_ORDER, std::vector<uintptr_t>())
        {
            if (path.empty())
            {
                fd = EMPTY_FD;
                data =
                    mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                if (data == MAP_FAILED)
                    throw std::runtime_error("mmap block error.");

                file_size = FILE_TRUNC_SIZE;
                used_size = 0;
            }
            else
            {
                // auto chunk_num = chunk_file(path, path);
                // mmapped_files_.clear();
                // mmapped_files_.reserve(chunk_num);
                // while (chunk_num)
                // {
                //     chunk_num--;
                //     mmapped_files_[chunk_num] = open_new_file(path + "_" + std::to_string(chunk_num), chunk_num);
                // }

                fd = open(path.c_str(), O_RDWR | O_CREAT, 0640);
                GBPLOG << path;
                if (fd == EMPTY_FD)
                    throw std::runtime_error("open block file error.");

                data = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (data == MAP_FAILED)
                    throw std::runtime_error("mmap block error.");

                file_size = lseek(fd, 0, SEEK_END);

                if (file_size == 0)
                {
                    if (ftruncate(fd, FILE_TRUNC_SIZE) != 0)
                        throw std::runtime_error("ftruncate block file error.");
                    file_size = FILE_TRUNC_SIZE;
                    used_size = 0;
                }
                else
                {
                    used_size = ((size_t *)data)[0];
                    file_size = ((size_t *)data)[1];
                }
            }

            if (madvise(data, capacity, MADV_RANDOM) != 0)
                throw std::runtime_error("madvise block error.");

            null_holder = alloc(LARGE_BLOCK_THRESHOLD);
        }

        ~BlockManager()
        {
            free(null_holder, LARGE_BLOCK_THRESHOLD);
            ((size_t *)data)[0] = used_size;
            ((size_t *)data)[1] = file_size;
            msync(data, capacity, MS_SYNC);
            munmap(data, capacity);
            if (fd != EMPTY_FD)
                close(fd);
        }

        // uintptr_t alloc(order_t order)
        // {
        //     uintptr_t pointer = NULLPOINTER;
        //     if (order < LARGE_BLOCK_THRESHOLD)
        //     {
        //         pointer = pop(free_blocks.local(), order);
        //     }
        //     else
        //     {
        //         std::lock_guard<std::mutex> lock(mutex);
        //         pointer = pop(large_free_blocks, order);
        //     }

        //     if (pointer == NULLPOINTER)
        //     {
        //         size_t block_size = 1ul << order;
        //         do
        //         {
        //             pointer = used_size.fetch_add(block_size);
        //             if (pointer + block_size >= file_size)
        //             {
        //                 auto new_file_size = ((pointer + block_size) / FILE_TRUNC_SIZE + 1) * FILE_TRUNC_SIZE;
        //                 std::lock_guard<std::mutex> lock(mutex);
        //                 if (new_file_size >= file_size)
        //                 {
        //                     if (fd != EMPTY_FD)
        //                     {
        //                         if (ftruncate(fd, new_file_size) != 0)
        //                             throw std::runtime_error("ftruncate block file error.");
        //                     }
        //                     file_size = new_file_size;
        //                 }
        //             }
        //             if (pointer / SINGLE_FILE_SIZE != (pointer + block_size) / SINGLE_FILE_SIZE)
        //             {
        //                 GBPLOG << "cp";
        //             }
        //         } while (pointer / SINGLE_FILE_SIZE != (pointer + block_size) / SINGLE_FILE_SIZE);
        //     }

        //     return pointer;
        // }

        uintptr_t alloc(size_t block_size)
        {
            uintptr_t pointer = NULLPOINTER;
            size_t padding_size = 0;
            {
                if (used_size / gbp::PAGE_SIZE_MEMORY !=
                    (used_size + sizeof(VertexBlockHeader)) / gbp::PAGE_SIZE_MEMORY)
                { // 当整个VertexBlockHeader不会在同一个内存页上时
                    padding_size = gbp::PAGE_SIZE_MEMORY - used_size % gbp::PAGE_SIZE_MEMORY;
                }
            }
            auto order = size_to_order(block_size + padding_size);
            block_size = 1ul << order;

            pointer = used_size.fetch_add(block_size);

            if (pointer + block_size >= file_size)
            {
                auto new_file_size = ((pointer + block_size) / FILE_TRUNC_SIZE + 1) * FILE_TRUNC_SIZE;
                std::lock_guard<std::mutex> lock(mutex);
                if (new_file_size >= file_size)
                {
                    if (fd != EMPTY_FD)
                    {
                        if (ftruncate(fd, new_file_size) != 0)
                            throw std::runtime_error("ftruncate block file error.");
                    }
                    file_size = new_file_size;
                }
            }

            return pointer + padding_size;
        }

        void free(uintptr_t block, order_t order)
        {
            if (order < LARGE_BLOCK_THRESHOLD)
            {
                push(free_blocks.local(), order, block);
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                push(large_free_blocks, order, block);
            }
        }

        template <typename T> inline T *convert(uintptr_t block)
        {
            if (__builtin_expect((block == NULLPOINTER), 0))
                return nullptr;
            return reinterpret_cast<T *>(reinterpret_cast<char *>(data) + block);
        }

        // template <typename T> inline T *convert(uintptr_t block)
        // {
        //     if (__builtin_expect((block == NULLPOINTER), 0))
        //         return nullptr;

        //     // auto file_id = block / SINGLE_FILE_SIZE;
        //     auto file_id = block >> SINGLE_FILE_ORDER;
        //     // auto offset = block % SINGLE_FILE_SIZE;
        //     auto offset = block & (SINGLE_FILE_SIZE - 1);
        //     // assert(file_id == 0);
        //     // assert(mmapped_files_.size() >= file_id);

        //     return reinterpret_cast<T *>(reinterpret_cast<char *>(mmapped_files_[file_id].second) + offset);
        // }

    private:
        const size_t capacity;
        int fd;
        void *data;
        std::vector<std::pair<int, void *>> mmapped_files_;
        std::mutex mutex;
        tbb::enumerable_thread_specific<std::vector<std::vector<uintptr_t>>> free_blocks;
        std::vector<std::vector<uintptr_t>> large_free_blocks;
        std::atomic<size_t> used_size, file_size;
        uintptr_t null_holder;

        std::pair<int, void *> open_new_file(const std::string &file_path, size_t file_id)
        {
            auto fd = open(file_path.c_str(), O_RDWR | O_CREAT, 0640);
            GBPLOG << file_path;
            if (fd == EMPTY_FD)
                throw std::runtime_error("open block file error.");

            auto data = mmap(nullptr, SINGLE_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            GBPLOG << fd << " " << (uintptr_t)data;
            if (data == MAP_FAILED)
                throw std::runtime_error("mmap block error.");
            if (madvise(data, SINGLE_FILE_SIZE, MADV_RANDOM) != 0)
                throw std::runtime_error("madvise block error.");
            {
                getDefaultFD(file_id) =
                    gbp::BufferPoolManager::GetGlobalInstance().OpenFile(file_path, O_RDWR | O_CREAT | O_DIRECT);

                // assert(fd_gbp == 1);
                // GBPLOG << "The fd in GoCache of file " + graphPath + "/graph.mmap is " << fd_gbp;
            }
            return {fd, data};
        }

        uintptr_t pop(std::vector<std::vector<uintptr_t>> &free_block, order_t order)
        {
            uintptr_t pointer = NULLPOINTER;
            if (free_block[order].size())
            {
                pointer = free_block[order].back();
                free_block[order].pop_back();
            }
            return pointer;
        }

        void push(std::vector<std::vector<uintptr_t>> &free_block, order_t order, uintptr_t pointer)
        {
            free_block[order].push_back(pointer);
        }

        constexpr static int EMPTY_FD = -1;
        constexpr static order_t MAX_ORDER = 64;
        constexpr static order_t LARGE_BLOCK_THRESHOLD = 20;
        constexpr static size_t FILE_TRUNC_SIZE = 1ul << 30; // 1GB
    };

    class BlockManagerLibc
    {
    public:
        constexpr static uintptr_t NULLPOINTER = UINTPTR_MAX;

        uintptr_t alloc(order_t order)
        {
            auto p = aligned_alloc(1ul << order, 1ul << order);
            if (!p)
                throw std::runtime_error("Failed to alloc block");
            return reinterpret_cast<std::uintptr_t>(p);
        }

        void free(uintptr_t block, order_t order) { ::free(reinterpret_cast<void *>(block)); }

        template <typename T> T *convert(uintptr_t block)
        {
            if (block == NULLPOINTER)
                return nullptr;
            return reinterpret_cast<T *>(block);
        }
    };
} // namespace livegraph
