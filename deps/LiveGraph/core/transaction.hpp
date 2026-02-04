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

#include <deque>
#include <map>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "blocks.hpp"
#include "graph.hpp"
#include "utils.hpp"

namespace livegraph
{
    class Transaction
    {
        enum class OPType
        {
            NewVertex,
            PutVertex,
            DelVertex,
            PutEdge,
            DelEdge,
        };

    public:
        class RollbackExcept : public std::runtime_error
        {
        public:
            RollbackExcept(const std::string &what_arg) : std::runtime_error(what_arg) {}
            RollbackExcept(const char *what_arg) : std::runtime_error(what_arg) {}
        };

        Transaction(
            Graph &_graph, timestamp_t _local_txn_id, timestamp_t _read_epoch_id, bool _batch_update, bool _trace_cache)
            : graph(_graph),
              local_txn_id(_local_txn_id),
              read_epoch_id(_read_epoch_id),
              batch_update(_batch_update),
              trace_cache(_trace_cache),
              write_epoch_id(batch_update ? read_epoch_id : -local_txn_id),
              valid(true),
              wal(),
              vertex_ptr_cache(),
              edge_ptr_cache(),
              block_cache(),
              edge_block_num_entries_data_length_cache(),
              new_vertex_cache(),
              recycled_vertex_cache(),
              acquired_locks(),
              timestamps_to_update()
        {
            wal_append((uint64_t)0); // number of operations
            wal_append(read_epoch_id);
            wal_append(local_txn_id);
        }

        Transaction(const Transaction &) = delete;

        Transaction(Transaction &&txn)
            : graph(txn.graph),
              local_txn_id(std::move(txn.local_txn_id)),
              read_epoch_id(std::move(txn.read_epoch_id)),
              batch_update(std::move(txn.batch_update)),
              trace_cache(std::move(txn.trace_cache)),
              write_epoch_id(std::move(txn.write_epoch_id)),
              valid(std::move(txn.valid)),
              wal(std::move(txn.wal)),
              vertex_ptr_cache(std::move(txn.vertex_ptr_cache)),
              edge_ptr_cache(std::move(txn.edge_ptr_cache)),
              block_cache(std::move(txn.block_cache)),
              edge_block_num_entries_data_length_cache(std::move(txn.edge_block_num_entries_data_length_cache)),
              new_vertex_cache(std::move(txn.new_vertex_cache)),
              recycled_vertex_cache(std::move(txn.recycled_vertex_cache)),
              acquired_locks(std::move(txn.acquired_locks)),
              timestamps_to_update(std::move(txn.timestamps_to_update))
        {
            txn.valid = false;
        }

        timestamp_t get_read_epoch_id() const { return read_epoch_id; }

        vertex_t new_vertex(bool use_recycled_vertex = false);
        void put_vertex(vertex_t vertex_id, std::string_view data);
        bool del_vertex(vertex_t vertex_id, bool recycle = false);

        void put_edge(vertex_t src, label_t label, vertex_t dst, std::string_view edge_data, bool force_insert = false);
        bool del_edge(vertex_t src, label_t label, vertex_t dst);

        std::string_view get_vertex(vertex_t vertex_id);

        size_t get_vertex_gbp(vertex_t vertex_id);
        template <typename T> std::pair<T, gbp::BufferBlock> get_vertex_with_data_gbp(vertex_t vertex_id)
        {
            auto addr = get_vertex_gbp(vertex_id);
            auto schema_buf = getBufferBlock(addr, sizeof(T));
            auto schema = schema_buf.GetInnerObj<T>();

            return {schema, getBufferBlock(addr, schema.length + offsetof(T, data))};
        }
        template <typename T> std::pair<T, size_t> get_vertex_without_data_gbp(vertex_t vertex_id)
        {
            auto addr = get_vertex_gbp(vertex_id);
            auto schema_buf = getBufferBlock(addr, sizeof(T));
            return {schema_buf.GetInnerObj<T>(), addr};
        }

        template <typename T>
        std::pair<std::vector<T>, std::vector<gbp::BufferBlock>>
        get_vertex_with_data_gbp(std::vector<vertex_t> &vertex_ids)
        {
            check_valid();
            std::vector<gbp::BufferBlock> ret;

            std::vector<std::pair<size_t, bool>> pointer_infos;
            pointer_infos.resize(vertex_ids.size(), {graph.block_manager.NULLPOINTER, false});

            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                auto vertex_id = vertex_ids[idx];

                if (vertex_id >= graph.vertex_id.load(std::memory_order_relaxed))
                    assert(false);

                uintptr_t pointer;
                if (batch_update || !trace_cache)
                {
                    pointer = graph.vertex_ptrs[vertex_id];
                }
                else
                {
                    auto cache_iter = vertex_ptr_cache.find(vertex_id);
                    if (cache_iter != vertex_ptr_cache.end())
                        pointer = cache_iter->second;
                    else
                        pointer = graph.vertex_ptrs[vertex_id];
                }
                pointer_infos[idx].first = pointer;
            }

