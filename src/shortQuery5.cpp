#include "manual.hpp"

#if GOCACHE_ENABLED

void InteractiveHandler::shortQuery5(ShortQuery5Response &_return, const ShortQuery5Request &request)
{
    _return = ShortQuery5Response();
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

    // auto person = (snb::PersonSchema::Person *)engine.get_vertex(message_schema.creator).data();
    auto person_addr = engine.get_vertex_gbp(message_schema.creator);
    auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
    auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
    auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

    _return.personId = person_schema.id;
    _return.firstName =
        person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
    _return.lastName = person_buf.GetString(person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data),
                                            person_schema.lastNameLen());
}
#else
void InteractiveHandler::shortQuery5(ShortQuery5Response &_return, const ShortQuery5Request &request)
{
    _return = ShortQuery5Response();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto message = (snb::MessageSchema::Message *)engine.get_vertex(vid).data();
    if (!message)
        return;
    auto person = (snb::PersonSchema::Person *)engine.get_vertex(message->creator).data();
    _return.personId = person->id;
    _return.firstName = std::string(person->firstName(), person->firstNameLen());
    _return.lastName = std::string(person->lastName(), person->lastNameLen());
}
#endif