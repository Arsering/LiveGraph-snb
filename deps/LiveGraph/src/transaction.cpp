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

#include "core/transaction.hpp"
#include "core/edge_iterator.hpp"
#include "core/graph.hpp"

using namespace livegraph;

vertex_t Transaction::new_vertex(bool use_recycled_vertex)
{
    check_valid();
    check_writable();

    vertex_t vertex_id;
    if (!batch_update && recycled_vertex_cache.size())
    {
        vertex_id = recycled_vertex_cache.front();
        recycled_vertex_cache.pop_front();
    }
    else if (!use_recycled_vertex || (!graph.recycled_vertex_ids.try_pop(vertex_id)))
    {
        vertex_id = graph.vertex_id.fetch_add(1, std::memory_order_relaxed);
    }
    graph.vertex_futexes[vertex_id].clear();
    graph.vertex_ptrs[vertex_id] = graph.block_manager.NULLPOINTER;
    graph.edge_label_ptrs[vertex_id] = graph.block_manager.NULLPOINTER;

    if (!batch_update)
    {
        new_vertex_cache.emplace_back(vertex_id);
        ++wal_num_ops();
        wal_append(OPType::NewVertex);
        wal_append(vertex_id);
    }

    return vertex_id;
}

void Transaction::put_vertex(vertex_t vertex_id, std::string_view data)
{
    check_valid();
    check_writable();
    check_vertex_id(vertex_id);

    uintptr_t prev_pointer;
    if (batch_update)
    {
        graph.vertex_futexes[vertex_id].lock();
        prev_pointer = graph.vertex_ptrs[vertex_id];
    }
    else
    {
        ensure_vertex_lock(vertex_id);
        auto cache_iter = vertex_ptr_cache.find(vertex_id);
        if (cache_iter != vertex_ptr_cache.end())
            prev_pointer = cache_iter->second;
        else
        {
            ensure_no_confict(vertex_id);
            prev_pointer = graph.vertex_ptrs[vertex_id];
        }
    }

    auto size = sizeof(VertexBlockHeader) + data.size();
    auto order = size_to_order(size);
    // GBPLOG << "order and size " << (size_t)order << " " << size;
    auto pointer = graph.block_manager.alloc(order);

    auto vertex_block = graph.block_manager.convert<VertexBlockHeader>(pointer);

    vertex_block->fill(order, vertex_id, write_epoch_id, prev_pointer, data.data(), data.size());

    graph.compact_table.local().emplace(vertex_id);

    if (batch_update)
    {
        graph.vertex_ptrs[vertex_id] = pointer;
        graph.vertex_futexes[vertex_id].unlock();
    }
    else
    {
        block_cache.emplace_back(pointer, order);
        timestamps_to_update.emplace_back(vertex_block->get_creation_time_pointer(), Graph::ROLLBACK_TOMBSTONE);
        vertex_ptr_cache[vertex_id] = pointer;

        ++wal_num_ops();
        wal_append(OPType::PutVertex);
        wal_append(vertex_id);
        wal_append(data);
    }
}

bool Transaction::del_vertex(vertex_t vertex_id, bool recycle)
{
    check_valid();
    check_writable();
    check_vertex_id(vertex_id);

    uintptr_t prev_pointer;
    if (batch_update)
    {
        graph.vertex_futexes[vertex_id].lock();
        prev_pointer = graph.vertex_ptrs[vertex_id];
    }
    else
    {
        ensure_vertex_lock(vertex_id);
        auto cache_iter = vertex_ptr_cache.find(vertex_id);
        if (cache_iter != vertex_ptr_cache.end())
            prev_pointer = cache_iter->second;
        else
        {
            ensure_no_confict(vertex_id);
            prev_pointer = graph.vertex_ptrs[vertex_id];
        }
    }

    bool ret = false;
    auto prev_vertex_block = graph.block_manager.convert<VertexBlockHeader>(prev_pointer);
    if (prev_vertex_block && prev_vertex_block->get_length() != prev_vertex_block->TOMBSTONE)
    {
        ret = true;
        auto size = sizeof(VertexBlockHeader);
        auto order = size_to_order(size);
        // GBPLOG << "order and size " << (size_t)order << " " << size;
        auto pointer = graph.block_manager.alloc(order);

        auto vertex_block = graph.block_manager.convert<VertexBlockHeader>(pointer);
        vertex_block->fill(order, vertex_id, write_epoch_id, prev_pointer, nullptr, vertex_block->TOMBSTONE);

        graph.compact_table.local().emplace(vertex_id);

        if (!batch_update)
        {
            block_cache.emplace_back(pointer, order);
            timestamps_to_update.emplace_back(vertex_block->get_creation_time_pointer(), Graph::ROLLBACK_TOMBSTONE);
            vertex_ptr_cache[vertex_id] = pointer;
        }
    }

    if (batch_update)
    {
        if (recycle)
            graph.recycled_vertex_ids.push(vertex_id);
        graph.vertex_futexes[vertex_id].unlock();
    }
    else
    {
        ++wal_num_ops();
        wal_append(OPType::DelVertex);
        wal_append(vertex_id);
        wal_append(recycle);

        if (recycle)
            recycled_vertex_cache.emplace_back(vertex_id);
    }

    return ret;
}

