#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED
// TODO: 需要进一步的优化

void InteractiveHandler::query12(std::vector<Query12Response> &_return, const Query12Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagclassId = tagclassSchema.findName(request.tagClassName);
    if (tagclassId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    auto tags = multihop_another_etype(engine, tagclassId, 65536, (label_t)snb::EdgeSchema::TagClass2TagClass_down,
                                       (label_t)snb::EdgeSchema::TagClass2Tag);
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, std::pair<uint64_t, std::vector<uint64_t>>> idx;
    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Comment_creator, false);
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    std::vector<vertex_t> message_vids;
    std::vector<size_t> intervals;
    intervals.emplace_back(0);
    for (size_t i = 0; i < friends.size(); i++)
    {
        auto &nbrs = nbrses[i];
        while (nbrs.valid())
        {
            message_vids.emplace_back(nbrs.dst_id());
            nbrs.next();
        }
        intervals.emplace_back(message_vids.size());
    }
    nbrses.clear();
    auto [message_schemas, _] = engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
    edgelist_infos.clear();
    for (size_t i = 0; i < message_schemas.size(); i++)
    {
        uint64_t vid = message_schemas[i].replyOfPost;
        if (vid != (uint64_t)-1)
        {
            edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Post2Tag, false);
        }
    }
    nbrses = engine.get_edges_gbp(edgelist_infos);
    size_t nbrs_cursor = 0;
    size_t interval_cursor = 0;
    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        int count = 0;
        std::set<uint64_t> tagSet;
        for (auto j = intervals[interval_cursor]; j < intervals[interval_cursor + 1]; j++)
        {
            bool flag = false;
            auto &message_schema = message_schemas[j];
            uint64_t vid = message_schema.replyOfPost;
            if (vid != (uint64_t)-1)
            {
                auto &nbrs = nbrses[nbrs_cursor++];
                while (nbrs.valid())
                {
                    uint64_t tag = nbrs.dst_id();
                    if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                    {
                        flag = true;
                        tagSet.emplace(tag);
                    }
                    nbrs.next();
                }
            }
            if (flag)
                count++;
        }
        interval_cursor++;
        if (count)
        {
            auto [person_schema, _] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(vid);

            uint64_t person_id = person_schema.id;
            auto key = std::make_pair(-count, person_id);
            std::vector<uint64_t> tagV;
            for (auto p : tagSet)
                tagV.push_back(p);
            auto value = std::make_pair(vid, tagV);

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, value);
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        // auto person = (snb::PersonSchema::Person *)engine.get_vertex(p.second.first).data();
        auto [person_schema, person_buf] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(p.second.first);

        _return.back().personId = person_schema.id;
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());

        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        for (auto vid : p.second.second)
        {
            auto [tag_schema, tag_buf] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(vid);

            _return.back().tagNames.emplace_back(
                tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen()));
        }
        std::sort(_return.back().tagNames.begin(), _return.back().tagNames.end());
        _return.back().replyCount = -p.first.first;
    }
}
#else
void InteractiveHandler::query12(std::vector<Query12Response> &_return, const Query12Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagclassId = tagclassSchema.findName(request.tagClassName);
    if (tagclassId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    auto tags = multihop_another_etype(engine, tagclassId, 65536, (label_t)snb::EdgeSchema::TagClass2TagClass_down,
                                       (label_t)snb::EdgeSchema::TagClass2Tag);
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, std::pair<uint64_t, std::vector<uint64_t>>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        int count = 0;
        std::set<uint64_t> tagSet;
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
        while (nbrs.valid())
        {
            bool flag = false;
            // auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
            auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
            auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
            auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

            uint64_t vid = message_schema.replyOfPost;
            if (vid != (uint64_t)-1)
            {
                auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Post2Tag);
                while (nbrs.valid())
                {
                    uint64_t tag = nbrs.dst_id();
                    if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                    {
                        flag = true;
                        tagSet.emplace(tag);
                    }
                    nbrs.next();
                }
            }
            if (flag)
                count++;
            nbrs.next();
        }
        if (count)
        {
            // auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();
            auto [person_schema, _] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(vid);

            uint64_t person_id = person_schema.id;
            auto key = std::make_pair(-count, person_id);
            std::vector<uint64_t> tagV;
            for (auto p : tagSet)
                tagV.push_back(p);
            auto value = std::make_pair(vid, tagV);

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, value);
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        // auto person = (snb::PersonSchema::Person *)engine.get_vertex(p.second.first).data();
        auto [person_schema, person_buf] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(p.second.first);

        _return.back().personId = person_schema.id;
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());

        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        for (auto vid : p.second.second)
        {
            auto [tag_schema, tag_buf] = engine.get_vertex_with_data_gbp<snb::TagSchema::Tag>(vid);

            _return.back().tagNames.emplace_back(
                tag_buf.GetString(0 + offsetof(snb::TagSchema::Tag, data), tag_schema.nameLen()));
        }
        std::sort(_return.back().tagNames.begin(), _return.back().tagNames.end());
        _return.back().replyCount = -p.first.first;
    }
}
#endif
#else
void InteractiveHandler::query12(std::vector<Query12Response> &_return, const Query12Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t tagclassId = tagclassSchema.findName(request.tagClassName);
    if (tagclassId == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    auto tags = multihop_another_etype(engine, tagclassId, 65536, (label_t)snb::EdgeSchema::TagClass2TagClass_down,
                                       (label_t)snb::EdgeSchema::TagClass2Tag);
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, std::pair<uint64_t, std::vector<uint64_t>>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        int count = 0;
        std::set<uint64_t> tagSet;
        auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
        while (nbrs.valid())
        {
            bool flag = false;
            auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
            uint64_t vid = message->replyOfPost;
            if (vid != (uint64_t)-1)
            {
                auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Post2Tag);
                while (nbrs.valid())
                {
                    uint64_t tag = nbrs.dst_id();
                    if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                    {
                        flag = true;
                        tagSet.emplace(tag);
                    }
                    nbrs.next();
                }
            }
            if (flag)
                count++;
            nbrs.next();
        }
        if (count)
        {
            auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();
            uint64_t person_id = person->id;
            auto key = std::make_pair(-count, person_id);
            std::vector<uint64_t> tagV;
            for (auto p : tagSet)
                tagV.push_back(p);
            auto value = std::make_pair(vid, tagV);

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, value);
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(p.second.first).data();
        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        for (auto vid : p.second.second)
        {
            auto tag = (snb::TagSchema::Tag *)engine.get_vertex(vid).data();
            _return.back().tagNames.emplace_back(tag->name(), tag->nameLen());
        }
        std::sort(_return.back().tagNames.begin(), _return.back().tagNames.end());
        _return.back().replyCount = -p.first.first;
    }
}
#endif