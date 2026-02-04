#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::shortQuery2(std::vector<ShortQuery2Response> &_return, const ShortQuery2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto person_addr = engine.get_vertex_gbp(vid);
    if (person_addr == BlockManager::NULLPOINTER)
        return;

    std::map<std::pair<int64_t, int64_t>, std::pair<snb::MessageSchema::Message, uintptr_t>> idx;
    std::set<std::pair<size_t, vertex_t>> order_container;
    size_t min_count = std::numeric_limits<size_t>::max();
    size_t count_of_min_count = 0;
    {
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
        bool flag = true;
        while (nbrs.valid() && flag)
        {
            int64_t date = *(uint64_t *)nbrs.edge_data().data();
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

            nbrs.next();
        }
    }
    {
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
        bool flag = true;
        while (nbrs.valid() && flag)
        {
            int64_t date = *(uint64_t *)nbrs.edge_data().data();
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
            nbrs.next();
        }
    }
    {
        idx.clear();
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
            idx.emplace(std::make_pair(-item.first, -message_schema.id), std::make_pair(message_schema, message_buf));
            message_cursor++;
        }
        while (idx.size() > (size_t)request.limit)
            idx.erase(idx.rbegin()->first);
    }
    std::vector<gbp::batch_request_type> blk_infos;
    std::vector<gbp::BufferBlock> results;
    for (auto p : idx)
    {
        blk_infos.emplace_back(p.second.second, p.second.first.length + offsetof(snb::MessageSchema::Message, data), 0);
    }
    getBufferBlockBatch(blk_infos, results);
    size_t ret_cursor = 0;
    for (auto p : idx)
    {
        _return.emplace_back();
        auto &message_schema = p.second.first;
        auto &message_buf = results[ret_cursor++];
        _return.back().messageId = message_schema.id;
        _return.back().messageCreationDate = message_schema.creationDate;
        _return.back().messageContent =
            message_schema.contentLen()
                ? message_buf.GetString(message_schema.content_offset + offsetof(snb::MessageSchema::Message, data),
                                        message_schema.contentLen())
                : message_buf.GetString(0 + offsetof(snb::MessageSchema::Message, data), message_schema.imageFileLen());
    }

    {
        std::vector<vertex_t> message_vids;
        do
        {
            message_vids.clear();
            for (auto p : idx)
            {
                auto &message_schema = p.second.first;
                if (message_schema.replyOfComment != (uint64_t)-1)
                {
                    message_vids.emplace_back(message_schema.replyOfComment);
                }
            }
            if (message_vids.size())
            {
                auto [message_schemas, _] =
                    engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
                ret_cursor = 0;
                for (auto &p : idx)
                {
                    if (p.second.first.replyOfComment != (uint64_t)-1)
                    {
                        p.second.first = message_schemas[ret_cursor];
                        ret_cursor++;
                    }
                }
            }
        } while (message_vids.size());
    }

    {
        std::vector<vertex_t> message_vids;
        for (auto p : idx)
        {
            auto &message_schema = p.second.first;
            if (message_schema.replyOfPost != (uint64_t)-1)

            {
                message_vids.emplace_back(message_schema.replyOfPost);
            }
        }
        if (message_vids.size())
        {
            auto [message_schemas, message_addrs] =
                engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
            ret_cursor = 0;
            for (auto &p : idx)
            {
                if (p.second.first.replyOfPost != (uint64_t)-1)
                {
                    p.second.first = message_schemas[ret_cursor++];
                }
            }
        }
    }
    std::vector<vertex_t> person_vids;
    for (auto p : idx)
    {
        person_vids.emplace_back(p.second.first.creator);
    }
    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(person_vids);
    ret_cursor = 0;
    for (auto p : idx)
    {
        auto &message_schema = p.second.first;
        auto &person_schema = person_schemas[ret_cursor];
        auto &person_buf = person_bufs[ret_cursor];

        _return[ret_cursor].originalPostId = message_schema.id;
        _return[ret_cursor].originalPostAuthorId = person_schema.id;
        _return[ret_cursor].originalPostAuthorFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return[ret_cursor].originalPostAuthorLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        ret_cursor++;
    }
}
#else
void InteractiveHandler::shortQuery2(std::vector<ShortQuery2Response> &_return, const ShortQuery2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    // auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();

    auto person_addr = engine.get_vertex_gbp(vid);
    // if (!person)
    //     return;
    if (person_addr == BlockManager::NULLPOINTER)
        return;

    std::map<std::pair<int64_t, int64_t>, gbp::BufferBlock> idx;
    {
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
        bool flag = true;
        while (nbrs.valid() && flag)
        {
            int64_t date = *(uint64_t *)nbrs.edge_data().data();
            // auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();

            auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
            auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
            auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

            auto key = std::make_pair(-date, -(int64_t)message_schema.id);
            // auto value = message;
            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, getBufferBlock(message_addr,
                                                message_schema.length + offsetof(snb::MessageSchema::Message, data)));
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
            else
            {
                flag = idx.rbegin()->first.first > key.first;
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
            // auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();

            auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
            auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
            auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

            auto key = std::make_pair(-date, -(int64_t)message_schema.id);
            // auto value = message;
            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, getBufferBlock(message_addr,
                                                message_schema.length + offsetof(snb::MessageSchema::Message, data)));
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
            else
            {
                flag = idx.rbegin()->first.first > key.first;
            }
            nbrs.next();
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        // auto message = p.second;
        auto message_string = p.second.GetString(0, p.second.Size());
        auto message = (snb::MessageSchema::Message *)message_string.data();

        _return.back().messageId = message->id;
        _return.back().messageCreationDate = message->creationDate;
        _return.back().messageContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
        // for (; message->replyOfComment != (uint64_t)-1;
        //      message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfComment).data())
        //     ;
        while (message->replyOfComment != (uint64_t)-1)
        {
            auto message_addr = engine.get_vertex_gbp(message->replyOfComment);
            auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
            auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
            p.second =
                getBufferBlock(message_addr, message_schema.length + offsetof(snb::MessageSchema::Message, data));
            message_string = p.second.GetString(0, p.second.Size());
            message = (snb::MessageSchema::Message *)message_string.data();
        }

        if (message->replyOfPost != (uint64_t)-1)
        // message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfPost).data();
        {
            auto message_addr = engine.get_vertex_gbp(message->replyOfPost);
            auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
            auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
            p.second =
                getBufferBlock(message_addr, message_schema.length + offsetof(snb::MessageSchema::Message, data));
            message_string = p.second.GetString(0, p.second.Size());
            message = (snb::MessageSchema::Message *)message_string.data();
        }
        // auto person = (snb::PersonSchema::Person *)engine.get_vertex(message->creator).data();
        auto person_addr = engine.get_vertex_gbp(message->creator);
        auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
        auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
        auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

        _return.back().originalPostId = message->id;
        _return.back().originalPostAuthorId = person_schema.id;
        _return.back().originalPostAuthorFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().originalPostAuthorLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
    }
}
#endif
#else
void InteractiveHandler::shortQuery2(std::vector<ShortQuery2Response> &_return, const ShortQuery2Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();
    if (!person)
        return;

    std::map<std::pair<int64_t, int64_t>, snb::MessageSchema::Message *> idx;
    {
        auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
        bool flag = true;
        while (nbrs.valid() && flag)
        {
            int64_t date = *(uint64_t *)nbrs.edge_data().data();
            auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
            auto key = std::make_pair(-date, -(int64_t)message->id);
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
            nbrs.next();
        }
    }
    {
        auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
        bool flag = true;
        while (nbrs.valid() && flag)
        {
            int64_t date = *(uint64_t *)nbrs.edge_data().data();
            auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
            auto key = std::make_pair(-date, -(int64_t)message->id);
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
            nbrs.next();
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        auto message = p.second;
        _return.back().messageId = message->id;
        _return.back().messageCreationDate = message->creationDate;
        _return.back().messageContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
        for (; message->replyOfComment != (uint64_t)-1;
             message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfComment).data())
            ;
        if (message->replyOfPost != (uint64_t)-1)
            message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfPost).data();
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(message->creator).data();
        _return.back().originalPostId = message->id;
        _return.back().originalPostAuthorId = person->id;
        _return.back().originalPostAuthorFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().originalPostAuthorLastName = std::string(person->lastName(), person->lastNameLen());
    }
}
#endif