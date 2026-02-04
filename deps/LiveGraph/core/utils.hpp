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

#include <string>

#include "../../GoCache/include/buffer_pool_manager.h"
#include "types.hpp"

namespace livegraph
{
    inline gbp::GBPfile_handle_type &getDefaultFD(size_t file_id)
    {
        static std::vector<gbp::GBPfile_handle_type> fd_gbps;
        while (fd_gbps.size() <= file_id)
        {
            fd_gbps.emplace_back(0);
        }
        return fd_gbps[file_id];
    }
    inline gbp::BufferBlock getBufferBlock(size_t file_offset, size_t block_size)
    {
        return gbp::BufferPoolManager::GetGlobalInstance().GetBlock(file_offset & (SINGLE_FILE_SIZE - 1), block_size,
                                                                    getDefaultFD(file_offset >> SINGLE_FILE_ORDER));
        // return gbp::BufferBlock();
    }
    inline void getBufferBlockBatch(std::vector<gbp::batch_request_type> &blk_info,
                                    std::vector<gbp::BufferBlock> &results)
    {
        for (auto &item : blk_info)
        {
            auto file_offset = item.file_offset_;
            auto file_id = file_offset / SINGLE_FILE_SIZE;
            item.fd_ = getDefaultFD(file_offset >> SINGLE_FILE_ORDER);
            item.file_offset_ = file_offset & (SINGLE_FILE_SIZE - 1);
        }
        gbp::BufferPoolManager::GetGlobalInstance().GetBlockBatch(blk_info, results);
    }
    inline size_t chunk_file(const std::string &src, const std::string &dst)
    {
        if (!std::filesystem::exists(src))
        {
            GBPLOG << "file not exists: " << src;
            return 0;
        }

        size_t len = std::filesystem::file_size(src);
        size_t file_id = 0;
        int src_fd = ::open(src.c_str(), O_RDONLY, 0777);
        assert(src_fd != -1);
        loff_t src_cursor = 0;
        while (src_cursor < len)
        {
            int dst_fd = ::open((dst + "_" + std::to_string(file_id++)).c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0777);
            assert(dst_fd != -1);
            loff_t dst_cursor = 0;
            size_t request_len = std::min(SINGLE_FILE_SIZE, static_cast<size_t>(len - src_cursor));
            do
            {
                ssize_t ret = ::copy_file_range(src_fd, &src_cursor, dst_fd, &dst_cursor, request_len, 0);
                assert(ret != -1);
                request_len -= ret;
                // GBPLOG << "cp";
                // if (static_cast<size_t>(ret) != request_len)
                // {
                //     GBPLOG << ret << " " << request_len << " " << src_cursor;
                //     assert(false);
                // }
            } while (request_len);
            ::close(dst_fd);
        }
        ::close(src_fd);
        return file_id;
    }

    inline void compiler_fence() { asm volatile("" ::: "memory"); }

    inline order_t size_to_order(size_t size)
    {
        order_t order = (order_t)((size & (size - 1)) != 0);
        while (size > 1)
        {
            order += 1;
            size >>= 1;
        }
        return order;
    }

    inline int cmp_timestamp(const timestamp_t *xp, timestamp_t y) // y > 0
    {
        timestamp_t x = *xp;
        if (x < 0)
            return 1;
        if (x < y)
            return -1;
        if (x == y)
            return 0;
        return 1;
    }

    inline int cmp_timestamp(const timestamp_t *xp, timestamp_t y,
                             timestamp_t local_txn_id) // y > 0
    {
        timestamp_t x = *xp;
        if (-x == local_txn_id)
            return 0;
        if (x < 0)
            return 1;
        if (x < y)
            return -1;
        if (x == y)
            return 0;
        return 1;
    }

    inline int cmp_timestamp(const timestamp_t xp, timestamp_t y,
                             timestamp_t local_txn_id) // y > 0
    {
        timestamp_t x = xp;
        if (-x == local_txn_id)
            return 0;
        if (x < 0)
            return 1;
        if (x < y)
            return -1;
        if (x == y)
            return 0;
        return 1;
    }

} // namespace livegraph
