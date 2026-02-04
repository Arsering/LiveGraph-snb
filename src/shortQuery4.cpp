#include "manual.hpp"

#if GOCACHE_ENABLED

void InteractiveHandler::shortQuery4(ShortQuery4Response &_return, const ShortQuery4Request &request)
{
    _return = ShortQuery4Response();
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
    auto message_buf =
        getBufferBlock(message_addr, message_schema.length + offsetof(snb::MessageSchema::Message, data));

    _return.messageCreationDate = message_schema.creationDate;
    _return.messageContent =
        message_schema.contentLen()
            ? message_buf.GetString(message_schema.content_offset + offsetof(snb::MessageSchema::Message, data),
                                    message_schema.contentLen())
            : message_buf.GetString(0 + offsetof(snb::MessageSchema::Message, data), message_schema.imageFileLen());
}
#else
void InteractiveHandler::shortQuery4(ShortQuery4Response &_return, const ShortQuery4Request &request)
{
    _return = ShortQuery4Response();
    uint64_t vid = postSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        vid = commentSchema.findId(request.messageId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto message = (snb::MessageSchema::Message *)engine.get_vertex(vid).data();
    if (!message)
        return;
    _return.messageCreationDate = message->creationDate;
    _return.messageContent = message->contentLen() ? std::string(message->content(), message->contentLen())
                                                   : std::string(message->imageFile(), message->imageFileLen());
}
#endif