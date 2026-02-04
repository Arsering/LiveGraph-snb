#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::query6(std::vector<Query6Response> &_return, const Query6Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagId = tagSchema.findName(request.tagName);
    if (tagId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::unordered_map<uint64_t, int> idx;
    std::vector<vertex_t> message_vids;
    {
        uint64_t vid = tagId;
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Tag2Post);
            while (nbrs.valid())
            {
                message_vids.emplace_back(nbrs.dst_id());
                nbrs.next();
            }
        }
    }
    auto [message_schemas, _] = engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (auto i = 0; i < message_schemas.size(); i++)
    {
        auto &message_schema = message_schemas[i];
        if (*std::lower_bound(friends.begin(), friends.end(), message_schema.creator) == message_schema.creator)
        {
            edgelist_infos.emplace_back(message_vids[i], (label_t)snb::EdgeSchema::Post2Tag, false);
        }
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    for (auto i = 0; i < nbrses.size(); i++)
    {
        auto &nbrs = nbrses[i];
        while (nbrs.valid())
        {
            if (nbrs.dst_id() != tagId)
            {
                idx[nbrs.dst_id()]++;
            }
            nbrs.next();
        }
    }
    std::set<std::pair<int, std::string>> idx_by_count;

    std::set<std::pair<size_t, vertex_t>> order_container;
    size_t min_count = std::numeric_limits<size_t>::max() - 1;
    size_t count_of_min_count = 0;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (order_container.size() < (size_t)request.limit || (i->second >= min_count))
        {
            order_container.emplace(i->second, i->first);

            if (i->second == min_count)
            {
                count_of_min_count++;
            }
            else if (i->second < min_count || min_count == std::numeric_limits<size_t>::max() - 1)
            {
                min_count = i->second;
                count_of_min_count = 1;
            }
            else
            {
                if ((order_container.size() - (size_t)request.limit) == count_of_min_count)
                {
                    while (order_container.begin()->first == min_count)
                    {
                        order_container.erase(order_container.begin());
                    }
                    min_count = order_container.begin()->first;
                    count_of_min_count = 0;
                    for (auto &item : order_container)
                    {
                        if (item.first != min_count)
                        {
                            break;
                        }
                        count_of_min_count++;
                    }
                }
            }
        }
    }
    std::vector<vertex_t> tag_vids;
    std::vector<size_t> counts;
    for (auto &item : order_container)
    {
        tag_vids.emplace_back(item.second);
        counts.emplace_back(item.first);
    }
    auto [tag_schemas, tag_bufs] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(tag_vids);
    for (auto i = 0; i < tag_schemas.size(); i++)
    {
        auto &tag_schema = tag_schemas[i];
        auto &tag_buf = tag_bufs[i];
        idx_by_count.emplace(-counts[i],
                             tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen()));
    }
    tag_bufs.clear();
    while (idx_by_count.size() > (size_t)request.limit)
        idx_by_count.erase(*idx_by_count.rbegin());

    // for (auto i = idx.begin(); i != idx.end(); i++)
    // {
    //     if (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first >= -i->second)
    //     {
    //         auto [tag_schema, tag_buf] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(i->first);

    //         idx_by_count.emplace(-i->second,
    //                              tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen()));
    //         while (idx_by_count.size() > (size_t)request.limit)
    //             idx_by_count.erase(*idx_by_count.rbegin());
    //     }
    // }
    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first;
        _return.back().tagName = p.second;
    }
}
#else
void InteractiveHandler::query6(std::vector<Query6Response> &_return, const Query6Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagId = tagSchema.findName(request.tagName);
    if (tagId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::unordered_map<uint64_t, int> idx;
    {
        uint64_t vid = tagId;
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Tag2Post);
            while (nbrs.valid())
            {
                // auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                auto [message_schema, _] =
                    engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());

                if (*std::lower_bound(friends.begin(), friends.end(), message_schema.creator) == message_schema.creator)
                {
                    uint64_t vid = nbrs.dst_id();
                    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid())
                    {
                        if (nbrs.dst_id() != tagId)
                        {
                            idx[nbrs.dst_id()]++;
                        }
                        nbrs.next();
                    }
                }
                nbrs.next();
            }
        }
    }
    std::set<std::pair<int, std::string>> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first >= -i->second)
        {
            // auto tag = (snb::TagSchema::Tag *)engine.get_vertex(i->first).data();
            auto [tag_schema, tag_buf] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(i->first);
            // tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen());
            idx_by_count.emplace(-i->second,
                                 tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen()));
            while (idx_by_count.size() > (size_t)request.limit)
                idx_by_count.erase(*idx_by_count.rbegin());
        }
    }
    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first;
        _return.back().tagName = p.second;
    }
}
#endif
#else
void InteractiveHandler::query6(std::vector<Query6Response> &_return, const Query6Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagId = tagSchema.findName(request.tagName);
    if (tagId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::unordered_map<uint64_t, int> idx;
    {
        uint64_t vid = tagId;
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Tag2Post);
            while (nbrs.valid())
            {
                auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                if (*std::lower_bound(friends.begin(), friends.end(), message->creator) == message->creator)
                {
                    uint64_t vid = nbrs.dst_id();
                    auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid())
                    {
                        if (nbrs.dst_id() != tagId)
                        {
                            idx[nbrs.dst_id()]++;
                        }
                        nbrs.next();
                    }
                }
                nbrs.next();
            }
        }
    }
    std::set<std::pair<int, std::string>> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first >= -i->second)
        {
            auto tag = (snb::TagSchema::Tag *)engine.get_vertex(i->first).data();
            idx_by_count.emplace(-i->second, std::string(tag->name(), tag->nameLen()));
            while (idx_by_count.size() > (size_t)request.limit)
                idx_by_count.erase(*idx_by_count.rbegin());
        }
    }
    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first;
        _return.back().tagName = p.second;
    }
}
#endif