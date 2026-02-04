#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::query5(std::vector<Query5Response> &_return, const Query5Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, int> idx;

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Forum_member, false);
    }
    {
        auto nbrses = engine.get_edges_gbp(edgelist_infos);
        for (size_t i = 0; i < friends.size(); i++)
        {
            {
                auto &nbrs = nbrses[i];
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() > (uint64_t)request.minDate)
                {
                    uint64_t posts = *(uint64_t *)(nbrs.edge_data().data() + sizeof(uint64_t));
                    idx[nbrs.dst_id()] += posts;
                    nbrs.next();
                }
            }
        }
    }
    std::map<std::pair<int, size_t>, std::string> idx_by_count;

    std::set<std::pair<size_t, vertex_t>> order_container;
    size_t min_count = std::numeric_limits<size_t>::max();
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
            else if (i->second < min_count || min_count == std::numeric_limits<size_t>::max())
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
    std::vector<vertex_t> forum_vids;
    std::vector<size_t> counts;
    for (auto &item : order_container)
    {
        forum_vids.emplace_back(item.second);
        counts.emplace_back(item.first);
    }
    auto [forum_schemas, forum_bufs] = engine.get_vertex_with_data_gbp<snb::ForumSchema::Forum>(forum_vids);
    for (auto i = 0; i < forum_schemas.size(); i++)
    {
        auto &forum_schema = forum_schemas[i];
        auto &forum_buf = forum_bufs[i];
        idx_by_count.emplace(std::make_pair(-counts[i], forum_schema.id),
                             forum_buf.GetString(0 + offsetof(snb::ForumSchema::Forum, data), forum_schema.titleLen()));
    }
    forum_bufs.clear();
    while (idx_by_count.size() > (size_t)request.limit)
        idx_by_count.erase(idx_by_count.rbegin()->first);

    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first.first;
        _return.back().forumTitle = p.second;
    }
}
#else
void InteractiveHandler::query5(std::vector<Query5Response> &_return, const Query5Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, int> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Forum_member);
            while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() > (uint64_t)request.minDate)
            {
                uint64_t posts = *(uint64_t *)(nbrs.edge_data().data() + sizeof(uint64_t));
                idx[nbrs.dst_id()] += posts;
                nbrs.next();
            }
        }
    }
    std::map<std::pair<int, size_t>, std::string> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first.first >= -i->second)
        {
            auto [forum_schema, forum_buf] = engine.get_vertex_with_data_gbp<snb::ForumSchema::Forum>(i->first);
            idx_by_count.emplace(
                std::make_pair(-i->second, forum_schema.id),
                forum_buf.GetString(0 + offsetof(snb::ForumSchema::Forum, data), forum_schema.titleLen()));
            while (idx_by_count.size() > (size_t)request.limit)
                idx_by_count.erase(idx_by_count.rbegin()->first);
        }
    }
    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first.first;
        _return.back().forumTitle = p.second;
    }
}
#endif
#else
void InteractiveHandler::query5(std::vector<Query5Response> &_return, const Query5Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::unordered_map<uint64_t, int> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Forum_member);
            while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() > (uint64_t)request.minDate)
            {
                uint64_t posts = *(uint64_t *)(nbrs.edge_data().data() + sizeof(uint64_t));
                idx[nbrs.dst_id()] += posts;
                nbrs.next();
            }
        }
    }
    std::map<std::pair<int, size_t>, std::string> idx_by_count;
    for (auto i = idx.begin(); i != idx.end(); i++)
    {
        if (idx_by_count.size() < (size_t)request.limit || idx_by_count.rbegin()->first.first >= -i->second)
        {
            auto forum = (snb::ForumSchema::Forum *)engine.get_vertex(i->first).data();
            idx_by_count.emplace(std::make_pair(-i->second, forum->id), std::string(forum->title(), forum->titleLen()));
            while (idx_by_count.size() > (size_t)request.limit)
                idx_by_count.erase(idx_by_count.rbegin()->first);
        }
    }
    for (auto p : idx_by_count)
    {
        _return.emplace_back();
        _return.back().postCount = -p.first.first;
        _return.back().forumTitle = p.second;
    }
}
#endif