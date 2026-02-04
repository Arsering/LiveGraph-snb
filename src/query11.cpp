#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED
void InteractiveHandler::query11(std::vector<Query11Response> &_return, const Query11Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t country = placeSchema.findName(request.countryName);
    if (country == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    auto orgs = multihop(engine, country, 1, {(label_t)snb::EdgeSchema::Place2Org});

    std::set<std::tuple<size_t, uint64_t, std::string>> order_container;
    size_t max_count = std::numeric_limits<size_t>::max();
    size_t count_of_min_count = 0;

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (size_t i = 0; i < orgs.size(); i++)
    {
        uint64_t vid = orgs[i];
        edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Org2Person_work, false);
    }
    {
        auto nbrses = engine.get_edges_gbp(edgelist_infos);
        auto [org_schemas, org_bufs] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(orgs);
        for (size_t i = 0; i < orgs.size(); i++)
        {
            uint64_t vid = orgs[i];
            auto &nbrs = nbrses[i];
            auto &org_schema = org_schemas[i];
            auto &org_buf = org_bufs[i];
            auto org_name = org_buf.GetString(0 + offsetof(snb::OrgSchema::Org, data), org_schema.nameLen());
            while (nbrs.valid())
            {
                int32_t date = *(uint32_t *)nbrs.edge_data().data();
                if (date < request.workFromYear)
                {
                    auto person_vid = nbrs.dst_id();
                    if (*std::lower_bound(friends.begin(), friends.end(), person_vid) == person_vid)
                    {
                        if (order_container.size() < (size_t)request.limit || (date <= max_count))
                        {
                            order_container.emplace(date, person_vid, org_name);
                            if (date == max_count)
                            {
                                count_of_min_count++;
                            }
                            else if (date > max_count || max_count == std::numeric_limits<size_t>::max())
                            {
                                max_count = date;
                                count_of_min_count = 1;
                            }
                            else
                            {
                                if ((order_container.size() - (size_t)request.limit) == count_of_min_count)
                                {
                                    while (std::get<0>(*order_container.rbegin()) == max_count)
                                    {
                                        order_container.erase(std::prev(order_container.rbegin().base()));
                                    }
                                    max_count = std::get<0>(*order_container.rbegin());
                                    count_of_min_count = 0;
                                    for (auto it = order_container.rbegin(); it != order_container.rend(); ++it)
                                    {
                                        if (std::get<0>(*it) != max_count)
                                        {
                                            break;
                                        }
                                        count_of_min_count++;
                                    }
                                }
                            }
                        }
                    }
                }
                nbrs.next();
            }
        }
    }

    std::vector<vertex_t> person_vids;
    std::map<std::tuple<int, int64_t, std::string>, uint64_t, std::greater<std::tuple<int, int64_t, std::string>>> idx;
    {
        for (auto &item : order_container)
        {
            person_vids.emplace_back(std::get<1>(item));
        }
        {
            auto [person_schemas, _] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(person_vids);
            size_t person_cursor = 0;
            for (auto &item : order_container)
            {
                idx.emplace(std::make_tuple(-std::get<0>(item), -(int64_t)person_schemas[person_cursor++].id,
                                            std::get<2>(item)),
                            std::get<1>(item));
            }
        }
        while (idx.size() > (size_t)request.limit)
            idx.erase(idx.rbegin()->first);
    }

    person_vids.clear();
    for (auto p : idx)
    {
        person_vids.emplace_back(p.second);
    }
    auto [person_schemas, person_bufs] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(person_vids);
    size_t person_cursor = 0;
    for (auto &p : idx)
    {
        _return.emplace_back();
        auto &person_schema = person_schemas[person_cursor];
        auto &person_buf = person_bufs[person_cursor++];
        _return.back().personId = person_schema.id;
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().organizationName = std::get<2>(p.first);
        _return.back().organizationWorkFromYear = -std::get<0>(p.first);
    }
}
#else
void InteractiveHandler::query11(std::vector<Query11Response> &_return, const Query11Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t country = placeSchema.findName(request.countryName);
    if (country == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    auto orgs = multihop(engine, country, 1, {(label_t)snb::EdgeSchema::Place2Org});

    std::map<std::tuple<int, int64_t, std::string>, uint64_t, std::greater<std::tuple<int, int64_t, std::string>>> idx;

    for (size_t i = 0; i < orgs.size(); i++)
    {
        uint64_t vid = orgs[i];
        auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Org2Person_work);

        auto [org_schema, org_buf] = engine.get_vertex_with_data_gbp<snb::OrgSchema::Org>(vid);

        while (nbrs.valid())
        {
            int32_t date = *(uint32_t *)nbrs.edge_data().data();
            if (date < request.workFromYear)
            {
                auto person_vid = nbrs.dst_id();
                if (*std::lower_bound(friends.begin(), friends.end(), person_vid) == person_vid)
                {
                    auto [person_schema, person_addr] =
                        engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(person_vid);

                    uint64_t person_id = person_schema.id;
                    auto key = std::make_tuple(
                        -date, -(int64_t)person_id,
                        org_buf.GetString(0 + offsetof(snb::OrgSchema::Org, data), org_schema.nameLen()));
                    auto value = person_vid;

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first < key)
                    {
                        idx.emplace(key, value);
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                }
            }
            nbrs.next();
        }
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        // auto person = (snb::PersonSchema::Person *)engine.get_vertex(p.second).data();
        auto [person_schema, person_buf] = engine.get_vertex_with_data_gbp<snb::PersonSchema::Person>(p.second);

        _return.back().personId = person_schema.id;
        _return.back().personFirstName =
            person_buf.GetString(0 + offsetof(snb::PersonSchema::Person, data), person_schema.firstNameLen());
        _return.back().personLastName = person_buf.GetString(
            person_schema.lastName_offset + offsetof(snb::PersonSchema::Person, data), person_schema.lastNameLen());
        _return.back().organizationName = std::get<2>(p.first);
        _return.back().organizationWorkFromYear = -std::get<0>(p.first);
    }
}
#endif
#else
void InteractiveHandler::query11(std::vector<Query11Response> &_return, const Query11Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    uint64_t country = placeSchema.findName(request.countryName);
    if (country == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;
    auto friends =
        multihop(engine, vid, 2, {(label_t)snb::EdgeSchema::Person2Person, (label_t)snb::EdgeSchema::Person2Person});
    friends.push_back(std::numeric_limits<uint64_t>::max());
    auto orgs = multihop(engine, country, 1, {(label_t)snb::EdgeSchema::Place2Org});

    std::map<std::tuple<int, int64_t, std::string>, uint64_t, std::greater<std::tuple<int, int64_t, std::string>>> idx;

    for (size_t i = 0; i < orgs.size(); i++)
    {
        uint64_t vid = orgs[i];
        auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Org2Person_work);
        auto org = (snb::OrgSchema::Org *)engine.get_vertex(vid).data();
        while (nbrs.valid())
        {
            int32_t date = *(uint32_t *)nbrs.edge_data().data();
            if (date < request.workFromYear)
            {
                auto person_vid = nbrs.dst_id();
                if (*std::lower_bound(friends.begin(), friends.end(), person_vid) == person_vid)
                {
                    auto person = (snb::PersonSchema::Person *)engine.get_vertex(person_vid).data();
                    uint64_t person_id = person->id;
                    auto key = std::make_tuple(-date, -(int64_t)person_id, std::string(org->name(), org->nameLen()));
                    auto value = person_vid;

                    if (idx.size() < (size_t)request.limit || idx.rbegin()->first < key)
                    {
                        idx.emplace(key, value);
                        while (idx.size() > (size_t)request.limit)
                            idx.erase(idx.rbegin()->first);
                    }
                }
            }
            nbrs.next();
        }
    }

    for (auto p : idx)
    {
        _return.emplace_back();
        auto person = (snb::PersonSchema::Person *)engine.get_vertex(p.second).data();
        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().organizationName = std::get<2>(p.first);
        _return.back().organizationWorkFromYear = -std::get<0>(p.first);
    }
}
#endif