// #include "manual.hpp"
// #include <tuple>

// void InteractiveHandler::query3(std::vector<Query3Response> &_return, const Query3Request &request)
// {
//     _return.clear();
//     uint64_t vid = personSchema.findId(request.personId);
//     uint64_t countryX = placeSchema.findName(request.countryXName);
//     uint64_t countryY = placeSchema.findName(request.countryYName);
//     if (vid == (uint64_t)-1)
//         return;
//     if (countryX == (uint64_t)-1)
//         return;
//     if (countryY == (uint64_t)-1)
//         return;
//     uint64_t endDate = request.startDate + 24lu * 60lu * 60lu * 1000lu * request.durationDays;
//     auto engine = graph->begin_read_only_transaction();
//     if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
//         return;
//     auto friends =
//         multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
//     std::vector<std::tuple<int, uint64_t, int, gbp::BufferBlock>> idx;

//     for (size_t i = 0; i < friends.size(); i++)
//     {
//         auto [person_schema, person_addr] =
//         engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(friends[i]); auto [place_schema, _] =
//         engine.get_vertex_without_data_gbp<snb::PlaceSchema::Place>(person_schema.place);

//         if (place_schema.isPartOf != countryX && place_schema.isPartOf != countryY)
//         {
//             uint64_t vid = friends[i];
//             int xCount = 0, yCount = 0;
//             {
//                 auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Comment_creator);
//                 while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
//                 {
//                     uint64_t date = *(uint64_t *)nbrs.edge_data().data();
//                     if (date < endDate)
//                     {
//                         auto [message_schema, _] =
//                             engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());

//                         if (message_schema.place == countryX)
//                             xCount++;
//                         if (message_schema.place == countryY)
//                             yCount++;
//                     }
//                     nbrs.next();
//                 }
//             }
//             {
//                 auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
//                 while (nbrs.valid() && *(uint64_t *)nbrs.edge_data().data() >= (uint64_t)request.startDate)
//                 {
//                     uint64_t date = *(uint64_t *)nbrs.edge_data().data();
//                     if (date < endDate)
//                     {
//                         auto [message_schema, _] =
//                             engine.get_vertex_without_data_gbp<snb::MessageSchema::Message>(nbrs.dst_id());
//                         if (message_schema.place == countryX)
//                             xCount++;
//                         if (message_schema.place == countryY)
//                             yCount++;
//                     }
//                     nbrs.next();
//                 }
//             }

//             if (xCount > 0 && yCount > 0)
//                 idx.push_back(std::make_tuple(
//                     -xCount, person_schema.id, -yCount,
//                     getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data))));
//         }
//     }
//     std::sort(idx.begin(), idx.end());

//     for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
//     {
//         _return.emplace_back();
//         auto person_string = std::get<3>(idx[i]).GetString(0, std::get<3>(idx[i]).Size());
//         auto person = (snb::PersonSchema::Person *)person_string.data();

//         _return.back().personId = std::get<1>(idx[i]);
//         _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
//         _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
//         _return.back().xCount = -std::get<0>(idx[i]);
//         _return.back().yCount = -std::get<2>(idx[i]);
//         _return.back().count = _return.back().xCount + _return.back().yCount;
//     }
// }
