#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::query4(std::vector<Query4Response> &_return, const Query4Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, std::pair<uint64_t, int>> idx;

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Post_creator, false);
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    edgelist_infos.clear();
    std::vector<vertex_t> message_vids;
    for (size_t i = 0; i < friends.size(); i++)
    {
        auto nbrs = nbrses[i];
        while (nbrs.valid())
        {
            uint64_t date = *(uint64_t *)nbrs.edge_data().data();
            if (date < endDate)
            {
                message_vids.emplace_back(nbrs.dst_id());
                edgelist_infos.emplace_back(nbrs.dst_id(), (label_t)snb::EdgeSchema::Post2Tag, false);
            }
            nbrs.next();
        }
    }

    nbrses = engine.get_edges_gbp(edgelist_infos);
    auto [message_schemas, _] = engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
    for (auto i = 0; i < message_vids.size(); i++)
    {
        auto &message_schema = message_schemas[i];
        {
            auto &nbrs = nbrses[i];
            while (nbrs.valid())
            {
                auto iter = idx.find(nbrs.dst_id());
                if (iter == idx.end())
                {
                    idx.emplace(nbrs.dst_id(), std::make_pair(message_schema.creationDate, 1));
                }
                else
                {
                    iter->second.first = std::min(message_schema.creationDate, iter->second.first);
                    iter->second.second++;
                }
                nbrs.next();
            }
        }
    }
    nbrses.clear();

    std::set<std::pair<int, std::string>> idx_by_count;
    std::set<std::pair<size_t, vertex_t>> order_container;
    size_t min_count = 0;
    size_t count_of_min_count = 0;
    {
        for (auto i = idx.begin(); i != idx.end(); i++)
        {
            if (i->second.first >= (uint64_t)request.startDate)
            {
                if (order_container.size() < (size_t)request.limit || (i->second.second >= min_count))
                {
                    order_container.emplace(i->second.second, i->first);
                    if (i->second.second == min_count)
                    {
                        count_of_min_count++;
                    }
                    else if (i->second.second < min_count || min_count == 0)
                    {
                        min_count = i->second.second;
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

    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first;
        _return.back().tagName = p.second;
    }
}
#else
void InteractiveHandler::query4(std::vector<Query4Response> &_return, const Query4Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, std::pair<uint64_t, int>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
        while (nbrs.valid())
        {
            uint64_t date = *(uint64_t *)nbrs.edge_data().data();
            if (date < endDate)
            {
                uint64_t vid = nbrs.dst_id();
                auto [message_schema, _] = engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(vid);
                {
                    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid())
                    {
                        auto iter = idx.find(nbrs.dst_id());
                        if (iter == idx.end())
                        {
                            idx.emplace(nbrs.dst_id(), std::make_pair(message_schema.creationDate, 1));
                        }
                        else
                        {
                            iter->second.first = std::min(message_schema.creationDate, iter->second.first);
                            iter->second.second++;
                        }
                        nbrs.next();
                    }
                }
            }
            nbrs.next();
        }
    }
    std::set<std::pair<int, std::string>> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (i->second.first >= (uint64_t)request.startDate &&
            (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first >= -i->second.second))
        {
            auto [tag_schema, tag_buf] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(i->first);

            idx_by_count.emplace(-i->second.second,
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
void InteractiveHandler::query4(std::vector<Query4Response> &_return, const Query4Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, std::pair<uint64_t, int>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
        while (nbrs.valid())
        {
            uint64_t date = *(uint64_t *)nbrs.edge_data().data();
            if (date < endDate)
            {
                uint64_t vid = nbrs.dst_id();
                auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                {
                    auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid())
                    {
                        auto iter = idx.find(nbrs.dst_id());
                        if (iter == idx.end())
                        {
                            idx.emplace(nbrs.dst_id(), std::make_pair(message->creationDate, 1));
                        }
                        else
                        {
                            iter->second.first = std::min(message->creationDate, iter->second.first);
                            iter->second.second++;
                        }
                        nbrs.next();
                    }
                }
            }
            nbrs.next();
        }
    }
    std::set<std::pair<int, std::string>> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (i->second.first >= (uint64_t)request.startDate &&
            (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first >= -i->second.second))
        {
            auto tag = (snb::TagSchema::Tag *)engine.get_vertex(i->first).data();
            idx_by_count.emplace(-i->second.second, std::string(tag->name(), tag->nameLen()));
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