#include "manual.hpp"
#include <tuple>
#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::query3(std::vector<Query3Response> &_return, const Query3Request &request)
{

    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    uint64_t countryX = placeSchema.findName(request.countryXName);
    uint64_t countryY = placeSchema.findName(request.countryYName);
    if (vid == (uint64_t)-1)
        return;
    if (countryX == (uint64_t)-1)
        return;
    if (countryY == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::vector<std::tuple<int, uint64_t, int, std::pair<uintptr_t, size_t>>> idx;
    auto [person_schemas, person_addrs] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(friends);
    std::vector<vertex_t> place_vids;
    for (size_t i = 0; i < friends.size(); i++)
    {
        place_vids.emplace_back(person_schemas[i].place);
    }
    auto [place_schemas, _] = engine.get_vertex_without_data_gbp<snb::PlaceSchema::Place>(place_vids);
    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < friends.size(); i++)
    {
        auto &place_schema = place_schemas[i];
        if (place_schema.isPartOf != countryX && place_schema.isPartOf != countryY)
        {
            edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Comment_creator, false);
            edgelist_infos.emplace_back(friends[i], (label_t)snb::EdgeSchema::Person2Post_creator, false);
        }
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    size_t nbrses_cursor = 0;
    std::vector<vertex_t> message_vids;
    std::vector<size_t> intervals;
    intervals.emplace_back(0);
    for (size_t i = 0; i < friends.size(); i++)
    {
        auto &place_schema = place_schemas[i];
        if (place_schema.isPartOf != countryX && place_schema.isPartOf != countryY)
        {
            int xCount = 0, yCount = 0;
            {
                auto &nbrs = nbrses[nbrses_cursor++];
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        message_vids.emplace_back(nbrs.dst_id());
                    }
                    nbrs.next();
                }
            }
            {
                auto &nbrs = nbrses[nbrses_cursor++];
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        message_vids.emplace_back(nbrs.dst_id());
                    }
                    nbrs.next();
                }
            }
            intervals.emplace_back(message_vids.size());
        }
    }
    auto [message_schemas, aa] = engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(message_vids);
    size_t intervals_cursor = 0;
    for (size_t i = 0; i < friends.size(); i++)
    {
        auto &person_schema = person_schemas[i];
        auto &place_schema = place_schemas[i];
        if (place_schema.isPartOf != countryX && place_schema.isPartOf != countryY)
        {
            int xCount = 0, yCount = 0;
            for (auto j = intervals[intervals_cursor]; j < intervals[intervals_cursor + 1]; j++)
            {
                auto &message_schema = message_schemas[j];

                if (message_schema.place == countryX)
                    xCount++;
                if (message_schema.place == countryY)
                    yCount++;
            }
            intervals_cursor++;
            if (xCount > 0 && yCount > 0)
                idx.emplace_back(
                    -xCount, person_schema.id, -yCount,
                    std::make_pair(person_addrs[i], person_schema.length + offsetof(snb::PersonSchema::Person, data)));
        }
    }
    person_schemas.clear();
    place_schemas.clear();
    message_schemas.clear();
    nbrses.clear();

    std::sort(idx.begin(), idx.end());
    std::vector<gbp::batch_request_type> blk_infos;
    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        blk_infos.emplace_back(std::get<3>(idx[i]).first, std::get<3>(idx[i]).second, 0);
    }
    std::vector<gbp::BufferBlock> buffer_blocks;
    getBufferBlockBatch(blk_infos, buffer_blocks);
    size_t buffer_block_cursor = 0;
    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto &person_buf = buffer_blocks[buffer_block_cursor++];
        auto person_schema = person_buf.GetInnerObj<snb::PersonSchema::Person>();
        _return.back().personId = std::get<1>(idx[i]);
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().xCount = -std::get<0>(idx[i]);
        _return.back().yCount = -std::get<2>(idx[i]);
        _return.back().count = _return.back().xCount + _return.back().yCount;
    }
}

#else
void InteractiveHandler::query3(std::vector<Query3Response> &_return, const Query3Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    uint64_t countryX = placeSchema.findName(request.countryXName);
    uint64_t countryY = placeSchema.findName(request.countryYName);
    if (vid == (uint64_t)-1)
        return;
    if (countryX == (uint64_t)-1)
        return;
    if (countryY == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::vector<std::tuple<int, uint64_t, int, gbp::BufferBlock>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        auto person_addr = engine.get_vertex_gbp(friends[i]);
        auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
        auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();

        auto [place_schema, _] = engine.get_vertex_without_data_gbp<snb::PlaceSchema::Place>(person_schema.place);

        if (place_schema.isPartOf != countryX && place_schema.isPartOf != countryY)
        {
            uint64_t vid = friends[i];
            int xCount = 0, yCount = 0;
            {
                auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
                        auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
                        auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

                        if (message_schema.place == countryX)
                            xCount++;
                        if (message_schema.place == countryY)
                            yCount++;
                    }
                    nbrs.next();
                }
            }
            {
                auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
                        auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
                        auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
                        if (message_schema.place == countryX)
                            xCount++;
                        if (message_schema.place == countryY)
                            yCount++;
                    }
                    nbrs.next();
                }
            }

            if (xCount > 0 && yCount > 0)
                idx.push_back(std::make_tuple(
                    -xCount, person_schema.id, -yCount,
                    getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data))));
        }
    }
    std::sort(idx.begin(), idx.end());

    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto person_string = std::get<3>(idx[i]).GetString(0, std::get<3>(idx[i]).Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();

        _return.back().personId = std::get<1>(idx[i]);
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().xCount = -std::get<0>(idx[i]);
        _return.back().yCount = -std::get<2>(idx[i]);
        _return.back().count = _return.back().xCount + _return.back().yCount;
    }
}
#endif
#else
void InteractiveHandler::query3(std::vector<Query3Response> &_return, const Query3Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    uint64_t countryX = placeSchema.findName(request.countryXName);
    uint64_t countryY = placeSchema.findName(request.countryYName);
    if (vid == (uint64_t)-1)
        return;
    if (countryX == (uint64_t)-1)
        return;
    if (countryY == (uint64_t)-1)
        return;
    uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    std::vector<std::tuple<int, uint64_t, int, snb::PersonSchema::Person *>> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(friends[i]).data();
        auto place = (snb::PlaceSchema::Place *)engine.get_vertex(person->place).data();
        if (place->isPartOf != countryX && place->isPartOf != countryY)
        {
            uint64_t vid = friends[i];
            int xCount = 0, yCount = 0;
            {
                auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                        if (message->place == countryX)
                            xCount++;
                        if (message->place == countryY)
                            yCount++;
                    }
                    nbrs.next();
                }
            }
            {
                auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
                while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
                {
                    uint64_t date = *(uint64_t *)nbrs.edge_data().data();
                    if (date < endDate)
                    {
                        auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
                        if (message->place == countryX)
                            xCount++;
                        if (message->place == countryY)
                            yCount++;
                    }
                    nbrs.next();
                }
            }

            if (xCount > 0 && yCount > 0)
                idx.push_back(std::make_tuple(-xCount, person->id, -yCount, person));
        }
    }
    std::sort(idx.begin(), idx.end());

    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto person = std::get<3>(idx[i]);
        _return.back().personId = std::get<1>(idx[i]);
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().xCount = -std::get<0>(idx[i]);
        _return.back().yCount = -std::get<2>(idx[i]);
        _return.back().count = _return.back().xCount + _return.back().yCount;
    }
}
#endif