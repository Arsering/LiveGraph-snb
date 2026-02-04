#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::shortQuery3(std::vector<ShortQuery3Response> &_return, const ShortQuery3Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();

    auto person_addr = engine.get_vertex_gbp(vid);
    if (person_addr == BlockManager::NULLPOINTER)
        return;

    std::vector<std::tuple<int64_t, uint64_t, gbp::BufferBlock>> idx;

    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Person);
    std::vector<vertex_t> person_vids;
    while (nbrs.valid())
    {
        int64_t date = *(uint64_t *)nbrs.edge_data().data();
        person_vids.emplace_back(nbrs.dst_id());
        idx.emplace_back(-date, 0, gbp::BufferBlock());
        nbrs.next();
    }
    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(person_vids);
    for (auto i = 0; i < person_vids.size(); i++)
    {
        std::get<1>(idx[i]) = person_schemas[i].id;
        std::get<2>(idx[i]) = person_bufs[i];
    }

    std::sort(idx.begin(), idx.end());
    for (auto p : idx)
    {
        _return.emplace_back();
        auto &person_buf = std::get<2>(p);
        auto person_schema = person_buf.GetInnerObj<snb::PersonSchema::Person>();
        _return.back().personId = person_schema.id;
        _return.back().firstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().lastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().friendshipCreationDate = -std::get<0>(p);
    }
}
#else
void InteractiveHandler::shortQuery3(std::vector<ShortQuery3Response> &_return, const ShortQuery3Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto person_addr = engine.get_vertex_gbp(vid);
    if (person_addr == BlockManager::NULLPOINTER)
        return;

    std::vector<std::tuple<int64_t, uint64_t, gbp::BufferBlock>> idx;

    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Person);
    while (nbrs.valid())
    {
        int64_t date = *(uint64_t *)nbrs.edge_data().data();
        auto person_addr = engine.get_vertex_gbp(nbrs.dst_id());
        auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
        auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
        auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

        idx.emplace_back(-date, person_schema.id, person_buf);
        nbrs.next();
    }
    std::sort(idx.begin(), idx.end());
    for (auto p : idx)
    {
        _return.emplace_back();

        auto &person_buf = std::get<2>(p);
        auto person_schema = person_buf.GetInnerObj<snb::PersonSchema::Person>();
        _return.back().personId = person_schema.id;
        _return.back().firstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().lastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().friendshipCreationDate = -std::get<0>(p);
    }
}
#endif
#else
void InteractiveHandler::shortQuery3(std::vector<ShortQuery3Response> &_return, const ShortQuery3Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();
    if (!person)
        return;

    std::vector<std::tuple<int64_t, uint64_t, snb::PersonSchema::Person *>> idx;

    auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Person);
    while (nbrs.valid())
    {
        int64_t date = *(uint64_t *)nbrs.edge_data().data();
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(nbrs.dst_id()).data();
        idx.emplace_back(-date, person->id, person);
        nbrs.next();
    }
    std::sort(idx.begin(), idx.end());
    for (auto p : idx)
    {
        _return.emplace_back();
        auto person = std::get<2>(p);
        _return.back().personId = person->id;
        _return.back().firstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().lastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().friendshipCreationDate = -std::get<0>(p);
    }
}
#endif