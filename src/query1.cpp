#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED
void InteractiveHandler::query1(std::vector<Query1Response> &_return, const Query1Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (vertex_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    std::vector<std::tuple<int, std::string, uint64_t, uint64_t, gbp::BufferBlock>> idx;

    std::vector<vertex_t> frontier = {vid};
    std::vector<vertex_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;

    for (int k = 0; k < 3 && idx.size() < (size_t)request.limit; k++)
    {
        next_frontier.clear();
        std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
        for (auto vid : frontier)
        {
            edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Person2Person, false);
        }
        {
            auto nbrses = engine.get_edges_gbp(edgelist_infos);
            for (auto idx = 0; idx < nbrses.size(); idx++)
            {
                auto &nbrs = nbrses[idx];
                while (nbrs.valid())
                {
                    if (nbrs.dst_id() != root && person_hash.find(nbrs.dst_id()) == person_hash.end())
                    {
                        next_frontier.push_back(nbrs.dst_id());
                        person_hash.emplace(nbrs.dst_id());
                    }
                    nbrs.next();
                }
            }
        }

        auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(next_frontier);
        for (auto i = 0; i < next_frontier.size(); i++)
        {
            auto &person_schema = person_schemas[i];
            auto &person_buf = person_bufs[i];

            auto firstName =
                person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
            if (firstName == request.firstName)
            {
                auto lastName =
                    person_buf.GetString(person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data),
                                         person_schema.lastNameLen());
                idx.push_back(std::make_tuple(k, lastName, person_schema.id, next_frontier[i], person_buf));
            }
        }
        frontier.swap(next_frontier);
    }

    std::sort(idx.begin(), idx.end());
    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    std::vector<vertex_t> place_vids;
    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto vid = std::get<3>(idx[i]);
        auto person_string = std::get<4>(idx[i]).GetString(0, std::get<4>(idx[i]).Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();
        _return.back().friendId = person->id;
        _return.back().friendLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().distanceFromPerson = std::get<0>(idx[i]) + 1;
        _return.back().friendBirthday = to_time(person->birthday);
        _return.back().friendCreationDate = person->creationDate;
        _return.back().friendGender = std::string(person->gender(), person->genderLen());
        _return.back().friendBrowserUsed = std::string(person->browserUsed(), person->browserUsedLen());
        _return.back().friendLocationIp = std::string(person->locationIP(), person->locationIPLen());
        _return.back().friendEmails = split(std::string(person->emails(), person->emailsLen()), zero_split);
        _return.back().friendLanguages = split(std::string(person->speaks(), person->speaksLen()), zero_split);
        place_vids.push_back(person->place);
        edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Person2Org_study, false);
        edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Person2Org_work, false);
    }
    idx.clear();
    auto nbrses = engine.get_edges_gbp(edgelist_infos);
    size_t nbrs_cursor = 0;
    auto [place_schemas, place_bufs] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(place_vids);
    for (size_t i = 0; i < _return.size(); i++)
    {
        auto &place_schema = place_schemas[i];
        auto &place_buf = place_bufs[i];

        _return[i].friendCityName =
            place_buf.GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schema.nameLen());
        {
            auto nbrs = nbrses[nbrs_cursor++];
            // auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Org_study);
            std::vector<std::tuple<uint64_t, int, std::string, std::string>> orgs;
            std::vector<vertex_t> org_vids;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                org_vids.emplace_back(nbrs.dst_id());
                orgs.emplace_back(0ull, year, "", "");
                nbrs.next();
            }
            auto [org_schemas, org_bufs] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(org_vids);
            std::vector<vertex_t> place_vids;
            for (auto j = 0; j < org_vids.size(); j++)
            {
                auto &org_schema = org_schemas[j];
                auto &org_buf = org_bufs[j];
                std::get<0>(orgs[j]) = org_schema.id;
                std::get<2>(orgs[j]) = org_buf.GetString(0 + offsetof(snb::OrgSchema::Org, data), org_schema.nameLen());
                place_vids.push_back(org_schema.place);
            }
            auto [place_schemas, place_bufs] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(place_vids);
            for (auto j = 0; j < org_vids.size(); j++)
            {
                auto &place_schema = place_schemas[j];
                auto &place_buf = place_bufs[j];
                std::get<3>(orgs[j]) =
                    place_buf.GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schema.nameLen());
            }
            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                _return[i].friendUniversities_year.emplace_back(year);
                _return[i].friendUniversities_name.emplace_back(std::get<2>(t));
                _return[i].friendUniversities_city.emplace_back(std::get<3>(t));
            }
        }

        {
            // auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Org_work);
            auto nbrs = nbrses[nbrs_cursor++];
            std::vector<std::tuple<uint64_t, int, std::string, std::string>> orgs;
            std::vector<vertex_t> org_vids;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                org_vids.emplace_back(nbrs.dst_id());
                orgs.emplace_back(0ull, year, "", "");
                nbrs.next();
            }
            auto [org_schemas, org_bufs] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(org_vids);
            std::vector<vertex_t> place_vids;
            for (auto j = 0; j < org_vids.size(); j++)
            {
                auto &org_schema = org_schemas[j];
                auto &org_buf = org_bufs[j];
                std::get<0>(orgs[j]) = org_schema.id;
                std::get<2>(orgs[j]) = org_buf.GetString(0 + offsetof(snb::OrgSchema::Org, data), org_schema.nameLen());
                place_vids.push_back(org_schema.place);
            }
            auto [place_schemas, place_bufs] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(place_vids);
            for (auto j = 0; j < org_vids.size(); j++)
            {
                auto &place_schema = place_schemas[j];
                auto &place_buf = place_bufs[j];
                std::get<3>(orgs[j]) =
                    place_buf.GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schema.nameLen());
            }

            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                _return[i].friendCompanies_year.emplace_back(year);
                _return[i].friendCompanies_name.emplace_back(std::get<2>(t));
                _return[i].friendCompanies_city.emplace_back(std::get<3>(t));
            }
        }
    }
}

