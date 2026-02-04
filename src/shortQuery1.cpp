#include "manual.hpp"

#if GOCACHE_ENABLED

void InteractiveHandler::shortQuery1(ShortQuery1Response &_return, const ShortQuery1Request &request)
{
    _return = ShortQuery1Response();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();

    auto person_addr = engine.get_vertex_gbp(vid);
    auto person_schema_buf = getBufferBlock(person_addr, sizeof(snb::PersonSchema::Person));
    auto person_schema = person_schema_buf.GetInnerObj<snb::PersonSchema::Person>();
    auto person_buf = getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data));

    if (person_addr == BlockManager::NULLPOINTER)
        return;

    _return.firstName =
        person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());

    _return.lastName = person_buf.GetString(person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data),
                                            person_schema.lastNameLen());

    _return.birthday = to_time(person_schema.birthday);
    _return.locationIp = person_buf.GetString(
        person_schema.locationIP_offset + offsetof(snb::PersonSchema::Person, data), person_schema.locationIPLen());
    _return.browserUsed = person_buf.GetString(
        person_schema.browserUsed_offset + offsetof(snb::PersonSchema::Person, data), person_schema.browserUsedLen());

    auto place_addr = engine.get_vertex_gbp(person_schema.place);
    auto place_schema_buf = getBufferBlock(place_addr, sizeof(snb::PlaceSchema::Place));
    auto place_schema = place_schema_buf.GetInnerObj<snb::PlaceSchema::Place>();

    _return.cityId = place_schema.id;
    _return.gender = person_buf.GetString(person_schema.gender_offset + offsetof(snb::PersonSchema::Person, data),
                                          person_schema.genderLen());
    _return.creationDate = person_schema.creationDate;
}
#else
void InteractiveHandler::shortQuery1(ShortQuery1Response &_return, const ShortQuery1Request &request)
{
    _return = ShortQuery1Response();
    GBPLOG << "cp";
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    auto person = (snb::PersonSchema::Person *)engine.get_vertex(vid).data();
    if (!person)
        return;
    _return.firstName = std::string(person->firstName(), person->firstNameLen());
    _return.lastName = std::string(person->lastName(), person->lastNameLen());
    _return.birthday = to_time(person->birthday);
    _return.locationIp = std::string(person->locationIP(), person->locationIPLen());
    _return.browserUsed = std::string(person->browserUsed(), person->browserUsedLen());
    auto place = (snb::PlaceSchema::Place *)engine.get_vertex(person->place).data();
    _return.cityId = place->id;
    _return.gender = std::string(person->gender(), person->genderLen());
    _return.creationDate = person->creationDate;
}
#endif