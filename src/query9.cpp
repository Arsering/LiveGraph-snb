#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::query9(std::vector<Query9Response> &_return, const Query9Request &request)
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

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Post_creator, false);
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Comment_creator, false);
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    size_t nbrs_cursor = 0;
    std::set<std::pair<size_t, vertex_t>> order_container;
    size_t min_count = std::numeric_limits<size_t>::max();
    size_t count_of_min_count = 0;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        {
            auto &nbrs = nbrses[nbrs_cursor++];
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    if (order_container.size() < (size_t)request.limit || (date >= min_count))
                    {
                        order_container.emplace(date, nbrs.dst_id());

                        if (date == min_count)
                        {
                            count_of_min_count++;
                        }
                        else if (date < min_count || min_count == std::numeric_limits<size_t>::max() - 1)
                        {
                            min_count = date;
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
                    else
                    {
                        flag = false;
                    }
                }
                nbrs.next();
            }
        }
        {
            auto &nbrs = nbrses[nbrs_cursor++];
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    if (order_container.size() < (size_t)request.limit || (date >= min_count))
                    {
                        order_container.emplace(date, nbrs.dst_id());

                        if (date == min_count)
                        {
                            count_of_min_count++;
                        }
                        else if (date < min_count || min_count == std::numeric_limits<size_t>::max() - 1)
                        {
                            min_count = date;
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
                    else
                    {
                        flag = false;
                    }
                }
                nbrs.next();
            }
        }
    }
    std::map<std::pair<int64_t, uint64_t>, std::pair<uintptr_t, size_t>> idx;
    {
        std::vector<vertex_t> message_vids;
        for (const auto &item : order_container)
        {
            message_vids.emplace_back(item.second);
        }
        auto [message_schemas, message_bufs] =
            engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
        size_t message_cursor = 0;
        for (const auto &item : order_container)
        {
            auto &message_schema = message_schemas[message_cursor];
            auto &message_buf = message_bufs[message_cursor];
            idx.emplace(
                std::make_pair(-item.first, message_schema.id),
                std::make_pair(message_buf, message_schema.length + offsetof(snb::MessageSchema::Message, data)));
            message_cursor++;
        }
        while (idx.size() > (size_t)request.limit)
            idx.erase(idx.rbegin()->first);
    }

    std::vector<gbp::batch_request_type> blk_infos;
    std::vector<gbp::BufferBlock> results;
    for (auto p : idx)
    {
        blk_infos.emplace_back(p.second.first, p.second.second, 0);
    }
    getBufferBlockBatch(blk_infos, results);
    size_t ret_cursor = 0;
    std::vector<vertex_t> person_vids;
    for (auto p : idx)
    {
        _return.emplace_back();
        auto message_string = results[ret_cursor++].GetString(0, p.second.second);
        auto message = (snb::MessageSchema::Message *)message_string.data();
        person_vids.emplace_back(message->creator);

        _return.back().messageId = message->id;
        _return.back().messageCreationDate = message->creationDate;
        _return.back().messageContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
    }

    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(person_vids);
    for (size_t i = 0; i < person_vids.size(); i++)
    {
        auto &person_schema = person_schemas[i];
        auto &person_buf = person_bufs[i];
        _return[i].personId = person_schema.id;
        _return[i].personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return[i].personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
    }
}
#else
void InteractiveHandler::query9(std::vector<Query9Response> &_return, const Query9Request &request)
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

    std::map<std::pair<int64_t, uint64_t>, gbp::BufferBlock> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    auto [message_schema, message_addr] =
                        engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());
                    auto key = std::make_pair(-date, message_schema.id);

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                    {
                        idx.emplace(key, getBufferBlock(message_addr, message_schema.length +
                                                                          offsetof(snb::MessageSchema::Message, data)));
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                    else
                    {
                        flag = idx.rbegin()->first.first > key.first;
                    }
                }
                nbrs.next();
            }
        }
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    auto [message_schema, message_addr] =
                        engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());
                    auto key = std::make_pair(-date, message_schema.id);

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                    {
                        idx.emplace(key, getBufferBlock(message_addr, message_schema.length +
                                                                          offsetof(snb::MessageSchema::Message, data)));
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                    else
                    {
                        flag = idx.rbegin()->first.first > key.first;
                    }
                }
                nbrs.next();
            }
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        auto message_string = p.second.GetString(0, p.second.Size());
        auto message = (snb::MessageSchema::Message *)message_string.data();

        auto [person_schema, person_buf] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(message->creator);

        _return.back().personId = person_schema.id;
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().messageId = message->id;
        ;
        _return.back().messageCreationDate = message->creationDate;
        _return.back().messageContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
    }
}
#endif
#else
void InteractiveHandler::query9(std::vector<Query9Response> &_return, const Query9Request &request)
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

    std::map<std::pair<int64_t, uint64_t>, snb::MessageSchema::Message *> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                    auto key = std::make_pair(-date, message->id);
                    auto value = message;

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                    {
                        idx.emplace(key, value);
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                    else
                    {
                        flag = idx.rbegin()->first.first > key.first;
                    }
                }
                nbrs.next();
            }
        }
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date < request.maxDate)
                {
                    auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                    auto key = std::make_pair(-date, message->id);
                    auto value = message;

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                    {
                        idx.emplace(key, value);
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                    else
                    {
                        flag = idx.rbegin()->first.first > key.first;
                    }
                }
                nbrs.next();
            }
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        auto message = p.second;
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(message->creator).data();
        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().messageId = message->id;
        ;
        _return.back().messageCreationDate = message->creationDate;
        _return.back().messageContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
    }
}
#endif