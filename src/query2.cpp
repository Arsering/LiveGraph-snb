#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED
void InteractiveHandler::query2(std::vector<Query2Response> &_return, const Query2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::map<std::pair<int64_t, uint64_t>, Query2Response> idx;

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Post_creator, false);
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Comment_creator, false);
    }
    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(friends);
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    size_t nbrses_cursor = 0;
    std::set<std::pair<size_t, uint64_t>> order_container;
    size_t min_count = 0;
    size_t count_of_min_count = 0;
    std::unordered_map<uint64_t, Query2Response> response_container;

    for (size_t i = 0; i < friends.size(); i++)
    {
        auto &person_schema = person_schemas[i];
        auto &person_buf = person_bufs[i];
        auto personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        auto personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        uint64_t vid = friends[i];
        {
            auto &nbrs = nbrses[nbrses_cursor++];
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date <= request.maxDate)
                {
                    {
                        if (order_container.size() < (size_t)request.limit || (date >= min_count))
                        {
                            auto value = Query2Response();
                            value.personId = person_schema.id;
                            value.personFirstName = personFirstName;
                            value.personLastName = personLastName;
                            order_container.emplace(date, nbrs.dst_id());
                            response_container.emplace(nbrs.dst_id(), value);

                            if (date == min_count)
                            {
                                count_of_min_count++;
                            }
                            else if (date < min_count || min_count == 0)
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
                                        response_container.erase(order_container.begin()->second);
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
                }
                nbrs.next();
            }
        }
        {
            auto &nbrs = nbrses[nbrses_cursor++];
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date <= request.maxDate)
                {

                    {
                        if (order_container.size() < (size_t)request.limit || (date >= min_count))
                        {
                            auto value = Query2Response();
                            value.personId = person_schema.id;
                            value.personFirstName = personFirstName;
                            value.personLastName = personLastName;

                            order_container.emplace(date, nbrs.dst_id());
                            response_container.emplace(nbrs.dst_id(), value);

                            if (date == min_count)
                            {
                                count_of_min_count++;
                            }
                            else if (date < min_count || min_count == 0)
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
                                        response_container.erase(order_container.begin()->second);
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
                }
                nbrs.next();
            }
        }
    }
    person_schemas.clear();
    person_bufs.clear();
    nbrses.clear();

    {
        idx.clear();
        std::vector<vertex_t> message_vids;

        for (const auto &item : order_container)
        {
            message_vids.emplace_back(item.second);
        }
        auto [message_schemas, message_bufs] =
            engine.get_vertex_with_data_gbp<snb::MessageSchema::Message>(message_vids);
        size_t message_cursor = 0;
        for (const auto &item : order_container)
        {
            auto &message_schema = message_schemas[message_cursor];
            auto &message_buf = message_bufs[message_cursor];
            auto value = response_container.find(item.second);
            value->second.messageCreationDate = message_schema.creationDate;
            value->second.messageId = message_schema.id;
            value->second.messageContent =
                message_schema.contentLen()
                    ? message_buf.GetString(message_schema.content_offset + offsetof(snb::MessageSchema::Message, data),
                                            message_schema.contentLen())
                    : message_buf.GetString(0 + offsetof(snb::MessageSchema::Message, data),
                                            message_schema.imageFileLen());
            idx.emplace(std::make_pair(-item.first, message_schema.id), value->second);
            message_cursor++;
        }
        while (idx.size() > (size_t)request.limit)
            idx.erase(idx.rbegin()->first);
    }
    for (auto p : idx)
        _return.emplace_back(p.second);
}