#else
void InteractiveHandler::query1(std::vector<Query1Response> &_return, const Query1Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    std::vector<std::tuple<int, std::string, uint64_t, uint64_t, gbp::BufferBlock>> idx;

    std::vector<size_t> frontier = {vid};
    std::vector<size_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;

    for (int k = 0; k < 3 && idx.size() < (size_t)request.limit; k++)
    {
        next_frontier.clear();
        for (auto vid : frontier)
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Person);
            while (nbrs.valid())
            {

                if (nbrs.dst_id() != root && person_hash.find(nbrs.dst_id()) == person_hash.end())
                {

                    next_frontier.push_back(nbrs.dst_id());
                    person_hash.emplace(nbrs.dst_id());

                    auto [person_schema, person_buf] =
                        engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(nbrs.dst_id());

                    auto firstName = person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data),
                                                          person_schema.firstNameLen());
                    if (firstName == request.firstName)
                    {
                        auto lastName = person_buf.GetString(person_schema.lastName_offset +
                                                                 offsetof(snb::PersonSchema::Person, data),
                                                             person_schema.lastNameLen());
                        idx.push_back(std::make_tuple(k, lastName, person_schema.id, nbrs.dst_id(), person_buf));
                    }
                }

                nbrs.next();
            }
        }
        frontier.swap(next_frontier);
    }

    std::sort(idx.begin(), idx.end());

    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto vid = std::get<3>(idx[i]);
        auto person_string = std::get<4>(idx[i]).GetString(0, std::get<4>(idx[i]).Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();
        _return.back().friendId = person->id;
        _return.back().friendLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().distanceFromPerson = std::get<0>(idx[i]) + 1;
        _return.back().friendBirthday = to_time(person->birthday);
        _return.back().friendCreationDate = person->creationDate;
        _return.back().friendGender = std::string(person->gender(), person->genderLen());
        _return.back().friendBrowserUsed = std::string(person->browserUsed(), person->browserUsedLen());
        _return.back().friendLocationIp = std::string(person->locationIP(), person->locationIPLen());
        _return.back().friendEmails = split(std::string(person->emails(), person->emailsLen()), zero_split);
        _return.back().friendLanguages = split(std::string(person->speaks(), person->speaksLen()), zero_split);
        auto [place_schema, place_buf] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(person->place);
        _return.back().friendCityName =
            place_buf.GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schema.nameLen());
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Org_study);
            std::vector<std::tuple<uint64_t, int, gbp::BufferBlock, gbp::BufferBlock>> orgs;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                uint64_t vid = nbrs.dst_id();
                auto [org_schema, org_buf] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(vid);
                auto [_, place_buf] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(org_schema.place);
                orgs.emplace_back(org_schema.id, year, org_buf, place_buf);
                nbrs.next();
            }
            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                _return.back().friendUniversities_year.emplace_back(year);

                auto org_string = std::get<2>(t).GetString(0, std::get<2>(t).Size());
                auto org = (snb::OrgSchema::Org *)org_string.data();
                _return.back().friendUniversities_name.emplace_back(org->name(), org->nameLen());
                auto place_string = std::get<3>(t).GetString(0, std::get<3>(t).Size());
                auto place = (snb::PlaceSchema::Place *)place_string.data();
                _return.back().friendUniversities_city.emplace_back(place->name(), place->nameLen());
            }
        }
        {
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Org_work);
            std::vector<std::tuple<uint64_t, int, gbp::BufferBlock, gbp::BufferBlock>> orgs;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                uint64_t vid = nbrs.dst_id();
                auto [org_schema, org_buf] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(vid);
                auto [_, place_buf] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(org_schema.place);
                orgs.emplace_back(org_schema.id, year, org_buf, place_buf);
                nbrs.next();
            }
            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                _return.back().friendCompanies_year.emplace_back(year);
                auto org_string = std::get<2>(t).GetString(0, std::get<2>(t).Size());
                auto org = (snb::OrgSchema::Org *)org_string.data();
                _return.back().friendCompanies_name.emplace_back(org->name(), org->nameLen());
                auto place_string = std::get<3>(t).GetString(0, std::get<3>(t).Size());
                auto place = (snb::PlaceSchema::Place *)place_string.data();
                _return.back().friendCompanies_city.emplace_back(place->name(), place->nameLen());
            }
        }
    }
}
#endif