            bool success_mark;
            do
            {
                success_mark = true;
                std::vector<gbp::batch_request_type> blk_infos;
                for (size_t idx = 0; idx < vertex_ids.size(); idx++)
                {
                    if (pointer_infos[idx].second || pointer_infos[idx].first == graph.block_manager.NULLPOINTER)
                    {
                        pointer_infos[idx].second = true;
                        continue;
                    }
                    blk_infos.emplace_back(
                        pointer_infos[idx].first,
                        std::max(gbp::PAGE_SIZE_MEMORY - pointer_infos[idx].first % gbp::PAGE_SIZE_MEMORY,
                                 sizeof(VertexBlockHeader)),
                        0);
                }
                ret.clear();
                getBufferBlockBatch(blk_infos, ret);

                size_t ret_cursor = 0;
                for (size_t idx = 0; idx < vertex_ids.size(); idx++)
                {
                    if (pointer_infos[idx].second)
                    {
                        continue;
                    }

                    auto vertex_block = ret[ret_cursor++].GetInnerObj<VertexBlockHeader>();
                    if (cmp_timestamp(vertex_block.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
                    {
                        pointer_infos[idx].second = true;
                        continue;
                    }
                    pointer_infos[idx].first = vertex_block.get_prev_pointer();
                    success_mark = false;
                }
            } while (!success_mark);

            std::vector<gbp::batch_request_type> blk_infos;
            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                blk_infos.emplace_back(pointer_infos[idx].first + sizeof(VertexBlockHeader), sizeof(T), 0);
            }
            ret.clear();
            getBufferBlockBatch(blk_infos, ret);

            blk_infos.clear();
            std::vector<T> schemas;
            schemas.resize(vertex_ids.size());
            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                schemas[idx] = ret[idx].GetInnerObj<T>();
                blk_infos.emplace_back(pointer_infos[idx].first + sizeof(VertexBlockHeader),
                                       schemas[idx].length + offsetof(T, data), 0);
            }
            ret.clear();
            getBufferBlockBatch(blk_infos, ret);

            return {schemas, ret};
        }

        template <typename T>
        std::pair<std::vector<T>, std::vector<uintptr_t>> get_vertex_without_data_gbp(std::vector<vertex_t> &vertex_ids)
        {
            check_valid();
            std::vector<gbp::BufferBlock> ret;

            std::vector<size_t> pointers;
            std::vector<bool> marks;
            pointers.resize(vertex_ids.size(), graph.block_manager.NULLPOINTER);
            marks.resize(vertex_ids.size(), false);

            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                auto vertex_id = vertex_ids[idx];

                if (vertex_id >= graph.vertex_id.load(std::memory_order_relaxed))
                    assert(false);

                uintptr_t pointer;
                if (batch_update || !trace_cache)
                {
                    pointer = graph.vertex_ptrs[vertex_id];
                }
                else
                {
                    auto cache_iter = vertex_ptr_cache.find(vertex_id);
                    if (cache_iter != vertex_ptr_cache.end())
                        pointer = cache_iter->second;
                    else
                        pointer = graph.vertex_ptrs[vertex_id];
                }
                pointers[idx] = pointer;
            }

            bool success_mark;
            do
            {
                success_mark = true;
                std::vector<gbp::batch_request_type> blk_infos;
                for (size_t idx = 0; idx < vertex_ids.size(); idx++)
                {
                    if (marks[idx] || pointers[idx] == graph.block_manager.NULLPOINTER)
                    {
                        marks[idx] = true;
                        continue;
                    }
                    blk_infos.emplace_back(pointers[idx],
                                           std::max(gbp::PAGE_SIZE_MEMORY - pointers[idx] % gbp::PAGE_SIZE_MEMORY,
                                                    sizeof(VertexBlockHeader)),
                                           0);
                }
                ret.clear();
                getBufferBlockBatch(blk_infos, ret);

                size_t ret_cursor = 0;
                for (size_t idx = 0; idx < vertex_ids.size(); idx++)
                {
                    if (marks[idx])
                    {
                        continue;
                    }

                    auto vertex_block = ret[ret_cursor++].GetInnerObj<VertexBlockHeader>();
                    if (cmp_timestamp(vertex_block.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
                    {
                        marks[idx] = true;
                        continue;
                    }
                    pointers[idx] = vertex_block.get_prev_pointer();
                    success_mark = false;
                }
            } while (!success_mark);

            std::vector<gbp::batch_request_type> blk_infos;
            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                pointers[idx] += sizeof(VertexBlockHeader);
                blk_infos.emplace_back(pointers[idx], sizeof(T), 0);
            }
            ret.clear();
            getBufferBlockBatch(blk_infos, ret);

            blk_infos.clear();
            std::vector<T> schemas;
            schemas.resize(vertex_ids.size());
            for (size_t idx = 0; idx < vertex_ids.size(); idx++)
            {
                schemas[idx] = ret[idx].GetInnerObj<T>();
            }

            return {schemas, pointers};
        }