std::string_view Transaction::get_vertex(vertex_t vertex_id)
{
    // assert(false);
    check_valid();

    if (vertex_id >= graph.vertex_id.load(std::memory_order_relaxed))
        return std::string_view();

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

    auto vertex_block = graph.block_manager.convert<VertexBlockHeader>(pointer);

    while (vertex_block)
    {
        if (cmp_timestamp(vertex_block->get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
            break;
        pointer = vertex_block->get_prev_pointer();
        vertex_block = graph.block_manager.convert<VertexBlockHeader>(pointer);
    }

    if (!(batch_update || !trace_cache))
    {
        vertex_ptr_cache[vertex_id] = pointer;
    }

    if (!vertex_block || vertex_block->get_length() == vertex_block->TOMBSTONE)
        return std::string_view();

    return std::string_view(vertex_block->get_data(), vertex_block->get_length());
}

size_t Transaction::get_vertex_gbp(vertex_t vertex_id)
{
    check_valid();

    if (vertex_id >= graph.vertex_id.load(std::memory_order_relaxed))
        return graph.block_manager.NULLPOINTER;

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
    VertexBlockHeader vertex_block;
    while (pointer != graph.block_manager.NULLPOINTER)
    {
        auto buffer_block = getBufferBlock(
            pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(VertexBlockHeader)));
        buffer_block.Copy((char *)(&vertex_block), sizeof(VertexBlockHeader));
        if (cmp_timestamp(vertex_block.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
            break;
        pointer = vertex_block.get_prev_pointer();
    }

    // if (!(batch_update || !trace_cache))
    //{
    //    vertex_ptr_cache[vertex_id] = pointer;
    //}

    if (pointer == graph.block_manager.NULLPOINTER || vertex_block.get_length() == vertex_block.TOMBSTONE)
        return graph.block_manager.NULLPOINTER;

    return pointer + sizeof(VertexBlockHeader);
}

std::pair<EdgeEntry *, char *>
Transaction::find_edge(vertex_t dst, EdgeBlockHeader *edge_block, size_t num_entries, size_t data_length)
{
    if (!edge_block)
        return {nullptr, nullptr};

    auto bloom_filter = edge_block->get_bloom_filter();
    if (bloom_filter.valid() && !bloom_filter.find(dst))
        return {nullptr, nullptr};

    auto entries = edge_block->get_entries() - num_entries;
    auto data = edge_block->get_data() + data_length;
    for (size_t i = 0; i < num_entries; i++)
    {
        data -= entries->get_length();
        if (entries->get_dst() == dst &&
            cmp_timestamp(entries->get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
            cmp_timestamp(entries->get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
        {
            return {entries, data};
        }
        entries++;
    }

    return {nullptr, nullptr};
}

uintptr_t Transaction::locate_edge_block(vertex_t src, label_t label)
{
    auto pointer = graph.edge_label_ptrs[src];
    if (pointer == graph.block_manager.NULLPOINTER)
        return pointer;
    auto edge_label_block = graph.block_manager.convert<EdgeLabelBlockHeader>(pointer);
    for (size_t i = 0; i < edge_label_block->get_num_entries(); i++)
    {
        auto label_entry = edge_label_block->get_entries()[i];
        if (label_entry.get_label() == label)
        {
            auto pointer = label_entry.get_pointer();
            while (pointer != graph.block_manager.NULLPOINTER)
            {
                auto edge_block = graph.block_manager.convert<EdgeBlockHeader>(pointer);
                if (cmp_timestamp(edge_block->get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
                    break;
                pointer = edge_block->get_prev_pointer();
            }
            return pointer;
        }
    }
    return graph.block_manager.NULLPOINTER;
}

uintptr_t Transaction::locate_edge_block_gbp(vertex_t src, label_t label)
{
    auto pointer = graph.edge_label_ptrs[src];
    if (pointer == graph.block_manager.NULLPOINTER)
        return pointer;
    auto buffer_block = getBufferBlock(
        pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeLabelBlockHeader)));
    auto edge_label_block = buffer_block.GetInnerObj<EdgeLabelBlockHeader>(0);
    buffer_block = getBufferBlock(pointer + sizeof(EdgeLabelBlockHeader),
                                  sizeof(EdgeLabelEntry) * edge_label_block.get_num_entries());

    for (size_t i = 0; i < edge_label_block.get_num_entries(); i++)
    {
        auto label_entry = buffer_block.GetInnerObj<EdgeLabelEntry>(i * sizeof(EdgeLabelEntry));
        if (label_entry.get_label() == label)
        {
            auto pointer = label_entry.get_pointer();
            while (pointer != graph.block_manager.NULLPOINTER)
            {
                auto buffer_block_entry = getBufferBlock(pointer, sizeof(EdgeBlockHeader));
                auto edge_block = buffer_block_entry.GetInnerObj<EdgeBlockHeader>(0);
                if (cmp_timestamp(edge_block.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
                    break;
                pointer = edge_block.get_prev_pointer();
            }
            return pointer;
        }
    }
    return graph.block_manager.NULLPOINTER;
}

std::vector<uintptr_t> Transaction::locate_edge_block_gbp(std::vector<std::pair<vertex_t, label_t>> &edgelist_infos)
{
    std::vector<gbp::BufferBlock> buffer_blocks;
    std::vector<uintptr_t> pointers;
    pointers.resize(edgelist_infos.size());
    std::vector<gbp::batch_request_type> blk_infos;

    for (size_t i = 0; i < edgelist_infos.size(); i++)
    {
        pointers[i] = graph.edge_label_ptrs[edgelist_infos[i].first];

        if (pointers[i] != graph.block_manager.NULLPOINTER)
            blk_infos.emplace_back(
                pointers[i],
                std::max(gbp::PAGE_SIZE_MEMORY - pointers[i] % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeLabelBlockHeader)), 0);
    }
    if (blk_infos.empty())
        return pointers;
    buffer_blocks.clear();
    getBufferBlockBatch(blk_infos, buffer_blocks);

    size_t buffer_block_cursor = 0;
    blk_infos.clear();
    std::vector<size_t> num_entries(edgelist_infos.size(), 0);
    for (size_t i = 0; i < edgelist_infos.size(); i++)
    {
        if (pointers[i] == graph.block_manager.NULLPOINTER)
            continue;
        auto edge_label_block = buffer_blocks[buffer_block_cursor++].GetInnerObj<EdgeLabelBlockHeader>(0);
        num_entries[i] = edge_label_block.get_num_entries();
        if (num_entries[i] == 0)
            continue;
        blk_infos.emplace_back(pointers[i] + sizeof(EdgeLabelBlockHeader), sizeof(EdgeLabelEntry) * num_entries[i], 0);
    }
    buffer_blocks.clear();
    getBufferBlockBatch(blk_infos, buffer_blocks);

    blk_infos.clear();
    assert(blk_infos.empty());
    buffer_block_cursor = 0;
    pointers.clear();
    pointers.resize(edgelist_infos.size(), graph.block_manager.NULLPOINTER);
    std::vector<bool> success_marks(pointers.size(), true);
    for (size_t j = 0; j < edgelist_infos.size(); j++)
    {
        for (size_t i = 0; i < num_entries[j]; i++)
        {
            auto label_entry =
                buffer_blocks[buffer_block_cursor].GetInnerObj<EdgeLabelEntry>(i * sizeof(EdgeLabelEntry));
            if (label_entry.get_label() == edgelist_infos[j].second)
            {
                pointers[j] = label_entry.get_pointer();
                if (pointers[j] != graph.block_manager.NULLPOINTER)
                {
                    blk_infos.emplace_back(pointers[j], sizeof(EdgeBlockHeader), 0);
                    success_marks[j] = false;
                }

                break;
            }
        }
        if (num_entries[j])
        {
            buffer_block_cursor++;
        }
    }
    while (!blk_infos.empty())
    {
        buffer_blocks.clear();
        getBufferBlockBatch(blk_infos, buffer_blocks);
        buffer_block_cursor = 0;

        blk_infos.clear();
        for (size_t i = 0; i < edgelist_infos.size(); i++)
        {
            if (success_marks[i])
            {
                continue;
            }
            auto edge_block = buffer_blocks[buffer_block_cursor++].GetInnerObj<EdgeBlockHeader>(0);
            if (cmp_timestamp(edge_block.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0)
            {
                success_marks[i] = true;
                continue;
            }
            pointers[i] = edge_block.get_prev_pointer();
            if (pointers[i] != graph.block_manager.NULLPOINTER)
            {
                blk_infos.emplace_back(pointers[i], sizeof(EdgeBlockHeader), 0);
            }
        }
    }

    return pointers;
}

void Transaction::ensure_no_confict(vertex_t src, label_t label)
{
    auto pointer = graph.edge_label_ptrs[src];
    if (pointer == graph.block_manager.NULLPOINTER)
        return;
    auto edge_label_block = graph.block_manager.convert<EdgeLabelBlockHeader>(pointer);
    for (size_t i = 0; i < edge_label_block->get_num_entries(); i++)
    {
        auto label_entry = edge_label_block->get_entries()[i];
        if (label_entry.get_label() == label)
        {
            auto pointer = label_entry.get_pointer();
            if (pointer != graph.block_manager.NULLPOINTER)
            {
                auto header = graph.block_manager.convert<EdgeBlockHeader>(pointer);
                if (header && cmp_timestamp(header->get_committed_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    throw RollbackExcept("Write-write confict on: " + std::to_string(src) + ": " +
                                         std::to_string(label) + ".");
            }
            return;
        }
    }
}

void Transaction::update_edge_label_block(vertex_t src, label_t label, uintptr_t edge_block_pointer)
{
    auto pointer = graph.edge_label_ptrs[src];
    auto edge_label_block = graph.block_manager.convert<EdgeLabelBlockHeader>(pointer);
    if (edge_label_block)
    {
        for (size_t i = 0; i < edge_label_block->get_num_entries(); i++)
        {
            auto &label_entry = edge_label_block->get_entries()[i];
            if (label_entry.get_label() == label)
            {
                label_entry.set_pointer(edge_block_pointer);
                return;
            }
        }
    }

    EdgeLabelEntry label_entry;
    label_entry.set_label(label);
    label_entry.set_pointer(edge_block_pointer);

    if (!edge_label_block || !edge_label_block->append(label_entry))
    {
        auto num_entries = edge_label_block ? edge_label_block->get_num_entries() : 0;
        auto size = sizeof(EdgeLabelBlockHeader) + (1 + num_entries) * sizeof(EdgeLabelEntry);
        auto order = size_to_order(size);
        // GBPLOG << "order and size " << (size_t)order << " " << size;
        auto new_pointer = graph.block_manager.alloc(order);

        auto new_edge_label_block = graph.block_manager.convert<EdgeLabelBlockHeader>(new_pointer);
        new_edge_label_block->fill(order, src, write_epoch_id, pointer);

        if (!batch_update)
        {
            block_cache.emplace_back(new_pointer, order);
            timestamps_to_update.emplace_back(new_edge_label_block->get_creation_time_pointer(),
                                              Graph::ROLLBACK_TOMBSTONE);
        }

        for (size_t i = 0; i < num_entries; i++)
        {
            auto old_label_entry = edge_label_block->get_entries()[i];
            new_edge_label_block->append(old_label_entry);
        }

        new_edge_label_block->append(label_entry);

        graph.edge_label_ptrs[src] = new_pointer;
    }
}

void Transaction::put_edge(vertex_t src, label_t label, vertex_t dst, std::string_view edge_data, bool force_insert)
{
    check_valid();
    check_writable();
    check_vertex_id(src);
    check_vertex_id(dst);

    uintptr_t pointer;
    if (batch_update)
    {
        graph.vertex_futexes[src].lock();
        pointer = locate_edge_block(src, label);
    }
    else
    {
        ensure_vertex_lock(src);
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            ensure_no_confict(src, label);
            pointer = locate_edge_block(src, label);
            edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }

    EdgeEntry entry;
    entry.set_length(edge_data.size());
    entry.set_dst(dst);
    entry.set_creation_time(write_epoch_id);
    entry.set_deletion_time(Graph::ROLLBACK_TOMBSTONE);

    auto edge_block = graph.block_manager.convert<EdgeBlockHeader>(pointer);

    auto [num_entries, data_length] =
        edge_block ? get_num_entries_data_length_cache(edge_block) : std::pair<size_t, size_t>{0, 0};
    // GBPLOG << "cp";
    // if (pointer == graph.block_manager.NULLPOINTER)
    // {
    //     assert(edge_block == nullptr);
    //     assert(num_entries == 0);
    //     assert(data_length == 0);
    // }
    // else
    // {
    //     GBPLOG << edge_block->get_block_size() << " " << (uintptr_t)edge_block->get_data() << " "
    //            << (uintptr_t)edge_block->get_data() << " " << edge_block->get_data_length() << " "
    //            << edge_block->get_num_entries();
    // }

    if (!edge_block || !edge_block->has_space(entry, num_entries, data_length))
    {
        auto size = sizeof(EdgeBlockHeader) + (1 + num_entries) * sizeof(EdgeEntry) + data_length + entry.get_length();
        auto order = size_to_order(size);

        if (order > edge_block->BLOOM_FILTER_PORTION &&
            size + (1ul << (order - edge_block->BLOOM_FILTER_PORTION)) >= (1ul << edge_block->BLOOM_FILTER_THRESHOLD))
        {
            size += 1ul << (order - edge_block->BLOOM_FILTER_PORTION);
        }
        order = size_to_order(size);
        // GBPLOG << "aa " << (uintptr_t)edge_block << " " << pointer;
        // GBPLOG << "aa " << (uintptr_t)edge_block << " " << edge_block->get_order();

        // GBPLOG << "order and size " << (size_t)order << " " << size << " " << num_entries << " " << data_length << "
        // "
        //        << entry.get_length();
        // GBPLOG << "aa";

        auto new_pointer = graph.block_manager.alloc(order);

        auto new_edge_block = graph.block_manager.convert<EdgeBlockHeader>(new_pointer);
        new_edge_block->fill(order, src, write_epoch_id, pointer, write_epoch_id);

        if (!batch_update)
        {
            block_cache.emplace_back(new_pointer, order);
            timestamps_to_update.emplace_back(new_edge_block->get_creation_time_pointer(), Graph::ROLLBACK_TOMBSTONE);
            // timestamps_to_update.emplace_back(new_edge_block->get_committed_time_pointer(),
            // Graph::ROLLBACK_TOMBSTONE); update when commit
        }

        if (edge_block)
        {
            auto entries = edge_block->get_entries();
            auto data = edge_block->get_data();

            auto bloom_filter = new_edge_block->get_bloom_filter();
            for (size_t i = 0; i < num_entries; i++)
            {
                entries--;
                // skip deleted edges
                if (cmp_timestamp(entries->get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                {
                    auto edge = new_edge_block->append(*entries, data, bloom_filter); // direct update size
                    if (!batch_update && edge->get_creation_time() == -local_txn_id)
                        timestamps_to_update.emplace_back(edge->get_creation_time_pointer(), Graph::ROLLBACK_TOMBSTONE);
                }
                data += entries->get_length();
            }
        }

        if (batch_update)
            update_edge_label_block(src, label, new_pointer);

        pointer = new_pointer;
        edge_block = new_edge_block;
        std::tie(num_entries, data_length) = new_edge_block->get_num_entries_data_length_atomic();
    }
    // GBPLOG << "cp";

    if (!force_insert)
    {
        auto prev_edge = find_edge(dst, edge_block, num_entries, data_length);

        if (prev_edge.first)
        {
            prev_edge.first->set_deletion_time(write_epoch_id);
            if (!batch_update)
                timestamps_to_update.emplace_back(prev_edge.first->get_deletion_time_pointer(),
                                                  Graph::ROLLBACK_TOMBSTONE);
        }
    }

    auto edge = edge_block->append_without_update_size(entry, edge_data.data(), num_entries, data_length);
    set_num_entries_data_length_cache(edge_block, num_entries + 1, data_length + entry.get_length());
    if (!batch_update)
        timestamps_to_update.emplace_back(edge->get_creation_time_pointer(), Graph::ROLLBACK_TOMBSTONE);

    graph.compact_table.local().emplace(src);

    if (batch_update)
    {
        graph.vertex_futexes[src].unlock();
    }
    else
    {
        edge_ptr_cache[std::make_pair(src, label)] = pointer;
        ++wal_num_ops();
        wal_append(OPType::PutEdge);
        wal_append(src);
        wal_append(label);
        wal_append(dst);
        wal_append(force_insert);
        wal_append(edge_data);
    }
}

bool Transaction::del_edge(vertex_t src, label_t label, vertex_t dst)
{
    check_valid();
    check_writable();
    check_vertex_id(src);
    check_vertex_id(dst);

    uintptr_t pointer;
    if (batch_update)
    {
        graph.vertex_futexes[src].lock();
        pointer = locate_edge_block(src, label);
    }
    else
    {
        ensure_vertex_lock(src);
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            ensure_no_confict(src, label);
            pointer = locate_edge_block(src, label);
            edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }

    auto edge_block = graph.block_manager.convert<EdgeBlockHeader>(pointer);

    if (!edge_block)
        return false;

    auto [num_entries, data_length] = get_num_entries_data_length_cache(edge_block);
    auto edge = find_edge(dst, edge_block, num_entries, data_length);

    if (edge.first)
    {
        edge.first->set_deletion_time(write_epoch_id);
        if (!batch_update)
            timestamps_to_update.emplace_back(edge.first->get_deletion_time_pointer(), Graph::ROLLBACK_TOMBSTONE);
    }

    graph.compact_table.local().emplace(src);

    if (batch_update)
    {
        graph.vertex_futexes[src].unlock();
    }
    else
    {
        edge_ptr_cache[std::make_pair(src, label)] = pointer;
        // make sure commit will change committed_time
        set_num_entries_data_length_cache(edge_block, num_entries, data_length);

        ++wal_num_ops();
        wal_append(OPType::DelEdge);
        wal_append(src);
        wal_append(label);
        wal_append(dst);
    }

    if (edge.first != nullptr)
        return true;
    else
        return false;
}

std::string_view Transaction::get_edge(vertex_t src, label_t label, vertex_t dst)
{
    check_valid();

    if (src >= graph.vertex_id.load(std::memory_order_relaxed))
        return std::string_view();

    uintptr_t pointer;
    if (batch_update || !trace_cache)
    {
        pointer = locate_edge_block(src, label);
    }
    else
    {
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            pointer = locate_edge_block(src, label);
            // edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }

    auto edge_block = graph.block_manager.convert<EdgeBlockHeader>(pointer);

    if (!edge_block)
        return std::string_view();

    auto [num_entries, data_length] = get_num_entries_data_length_cache(edge_block);
    auto edge = find_edge(dst, edge_block, num_entries, data_length);

    if (edge.first)
        return std::string_view(edge.second, edge.first->get_length());
    else
        return std::string_view();
}

std::string Transaction::get_edge_gbp(vertex_t src, label_t label, vertex_t dst)
{
    // assert(false);
    check_valid();

    if (src >= graph.vertex_id.load(std::memory_order_relaxed))
        return std::string();

    uintptr_t pointer;
    if (batch_update || !trace_cache)
    {
        pointer = locate_edge_block_gbp(src, label);
    }
    else
    {
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            pointer = locate_edge_block_gbp(src, label);
            // edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }

    if (graph.block_manager.NULLPOINTER == pointer)
        return std::string();

    auto buffer_block = getBufferBlock(
        pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeBlockHeader)));
    auto edge_block = buffer_block.GetInnerObj<EdgeBlockHeader>();
    buffer_block = getBufferBlock(pointer, edge_block.get_block_size());
    edge_block = buffer_block.GetInnerObj<EdgeBlockHeader>(); // 需要进一步的优化

    auto [num_entries, data_length] = get_num_entries_data_length_cache(&edge_block);
    auto edge = find_edge(dst, &edge_block, num_entries, data_length);

    if (edge.first)
        return std::string(edge.second, edge.first->get_length());
    else
        return std::string();
}

void Transaction::abort()
{
    check_valid();

    for (const auto &p : timestamps_to_update)
    {
        *p.first = p.second;
    }

    for (const auto &vid : new_vertex_cache)
    {
        graph.recycled_vertex_ids.push(vid);
    }

    for (const auto &p : block_cache)
    {
        graph.block_manager.free(p.first, p.second);
    }

    clean();
}

EdgeIterator Transaction::get_edges(vertex_t src, label_t label, bool reverse)
{
    // assert(false);
    check_valid();

    if (src >= graph.vertex_id.load(std::memory_order_relaxed))
        return EdgeIterator(nullptr, nullptr, 0, 0, read_epoch_id, local_txn_id, reverse);

    uintptr_t pointer;
    if (batch_update || !trace_cache)
    {
        pointer = locate_edge_block(src, label);
    }
    else
    {
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            pointer = locate_edge_block(src, label);
            // edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }

    auto edge_block = graph.block_manager.convert<EdgeBlockHeader>(pointer);

    if (!edge_block)
        return EdgeIterator(nullptr, nullptr, 0, 0, read_epoch_id, local_txn_id, reverse);

    auto [num_entries, data_length] = get_num_entries_data_length_cache(edge_block);
    // {

    //     auto buffer_block = getBufferBlock(
    //         pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeBlockHeader)));
    //     auto edge_block = buffer_block.GetInnerObj<EdgeBlockHeader>();
    //     auto [num_entries_t, data_length_t] = get_num_entries_data_length_cache(&edge_block);
    //     assert(num_entries == num_entries_t && data_length == data_length_t);
    // }

    return EdgeIterator(edge_block->get_entries(), edge_block->get_data(), num_entries, data_length, read_epoch_id,
                        local_txn_id, reverse);
}

EdgeIterator_gbp Transaction::get_edges_gbp(vertex_t src, label_t label, bool reverse)
{
    check_valid();

    if (src >= graph.vertex_id.load(std::memory_order_relaxed))
        return EdgeIterator_gbp(0, 0, 0, 0, read_epoch_id, local_txn_id, reverse);

    uintptr_t pointer;
    if (batch_update || !trace_cache)
    {
        pointer = locate_edge_block_gbp(src, label);
    }
    else
    {
        auto cache_iter = edge_ptr_cache.find(std::make_pair(src, label));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointer = cache_iter->second;
        }
        else
        {
            pointer = locate_edge_block_gbp(src, label);
            // edge_ptr_cache.emplace_hint(cache_iter, std::make_pair(src, label), pointer);
        }
    }
    if (graph.block_manager.NULLPOINTER == pointer)
        return EdgeIterator_gbp(0, 0, 0, 0, read_epoch_id, local_txn_id, reverse);

    auto buffer_block = getBufferBlock(
        pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeBlockHeader)));
    auto edge_block = buffer_block.GetInnerObj<EdgeBlockHeader>();

    auto [num_entries, data_length] = get_num_entries_data_length_cache(&edge_block);
    return EdgeIterator_gbp(pointer + edge_block.get_entries_gbp(), pointer + edge_block.get_data_gbp(), num_entries,
                            data_length, read_epoch_id, local_txn_id, reverse);
}

std::vector<EdgeIterator_gbp>
Transaction::get_edges_gbp(std::vector<std::tuple<vertex_t, label_t, bool>> &edgelist_infos)
{
    check_valid();
    std::vector<EdgeIterator_gbp> ret;
    std::vector<bool> success_marks(edgelist_infos.size(), false);
    std::vector<uintptr_t> pointers(edgelist_infos.size(), graph.block_manager.NULLPOINTER);
    // auto src = std::get<0>(edgelist_infos[0]);
    // auto label = std::get<1>(edgelist_infos[0]);
    // auto reverse = std::get<2>(edgelist_infos[0]);
    std::vector<std::pair<vertex_t, label_t>> reqs;
    for (auto i = 0; i < edgelist_infos.size(); i++)
    {
        if (std::get<0>(edgelist_infos[i]) >= graph.vertex_id.load(std::memory_order_relaxed))
        {
            success_marks[i] = true;
            continue;
        }

        auto cache_iter =
            edge_ptr_cache.find(std::make_pair(std::get<0>(edgelist_infos[i]), std::get<1>(edgelist_infos[i])));
        if (cache_iter != edge_ptr_cache.end())
        {
            pointers[i] = cache_iter->second;
            success_marks[i] = true;
        }
        else
        {
            reqs.emplace_back(std::get<0>(edgelist_infos[i]), std::get<1>(edgelist_infos[i]));
        }
    }
    auto result = locate_edge_block_gbp(reqs);
    size_t result_cursor = 0;
    std::vector<gbp::batch_request_type> blk_infos;
    for (auto i = 0; i < edgelist_infos.size(); i++)
    {
        if (!success_marks[i])
        {
            pointers[i] = result[result_cursor++];
            if (pointers[i] != graph.block_manager.NULLPOINTER)
            {
                blk_infos.emplace_back(
                    pointers[i],
                    std::max(gbp::PAGE_SIZE_MEMORY - pointers[i] % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeBlockHeader)), 0);
            }
        }
    }
    std::vector<gbp::BufferBlock> buffer_blocks;
    getBufferBlockBatch(blk_infos, buffer_blocks);
    blk_infos.clear();
    size_t buffer_block_cursor = 0;
    for (auto i = 0; i < edgelist_infos.size(); i++)
    {
        if (pointers[i] != graph.block_manager.NULLPOINTER)
        {
            auto edge_block = buffer_blocks[buffer_block_cursor++].GetInnerObj<EdgeBlockHeader>();
            auto [num_entries, data_length] = get_num_entries_data_length_cache(&edge_block);
            ret.emplace_back(num_entries, data_length, read_epoch_id, local_txn_id, std::get<2>(edgelist_infos[0]));
            if (num_entries)
            {
                blk_infos.emplace_back(pointers[i] + edge_block.get_entries_gbp() - sizeof(EdgeEntry) * num_entries,
                                       sizeof(EdgeEntry) * num_entries, 0);
                blk_infos.emplace_back(pointers[i] + edge_block.get_data_gbp(), data_length, 0);
            }
        }
        else
        {
            ret.emplace_back(0, 0, 0, 0, read_epoch_id, local_txn_id, std::get<2>(edgelist_infos[0]));
        }
    }
    getBufferBlockBatch(blk_infos, buffer_blocks);
    buffer_block_cursor = 0;
    for (auto i = 0; i < edgelist_infos.size(); i++)
    {
        if (ret[i].num_entries)
        {
            ret[i].init(buffer_blocks[buffer_block_cursor++], buffer_blocks[buffer_block_cursor++]);
        }
    }
    // auto pointer = pointers[0];
    // auto reverse = false;
    // if (graph.block_manager.NULLPOINTER == pointer)
    // {
    //     ret.emplace_back(0, 0, 0, 0, read_epoch_id, local_txn_id, reverse);
    //     return ret;
    // }
    // auto buffer_block = getBufferBlock(
    //     pointer, std::max(gbp::PAGE_SIZE_MEMORY - pointer % gbp::PAGE_SIZE_MEMORY, sizeof(EdgeBlockHeader)));
    // auto edge_block = buffer_block.GetInnerObj<EdgeBlockHeader>();

    // auto [num_entries, data_length] = get_num_entries_data_length_cache(&edge_block);
    // auto entries_vec = getBufferBlock(pointer + edge_block.get_entries_gbp() - sizeof(EdgeEntry) * num_entries,
    //                                   sizeof(EdgeEntry) * num_entries);
    // auto data_vec = getBufferBlock(pointer + edge_block.get_data_gbp(), data_length);

    // ret.emplace_back(num_entries, data_length, read_epoch_id, local_txn_id, reverse);
    // ret.back().init(entries_vec, data_vec);
    return ret;
}

timestamp_t Transaction::commit(bool wait_visable)
{
    check_valid();
    check_writable();

    if (batch_update)
        return read_epoch_id;

    auto [commit_epoch_id, num_unfinished] = graph.commit_manager.register_commit(wal);

    for (const auto &p : vertex_ptr_cache)
    {
        auto vertex_id = p.first;
        auto pointer = p.second;
        if (graph.vertex_ptrs[vertex_id] != pointer)
            graph.vertex_ptrs[vertex_id] = pointer;
    }

    for (const auto &vid : recycled_vertex_cache)
    {
        graph.recycled_vertex_ids.push(vid);
    }

    for (const auto &p : edge_block_num_entries_data_length_cache)
    {
        p.first->set_num_entries_data_length_atomic(p.second.first, p.second.second);
        timestamps_to_update.emplace_back(p.first->get_committed_time_pointer(), p.first->get_committed_time());
        p.first->set_committed_time(write_epoch_id);
    }

    for (const auto &p : edge_ptr_cache)
    {
        auto prev_pointer = locate_edge_block(p.first.first, p.first.second);
        if (p.second != prev_pointer)
        {
            update_edge_label_block(p.first.first, p.first.second, p.second);
        }
    }

    for (const auto &p : timestamps_to_update)
    {
        *p.first = commit_epoch_id;
    }

    clean();

    graph.commit_manager.finish_commit(commit_epoch_id, num_unfinished, wait_visable);

    return commit_epoch_id;
}