#else

void InteractiveHandler::query1(std::vector<Query1Response> &_return, const Query1Request &request)
{
    _return.clear();
    GBPLOG << "cp";
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    std::vector<std::tuple<int, std::string, uint64_t, uint64_t, snb::PersonSchema::Person *>> idx;

    std::vector<size_t> frontier = {vid};
    std::vector<size_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;

    for (int k = 0; k < 3 && idx.size() < (size_t)request.limit; k++)
    {
        next_frontier.clear();
        for (auto vid : frontier)
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Person);
            while (nbrs.valid())
            {
                if (nbrs.dst_id() != root && person_hash.find(nbrs.dst_id()) == person_hash.end())
                {

                    next_frontier.push_back(nbrs.dst_id());
                    person_hash.emplace(nbrs.dst_id());
                    auto person = (snb::PersonSchema::Person *)engine.get_vertex(nbrs.dst_id()).data();
                    auto firstName = std::string(person->firstName(), person->firstNameLen());

                    if (firstName == request.firstName)
                    {
                        auto lastName = std::string(person->lastName(), person->lastNameLen());
                        idx.push_back(std::make_tuple(k, lastName, person->id, nbrs.dst_id(), person));
                    }
                }

                nbrs.next();
            }
        }
        frontier.swap(next_frontier);
    }

    std::sort(idx.begin(), idx.end());

    for (size_t i = 0; i < std::min((size_t)request.limit, idx.size()); i++)
    {
        _return.emplace_back();
        auto vid = std::get<3>(idx[i]);
        auto person = std::get<4>(idx[i]);
        _return.back().friendId = person->id;
        _return.back().friendLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().distanceFromPerson = std::get<0>(idx[i]) + 1;
        _return.back().friendBirthday = to_time(person->birthday);
        _return.back().friendCreationDate = person->creationDate;
        _return.back().friendGender = std::string(person->gender(), person->genderLen());
        _return.back().friendBrowserUsed = std::string(person->browserUsed(), person->browserUsedLen());
        _return.back().friendLocationIp = std::string(person->locationIP(), person->locationIPLen());
        _return.back().friendEmails = split(std::string(person->emails(), person->emailsLen()), zero_split);
        _return.back().friendLanguages = split(std::string(person->speaks(), person->speaksLen()), zero_split);

        auto place = (snb::PlaceSchema::Place *)engine.get_vertex(person->place).data();
        _return.back().friendCityName = std::string(place->name(), place->nameLen());
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Org_study);
            std::vector<std::tuple<uint64_t, int, snb::OrgSchema::Org *, snb::PlaceSchema::Place *>> orgs;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                uint64_t vid = nbrs.dst_id();
                auto org = (snb::OrgSchema::Org *)engine.get_vertex(vid).data();
                auto place = (snb::PlaceSchema::Place *)engine.get_vertex(org->place).data();
                orgs.emplace_back(org->id, year, org, place);
                nbrs.next();
            }
            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                auto org = std::get<2>(t);
                auto place = std::get<3>(t);
                _return.back().friendUniversities_year.emplace_back(year);
                _return.back().friendUniversities_name.emplace_back(org->name(), org->nameLen());
                _return.back().friendUniversities_city.emplace_back(place->name(), place->nameLen());
            }
        }
        {
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Org_work);
            std::vector<std::tuple<uint64_t, int, snb::OrgSchema::Org *, snb::PlaceSchema::Place *>> orgs;
            while (nbrs.valid())
            {
                int year = *(int *)nbrs.edge_data().data();
                uint64_t vid = nbrs.dst_id();
                auto org = (snb::OrgSchema::Org *)engine.get_vertex(vid).data();
                auto place = (snb::PlaceSchema::Place *)engine.get_vertex(org->place).data();
                orgs.emplace_back(org->id, year, org, place);
                nbrs.next();
            }
            std::sort(orgs.begin(), orgs.end());
            for (auto t : orgs)
            {
                int year = std::get<1>(t);
                auto org = std::get<2>(t);
                auto place = std::get<3>(t);
                _return.back().friendCompanies_year.emplace_back(year);
                _return.back().friendCompanies_name.emplace_back(org->name(), org->nameLen());
                _return.back().friendCompanies_city.emplace_back(place->name(), place->nameLen());
            }
        }
    }
}

#endif