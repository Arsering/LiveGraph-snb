#include "manual.hpp"

#if GOCACHE_ENABLED

void InteractiveHandler::shortQuery6(ShortQuery6Response &_return, const ShortQuery6Request &request)
{
    _return = ShortQuery6Response();
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

    while (message_schema.replyOfComment != (uint64_t)-1)
    {
        message_addr = engine.get_vertex_gbp(message_schema.replyOfComment);
        message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
        message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
    }

    if (message_schema.replyOfPost != (uint64_t)-1)
    {
        message_addr = engine.get_vertex_gbp(message_schema.replyOfPost);
        message_schema_buf = getBufferBlock(message_addr, sizeof(snb::MessageSchema::Message));
        message_schema = message_schema_buf.GetInnerObj<snb::MessageSchema::Message>();
    }

    auto forum_addr = engine.get_vertex_gbp(message_schema.forumid);
    auto forum_schema_buf = getBufferBlock(forum_addr, sizeof(snb::ForumSchema::Forum));
    auto forum_schema = forum_schema_buf.GetInnerObj<snb::ForumSchema::Forum>();
    auto forum_buf = getBufferBlock(forum_addr, forum_schema.length + offsetof(snb::ForumSchema::Forum, data));

    auto person_addr = engine.get_vertex_gbp(forum_schema.moderator);
    auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
    auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
    auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

    _return.forumId = forum_schema.id;
    _return.forumTitle = forum_buf.GetString(0 + offsetof(snb::ForumSchema::Forum, data), forum_schema.titleLen());
    _return.moderatorId = person_schema.id;
    _return.moderatorFirstName =
        person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
    _return.moderatorLastName = person_buf.GetString(
        person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
}

#else
void InteractiveHandler::shortQuery6(ShortQuery6Response &_return, const ShortQuery6Request &request)
{
    _return = ShortQuery6Response();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto message = (snb::MessageSchema::Message *)engine.get_vertex(vid).data();
    if (!message)
        return;
    for (; message->replyOfComment != (uint64_t)-1;
         message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfComment).data())
        ;
    if (message->replyOfPost != (uint64_t)-1)
        message = (snb::MessageSchema::Message *)engine.get_vertex(message->replyOfPost).data();
    auto forum = (snb::ForumSchema::Forum *)engine.get_vertex(message->forumid).data();
    if (!engine.get_vertex(vid).data())
        return;
    auto person = (snb::PersonSchema::Person *)engine.get_vertex(forum->moderator).data();
    _return.forumId = forum->id;
    _return.forumTitle = std::string(forum->title(), forum->titleLen());
    _return.moderatorId = person->id;
    _return.moderatorFirstName = std::string(person->firstName(), person->firstNameLen());
    _return.moderatorLastName = std::string(person->lastName(), person->lastNameLen());
}
#endif