#else
void InteractiveHandler::query2(std::vector<Query2Response> &_return, const Query2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::map<std::pair<int64_t, uint64_t>, Query2Response> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        auto [person_schema, person_buf] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(friends[i]);

        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date <= request.maxDate)
                {
                    auto [message_schema, message_buf] =
                        engine.get_vertex_with_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());

                    auto key = std::make_pair(-date, message_schema.id);
                    auto value = Query2Response();
                    value.personId = person_schema.id;
                    value.messageCreationDate = message_schema.creationDate;
                    value.messageId = message_schema.id;

                    {
                        if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                        {
                            value.personFirstName = person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data),
                                                                         person_schema.firstNameLen());
                            value.personLastName = person_buf.GetString(person_schema.lastName_offset +
                                                                            offsetof(snb::PersonSchema::Person, data),
                                                                        person_schema.lastNameLen());
                            value.messageContent =
                                message_schema.contentLen()
                                    ? message_buf.GetString(message_schema.content_offset +
                                                                offsetof(snb::MessageSchema::Message, data),
                                                            message_schema.contentLen())
                                    : message_buf.GetString(0 + offsetof(snb::MessageSchema::Message, data),
                                                            message_schema.imageFileLen());
                            idx.emplace(key, value);
                            while (idx.size() > (size_t)request.limit)
                                idx.erase(idx.rbegin()->first);
                        }
                        else
                        {
                            flag = false;
                        }
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
                if (date <= request.maxDate)
                {
                    auto [message_schema, message_buf] =
                        engine.get_vertex_with_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());

                    auto key = std::make_pair(-date, message_schema.id);
                    auto value = Query2Response();
                    value.personId = person_schema.id;
                    value.messageCreationDate = message_schema.creationDate;
                    value.messageId = message_schema.id;

                    {
                        if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                        {
                            value.personFirstName = person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data),
                                                                         person_schema.firstNameLen());
                            value.personLastName = person_buf.GetString(person_schema.lastName_offset +
                                                                            offsetof(snb::PersonSchema::Person, data),
                                                                        person_schema.lastNameLen());

                            value.messageContent =
                                message_schema.contentLen()
                                    ? message_buf.GetString(message_schema.content_offset +
                                                                offsetof(snb::MessageSchema::Message, data),
                                                            message_schema.contentLen())
                                    : message_buf.GetString(0 + offsetof(snb::MessageSchema::Message, data),
                                                            message_schema.imageFileLen());
                            idx.emplace(key, value);
                            while (idx.size() > (size_t)request.limit)
                                idx.erase(idx.rbegin()->first);
                        }
                        else
                        {
                            flag = false;
                        }
                    }
                }
                nbrs.next();
            }
        }
    }
    for (auto p : idx)
        _return.emplace_back(p.second);
}
#endif
#else

void InteractiveHandler::query2(std::vector<Query2Response> &_return, const Query2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    std::map<std::pair<int64_t, uint64_t>, Query2Response> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(friends[i]).data();
        uint64_t vid = friends[i];
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            bool flag = true;
            while (nbrs.valid() && flag)
            {
                int64_t date = *(uint64_t *)nbrs.edge_data().data();
                if (date <= request.maxDate)
                {
                    auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                    auto key = std::make_pair(-date, message->id);
                    auto value = Query2Response();
                    value.personId = person->id;
                    value.messageCreationDate = message->creationDate;
                    value.messageId = message->id;

                    {
                        if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                        {
                            value.personFirstName = std::string(person->firstName(), person->firstNameLen());
                            value.personLastName = std::string(person->lastName(), person->lastNameLen());
                            value.messageContent = message->contentLen()
                                                       ? std::string(message->content(), message->contentLen())
                                                       : std::string(message->imageFile(), message->imageFileLen());
                            idx.emplace(key, value);
                            while (idx.size() > (size_t)request.limit)
                                idx.erase(idx.rbegin()->first);
                        }
                        else
                        {
                            flag = false;
                        }
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
                if (date <= request.maxDate)
                {
                    auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                    auto key = std::make_pair(-date, message->id);
                    auto value = Query2Response();
                    value.personId = person->id;
                    value.messageCreationDate = message->creationDate;
                    value.messageId = message->id;

                    {
                        if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
                        {
                            value.personFirstName = std::string(person->firstName(), person->firstNameLen());
                            value.personLastName = std::string(person->lastName(), person->lastNameLen());
                            value.messageContent = message->contentLen()
                                                       ? std::string(message->content(), message->contentLen())
                                                       : std::string(message->imageFile(), message->imageFileLen());
                            idx.emplace(key, value);
                            while (idx.size() > (size_t)request.limit)
                                idx.erase(idx.rbegin()->first);
                        }
                        else
                        {
                            flag = false;
                        }
                    }
                }
                nbrs.next();
            }
        }
    }
    for (auto p : idx)
        _return.emplace_back(p.second);
}
#endif
