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

#include "blocks.hpp"
#include "graph.hpp"
#include "utils.hpp"

namespace livegraph
{

    class EdgeIterator_gbp
    {
    public:
        EdgeIterator_gbp(size_t entries_addr,
                         size_t data_addr,
                         size_t _num_entries,
                         size_t _data_length,
                         timestamp_t _read_epoch_id,
                         timestamp_t _local_txn_id,
                         bool _reverse)
            : num_entries(_num_entries),
              data_length(_data_length),
              read_epoch_id(_read_epoch_id),
              local_txn_id(_local_txn_id),
              reverse(_reverse)
        {
            if (_num_entries)
            {
                entries_vec =
                    getBufferBlock(entries_addr - sizeof(EdgeEntry) * num_entries, sizeof(EdgeEntry) * num_entries);
                data_vec = getBufferBlock(data_addr, data_length);
            }

            if (!reverse)
            {
                entries_cursor = 0;        // at the begining
                data_cursor = data_length; // at the end
            }
            else
            {
                entries_cursor = num_entries; // at the begining
                data_cursor = 0;              // at the end
            }
            if (!num_entries)
                return;

            if (!reverse)
            {
                while (valid())
                {
                    entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry), entries_cursor * sizeof(EdgeEntry));
                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    data_cursor -= entry_cur_.get_length();
                    entries_cursor++;
                }
            }
            else
            {
                while (valid())
                {
                    entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry),
                                     (entries_cursor - 1) * sizeof(EdgeEntry));
                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    data_cursor += entry_cur_.get_length();
                    entries_cursor--;
                }
            }
        }

        EdgeIterator_gbp(size_t _num_entries,
                         size_t _data_length,
                         timestamp_t _read_epoch_id,
                         timestamp_t _local_txn_id,
                         bool _reverse)
            : num_entries(_num_entries),
              data_length(_data_length),
              read_epoch_id(_read_epoch_id),
              local_txn_id(_local_txn_id),
              reverse(_reverse)
        {
        }

        void init(gbp::BufferBlock entries_vec_, gbp::BufferBlock data_vec_)
        {
            entries_vec = entries_vec_;
            data_vec = data_vec_;

            if (!reverse)
            {
                entries_cursor = 0;        // at the begining
                data_cursor = data_length; // at the end
            }
            else
            {
                entries_cursor = num_entries; // at the begining
                data_cursor = 0;              // at the end
            }
            if (!num_entries)
                return;

            if (!reverse)
            {
                while (valid())
                {
                    entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry), entries_cursor * sizeof(EdgeEntry));
                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    data_cursor -= entry_cur_.get_length();
                    entries_cursor++;
                }
            }
            else
            {
                while (valid())
                {
                    entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry),
                                     (entries_cursor - 1) * sizeof(EdgeEntry));
                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    data_cursor += entry_cur_.get_length();
                    entries_cursor--;
                }
            }
        }

        EdgeIterator_gbp(const EdgeIterator_gbp &) = default;

        EdgeIterator_gbp(EdgeIterator_gbp &&) = default;

        bool valid() const
        {
            if (!reverse)
                return !(entries_cursor == num_entries);
            else
                return !(entries_cursor == 0);
        }

        void next()
        {
            if (!reverse)
            {
                entries_cursor++;
                while (valid())
                {
                    data_cursor -= entry_cur_.get_length();
                    assert(entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry),
                                            entries_cursor * sizeof(EdgeEntry)) == sizeof(EdgeEntry));
                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    else
                    {
                        assert(cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0);
                        assert(cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0);
                        assert(false);
                    }

                    entries_cursor++;
                }
            }
            else
            {
                entries_cursor--;
                while (valid())
                {
                    data_cursor += entry_cur_.get_length();
                    entries_vec.Copy((char *)(&entry_cur_), sizeof(EdgeEntry),
                                     (entries_cursor - 1) * sizeof(EdgeEntry));

                    if (cmp_timestamp(entry_cur_.get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entry_cur_.get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }

                    entries_cursor--;
                }
            }
        }

        vertex_t dst_id() const
        {
            if (!valid())
                return Graph::VERTEX_TOMBSTONE;
            if (!reverse)
                return entry_cur_.get_dst();
            else
                return entry_cur_.get_dst();
        }

        std::string edge_data() const
        {
            std::string ret;
            if (!valid())
                ret.resize(0);
            if (!reverse)
            {
                ret.resize(entry_cur_.get_length());
                data_vec.Copy((char *)ret.data(), entry_cur_.get_length(), data_cursor - entry_cur_.get_length());
                // return std::string_view(data_cursor - entries_cursor->get_length(), entries_cursor->get_length());
            }
            else
            {
                ret.resize(entry_cur_.get_length());
                data_vec.Copy((char *)ret.data(), entry_cur_.get_length(), data_cursor);
                // return std::string_view(data_cursor, (entries_cursor - 1)->get_length());
            }
            return ret;
        }

        const size_t num_entries;

    private:
        gbp::BufferBlock entries_vec;
        EdgeEntry entry_cur_;

        gbp::BufferBlock data_vec;

        size_t data_length;
        timestamp_t read_epoch_id;
        timestamp_t local_txn_id;
        bool reverse;
        size_t entries_cursor;
        size_t data_cursor;
    };

    class EdgeIterator
    {
    public:
        EdgeIterator(EdgeEntry *_entries,
                     char *_data,
                     size_t _num_entries,
                     size_t _data_length,
                     timestamp_t _read_epoch_id,
                     timestamp_t _local_txn_id,
                     bool _reverse)
            : entries(_entries),
              data(_data),
              num_entries(_num_entries),
              data_length(_data_length),
              read_epoch_id(_read_epoch_id),
              local_txn_id(_local_txn_id),
              reverse(_reverse)
        {
            if (!reverse)
            {
                entries_cursor = entries - num_entries; // at the begining
                data_cursor = data + data_length;       // at the end
            }
            else
            {
                entries_cursor = entries; // at the end
                data_cursor = data;       // at the begining
            }

            if (!reverse)
            {
                while (valid())
                {
                    if (cmp_timestamp(entries_cursor->get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entries_cursor->get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                    data_cursor -= entries_cursor->get_length();
                    entries_cursor++;
                }
            }
            else
            {
                while (valid())
                {
                    if (cmp_timestamp((entries_cursor - 1)->get_creation_time_pointer(), read_epoch_id, local_txn_id) <=
                            0 &&
                        cmp_timestamp((entries_cursor - 1)->get_deletion_time_pointer(), read_epoch_id, local_txn_id) >
                            0)
                    {
                        break;
                    }
                    data_cursor += (entries_cursor - 1)->get_length();
                    entries_cursor--;
                }
            }
        }

        EdgeIterator(const EdgeIterator &) = default;

        EdgeIterator(EdgeIterator &&) = default;

        bool valid() const
        {
            if (!reverse)
                return !(entries_cursor == entries);
            else
                return !(entries_cursor == entries - num_entries);
        }

        void next()
        {
            if (!reverse)
            {
                while (valid())
                {
                    data_cursor -= entries_cursor->get_length();
                    entries_cursor++;
                    if (cmp_timestamp(entries_cursor->get_creation_time_pointer(), read_epoch_id, local_txn_id) <= 0 &&
                        cmp_timestamp(entries_cursor->get_deletion_time_pointer(), read_epoch_id, local_txn_id) > 0)
                    {
                        break;
                    }
                }
            }
            else
            {
                while (valid())
                {
                    data_cursor += (entries_cursor - 1)->get_length();
                    entries_cursor--;
                    if (cmp_timestamp((entries_cursor - 1)->get_creation_time_pointer(), read_epoch_id, local_txn_id) <=
                            0 &&
                        cmp_timestamp((entries_cursor - 1)->get_deletion_time_pointer(), read_epoch_id, local_txn_id) >
                            0)
                    {
                        break;
                    }
                }
            }
        }

        vertex_t dst_id() const
        {
            if (!valid())
                return Graph::VERTEX_TOMBSTONE;
            if (!reverse)
                return entries_cursor->get_dst();
            else
                return (entries_cursor - 1)->get_dst();
        }

        std::string_view edge_data() const
        {
            if (!valid())
                return std::string_view();
            if (!reverse)
                return std::string_view(data_cursor - entries_cursor->get_length(), entries_cursor->get_length());
            else
                return std::string_view(data_cursor, (entries_cursor - 1)->get_length());
        }

    private:
        EdgeEntry *entries;
        char *data;
        size_t num_entries;
        size_t data_length;
        timestamp_t read_epoch_id;
        timestamp_t local_txn_id;
        bool reverse;
        EdgeEntry *entries_cursor;
        char *data_cursor;
    };

} // namespace livegraph
