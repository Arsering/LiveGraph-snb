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

#include <cstddef>
#include <cstdint>

#define GOCACHE_ENABLED true
#define GOCACHE_BATCH_IO_ENABLED false

namespace livegraph
{
    using label_t = uint16_t;
    using vertex_t = uint64_t;
    using order_t = uint8_t;
    using timestamp_t = int64_t;
    constexpr static size_t SINGLE_FILE_ORDER = 31;
    constexpr static size_t SINGLE_FILE_SIZE = 1ul << SINGLE_FILE_ORDER; // 32GB

} // namespace livegraph