        std::string_view get_edge(vertex_t src, label_t label, vertex_t dst);
        std::string get_edge_gbp(vertex_t src, label_t label, vertex_t dst);
        std::vector<std::string> get_edge_gbp(std::vector<std::tuple<vertex_t, label_t, bool>> &edge_infos);

        EdgeIterator get_edges(vertex_t src, label_t label, bool reverse = false);
        EdgeIterator_gbp get_edges_gbp(vertex_t src, label_t label, bool reverse = false);
        std::vector<EdgeIterator_gbp> get_edges_gbp(std::vector<std::tuple<vertex_t, label_t, bool>> &edgelist_infos);

        timestamp_t commit(bool wait_visable = true);
        void abort();

        ~Transaction()
        {
            if (valid)
                abort();
        }

    private:
        Graph &graph;
        const timestamp_t local_txn_id;
        const timestamp_t read_epoch_id;
        const bool batch_update;
        const bool trace_cache;
        const timestamp_t write_epoch_id;
        bool valid;
        std::string wal;

        std::unordered_map<vertex_t, uintptr_t> vertex_ptr_cache;
        std::map<std::pair<vertex_t, label_t>, uintptr_t> edge_ptr_cache;
        std::vector<std::pair<uintptr_t, order_t>> block_cache;
        std::unordered_map<EdgeBlockHeader *, std::pair<size_t, size_t>> edge_block_num_entries_data_length_cache;
        std::vector<vertex_t> new_vertex_cache;
        std::deque<vertex_t> recycled_vertex_cache;

        std::unordered_set<vertex_t> acquired_locks;
        std::vector<std::pair<timestamp_t *, timestamp_t>> timestamps_to_update;

        template <typename T, typename = std::enable_if_t<std::is_trivial_v<T>>> inline void wal_append(T data)
        {
            wal.append(reinterpret_cast<char *>(&data), sizeof(T));
        }

        inline void wal_append(std::string_view data)
        {
            wal_append(data.size());
            wal.append(data);
        }

        inline uint64_t &wal_num_ops() { return *reinterpret_cast<uint64_t *>(wal.data()); }

        void check_writable()
        {
            if (!batch_update && !trace_cache)
                throw std::invalid_argument("The transaction is read-only without cache.");
        }

        void check_valid()
        {
            if (!valid)
                throw std::invalid_argument("The transaction is committed or aborted.");
        }

        void check_vertex_id(vertex_t vertex_id)
        {
            if (vertex_id >= graph.vertex_id.load(std::memory_order_relaxed))
                throw std::invalid_argument("The vertex id is invalid.");
        }

        void ensure_vertex_lock(vertex_t vertex_id)
        {
            auto iter = acquired_locks.find(vertex_id);
            if (iter != acquired_locks.end())
                return;
            if (!graph.vertex_futexes[vertex_id].try_lock_for(Graph::TIMEOUT))
                throw RollbackExcept("Deadlock on Vertex: " + std::to_string(vertex_id) + ".");
            acquired_locks.emplace_hint(iter, vertex_id);
        }

        void ensure_no_confict(vertex_t vertex_id)
        {
            auto header = graph.block_manager.convert<VertexBlockHeader>(graph.vertex_ptrs[vertex_id]);
            if (header && cmp_timestamp(header->get_creation_time_pointer(), read_epoch_id, local_txn_id) > 0)
                throw RollbackExcept("Write-write confict on: " + std::to_string(vertex_id) + ".");
        }

        void clean()
        {
            for (const auto &vertex_id : acquired_locks)
            {
                graph.vertex_futexes[vertex_id].unlock();
            }
            valid = false;
            graph.read_epoch_table.local() = Graph::NO_TRANSACTION;
        }

        std::pair<size_t, size_t> get_num_entries_data_length_cache(EdgeBlockHeader *edge_block) const
        {
            if (batch_update || !trace_cache)
                return edge_block->get_num_entries_data_length_atomic();
            assert(false);
            auto iter = edge_block_num_entries_data_length_cache.find(edge_block);
            if (iter == edge_block_num_entries_data_length_cache.end())
                return edge_block->get_num_entries_data_length_atomic();
            else
                return iter->second;
        }

        void set_num_entries_data_length_cache(EdgeBlockHeader *edge_block, size_t num_entries, size_t data_length)
        {
            if (batch_update)
                edge_block->set_num_entries_data_length_atomic(num_entries, data_length);
            else
                edge_block_num_entries_data_length_cache[edge_block] = {num_entries, data_length};
        }

        std::pair<EdgeEntry *, char *>
        find_edge(vertex_t dst, EdgeBlockHeader *edge_block, size_t num_entries, size_t data_length);

        uintptr_t locate_edge_block(vertex_t src, label_t label);
        uintptr_t locate_edge_block_gbp(vertex_t src, label_t label);
        std::vector<uintptr_t> locate_edge_block_gbp(std::vector<std::pair<vertex_t, label_t>> &edgelist_infos);

        void update_edge_label_block(vertex_t src, label_t label, uintptr_t edge_block_pointer);

        void ensure_no_confict(vertex_t src, label_t label);
    };
} // namespace livegraph
