#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED

void InteractiveHandler::shortQuery7(std::vector<ShortQuery7Response> &_return, const ShortQuery7Request &request)
{
    _return.clear();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto message_addr = engine.get_vertex_gbp(vid);
    if (message_addr == BlockManager::NULLPOINTER)
        return;
    auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
    auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

    auto person_vid = message_schema.creator;

    auto friends = multihop(engine, person_vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::vector<std::tuple<int64_t, uint64_t, gbp::BufferBlock, gbp::BufferBlock>> idx;

    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Message2Message_down);
    std::vector<vertex_t> message_vids;
    while (nbrs.valid())
    {
        message_vids.emplace_back(nbrs.dst_id());
        nbrs.next();
    }
    auto [message_schemas, message_bufs] = engine.get_vertex_with_data_gbp<snb::MessageSchema::Message>(message_vids);
    std::vector<vertex_t> person_vids;
    for (auto i = 0; i < message_vids.size(); i++)
    {
        person_vids.emplace_back(message_schemas[i].creator);
    }
    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(person_vids);
    for (auto i = 0; i < message_vids.size(); i++)
    {
        idx.emplace_back(-(int64_t)message_schemas[i].creationDate, person_schemas[i].id, message_bufs[i],
                         person_bufs[i]);
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        auto message_string = std::get<2>(p).GetString(0, std::get<2>(p).Size());
        auto message = (snb::MessageSchema::Message *)message_string.data();
        auto person_string = std::get<3>(p).GetString(0, std::get<3>(p).Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();

        _return.back().commentId = message->id;
        _return.back().commentCreationDate = message->creationDate;
        _return.back().commentContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
        _return.back().replyAuthorId = person->id;
        _return.back().replyAuthorFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().replyAuthorLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().replyAuthorKnowsOriginalMassageAuthor =
            (*std::lower_bound(friends.begin(), friends.end(), message->creator) == message->creator);
    }
}
#else
void InteractiveHandler::shortQuery7(std::vector<ShortQuery7Response> &_return, const ShortQuery7Request &request)
{
    _return.clear();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    // auto message = (snb::MessageSchema::Message *)engine.get_vertex(vid).data();
    // if (!message)
    //     return;
    auto message_addr = engine.get_vertex_gbp(vid);
    if (message_addr == BlockManager::NULLPOINTER)
        return;
    auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
    auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();

    auto person_vid = message_schema.creator;

    auto friends = multihop(engine, person_vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::vector<std::tuple<int64_t, uint64_t, gbp::BufferBlock, gbp::BufferBlock>> idx;

    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Message2Message_down);
    while (nbrs.valid())
    {
        auto message_addr = engine.get_vertex_gbp(nbrs.dst_id());
        auto message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
        auto message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
        auto message_buf =
            getBufferBlock(message_addr, message_schema.length + offsetof(snb::MessageSchema::Message, data));

        auto person_addr = engine.get_vertex_gbp(message_schema.creator);
        auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
        auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
        auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

        // auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
        // auto person = (snb::PersonSchema::Person *)engine.get_vertex(message_schema.creator).data();

        idx.emplace_back(-(int64_t)message_schema.creationDate, person_schema.id, message_buf, person_buf);
        nbrs.next();
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        // auto message = std::get<2>(p);
        // auto person = std::get<3>(p);

        auto message_string = std::get<2>(p).GetString(0, std::get<2>(p).Size());
        auto message = (snb::MessageSchema::Message *)message_string.data();
        auto person_string = std::get<3>(p).GetString(0, std::get<3>(p).Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();

        _return.back().commentId = message->id;
        _return.back().commentCreationDate = message->creationDate;
        _return.back().commentContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
        _return.back().replyAuthorId = person->id;
        _return.back().replyAuthorFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().replyAuthorLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().replyAuthorKnowsOriginalMassageAuthor =
            (*std::lower_bound(friends.begin(), friends.end(), message->creator) == message->creator);
    }
}
#endif
#else
void InteractiveHandler::shortQuery7(std::vector<ShortQuery7Response> &_return, const ShortQuery7Request &request)
{
    _return.clear();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto message = (snb::MessageSchema::Message *)engine.get_vertex(vid).data();
    if (!message)
        return;
    auto person_vid = message->creator;

    auto friends = multihop(engine, person_vid, 1, {(label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    std::vector<std::tuple<int64_t, uint64_t, snb::MessageSchema::Message *, snb::PersonSchema::Person *>> idx;

    auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Message2Message_down);
    while (nbrs.valid())
    {
        auto message = (snb::MessageSchema::Message *)engine.get_vertex(nbrs.dst_id()).data();
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(message->creator).data();
        idx.emplace_back(-(int64_t)message->creationDate, person->id, message, person);
        nbrs.next();
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        auto message = std::get<2>(p);
        auto person = std::get<3>(p);
        _return.back().commentId = message->id;
        _return.back().commentCreationDate = message->creationDate;
        _return.back().commentContent = message->contentLen()
                                            ? std::string(message->content(), message->contentLen())
                                            : std::string(message->imageFile(), message->imageFileLen());
        _return.back().replyAuthorId = person->id;
        _return.back().replyAuthorFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().replyAuthorLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().replyAuthorKnowsOriginalMassageAuthor =
            (*std::lower_bound(friends.begin(), friends.end(), message->creator) == message->creator);
    }
}
#endif