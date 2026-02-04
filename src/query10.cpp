#include "manual.hpp"

#if GOCACHE_ENABLED
#if GOCACHE_BATCH_IO_ENABLED
void InteractiveHandler::query10(std::vector<Query10Response> &_return, const Query10Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;

    std::vector<size_t> frontier = {vid};
    std::vector<size_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;

    std::vector<std::tuple<vertex_t, label_t, bool>> edgelist_infos;
    for (int k = 0; k < 2; k++)
    {
        next_frontier.clear();

        edgelist_infos.clear();
        for (auto vid : frontier)
        {
            edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Person2Person, false);
        }
        auto nbrses = engine.get_edges_gbp(edgelist_infos);
        size_t nbrs_cursor = 0;

        for (auto vid : frontier)
        {
            auto &nbrs = nbrses[nbrs_cursor++];
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
        frontier.swap(next_frontier);
    }
    auto friends = frontier;
    std::sort(friends.begin(), friends.end());

    auto tags = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Tag});
    auto nextMonth = request.month % 12 + 1;
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, std::pair<uintptr_t, size_t>> idx;
    auto [person_schemas, person_addrs] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(friends);

    edgelist_infos.clear();
    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto &person_schema = person_schemas[i];
        std::pair<int, int> monday;

        {
            monday = to_monday(person_schema.birthday);
        }
        if ((monday.first == request.month && monday.second >= 21) || (monday.first == nextMonth && monday.second < 22))
        {
            edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Person2Post_creator, false);
        }
    }
    std::vector<size_t> intervals;
    intervals.emplace_back(0);
    {
        auto nbrses = engine.get_edges_gbp(edgelist_infos);
        size_t nbrs_cursor = 0;
        edgelist_infos.clear();
        for (size_t i = 0; i < friends.size(); i++)
        {
            uint64_t vid = friends[i];
            auto &person_schema = person_schemas[i];
            std::pair<int, int> monday;

            {
                monday = to_monday(person_schema.birthday);
            }
            if ((monday.first == request.month && monday.second >= 21) ||
                (monday.first == nextMonth && monday.second < 22))
            {
                auto &nbrs = nbrses[nbrs_cursor++];
                while (nbrs.valid())
                {
                    uint64_t vid = nbrs.dst_id();
                    // auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    edgelist_infos.emplace_back(vid, (label_t)snb::EdgeSchema::Post2Tag, false);
                    nbrs.next();
                }
                intervals.emplace_back(edgelist_infos.size());
            }
        }
    }
    auto nbrses = engine.get_edges_gbp(edgelist_infos);

    size_t nbrs_cursor = 0;
    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto &person_schema = person_schemas[i];
        auto person_addr = person_addrs[i];
        std::pair<int, int> monday;

        {
            monday = to_monday(person_schema.birthday);
        }
        if ((monday.first == request.month && monday.second >= 21) || (monday.first == nextMonth && monday.second < 22))
        {
            int commonInterestScore = 0;
            for (auto j = intervals[nbrs_cursor]; j < intervals[nbrs_cursor + 1]; j++)
            {
                bool flag = false;
                {
                    auto &nbrs = nbrses[j];
                    while (nbrs.valid() && !flag)
                    {
                        uint64_t tag = nbrs.dst_id();
                        if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                        {
                            flag = true;
                        }
                        nbrs.next();
                    }
                }
                if (flag)
                    commonInterestScore++;
                else
                    commonInterestScore--;
            }
            nbrs_cursor++;
            auto key = std::make_pair(-commonInterestScore, person_schema.id);

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(
                    key, std::make_pair(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data)));
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }
    nbrses.clear();

    std::vector<gbp::batch_request_type> blk_infos;
    std::vector<gbp::BufferBlock> results;
    for (auto p : idx)
    {
        blk_infos.emplace_back(p.second.first, p.second.second, 0);
    }
    getBufferBlockBatch(blk_infos, results);
    size_t person_cursor = 0;
    std::vector<vertex_t> place_vids;
    for (auto p : idx)
    {
        _return.emplace_back();
        auto &person_buf = results[person_cursor++];
        auto person_string = person_buf.GetString(0, person_buf.Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();

        place_vids.emplace_back(person->place);

        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().commonInterestSore = -p.first.first;
        _return.back().personGender = std::string(person->gender(), person->genderLen());
    }
    results.clear();
    auto [place_schemas, place_bufs] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(place_vids);
    for (size_t i = 0; i < place_vids.size(); i++)
    {
        _return[i].personCityName =
            place_bufs[i].GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schemas[i].nameLen());
    }
}
#else
void InteractiveHandler::query10(std::vector<Query10Response> &_return, const Query10Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex_gbp(vid) == BlockManager::NULLPOINTER)
        return;

    std::vector<size_t> frontier = {vid};
    std::vector<size_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;
    for (int k = 0; k < 2; k++)
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
                }
                nbrs.next();
            }
        }
        frontier.swap(next_frontier);
    }
    auto friends = frontier;
    std::sort(friends.begin(), friends.end());

    auto tags = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Tag});
    auto nextMonth = request.month % 12 + 1;
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, gbp::BufferBlock> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto [person_schema, person_addr] = engine.get_vertex_without_data_gbp<snb::PersonSchema::Person>(vid);

        std::pair<int, int> monday;

        {
            monday = to_monday(person_schema.birthday);
        }
        if ((monday.first == request.month && monday.second >= 21) || (monday.first == nextMonth && monday.second < 22))
        {
            int commonInterestScore = 0;
            auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            while (nbrs.valid())
            {
                bool flag = false;
                uint64_t vid = nbrs.dst_id();
                {
                    auto nbrs = engine.get_edges_gbp(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid() && !flag)
                    {
                        uint64_t tag = nbrs.dst_id();
                        if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                        {
                            flag = true;
                        }
                        nbrs.next();
                    }
                }
                if (flag)
                    commonInterestScore++;
                else
                    commonInterestScore--;
                nbrs.next();
            }
            auto key = std::make_pair(-commonInterestScore, person_schema.id);

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(
                    key, getBufferBlock(person_addr, person_schema.length + offsetof(snb::PersonSchema::Person, data)));
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        // auto person = p.second;
        auto person_string = p.second.GetString(0, p.second.Size());
        auto person = (snb::PersonSchema::Person *)person_string.data();

        auto [place_schema, place_buf] = engine.get_vertex_with_data_gbp<snb::PlaceSchema::Place>(person->place);
        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().commonInterestSore = -p.first.first;
        _return.back().personGender = std::string(person->gender(), person->genderLen());
        _return.back().personCityName =
            place_buf.GetString(0 + offsetof(snb::PlaceSchema::Place, data), place_schema.nameLen());
    }
}
#endif
#else
void InteractiveHandler::query10(std::vector<Query10Response> &_return, const Query10Request &request)
{
    _return.clear();
    uint64_t vid = personSchema.findId(request.personId);
    if (vid == (uint64_t)-1)
        return;
    auto engine = graph->begin_read_only_transaction();
    if (engine.get_vertex(vid).data() == nullptr)
        return;

    std::vector<size_t> frontier = {vid};
    std::vector<size_t> next_frontier;
    std::unordered_set<uint64_t> person_hash{vid};
    uint64_t root = vid;
    for (int k = 0; k < 2; k++)
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
                }
                nbrs.next();
            }
        }
        frontier.swap(next_frontier);
    }
    auto friends = frontier;
    std::sort(friends.begin(), friends.end());

    auto tags = multihop(engine, vid, 1, {(label_t)snb::EdgeSchema::Person2Tag});
    auto nextMonth = request.month % 12 + 1;
    tags.push_back(std::numeric_limits<uint64_t>::max());
    std::map<std::pair<int, uint64_t>, snb::PersonSchema::Person *> idx;

    for (size_t i = 0; i < friends.size(); i++)
    {
        uint64_t vid = friends[i];
        auto person = (snb::PersonSchema::Person *)(engine.get_vertex(vid)).data();
        std::pair<int, int> monday;

        {
            monday = to_monday(person->birthday);
        }
        if ((monday.first == request.month && monday.second >= 21) || (monday.first == nextMonth && monday.second < 22))
        {
            int commonInterestScore = 0;
            auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Person2Post_creator);
            while (nbrs.valid())
            {
                bool flag = false;
                uint64_t vid = nbrs.dst_id();
                {
                    auto nbrs = engine.get_edges(vid, (label_t)snb::EdgeSchema::Post2Tag);
                    while (nbrs.valid() && !flag)
                    {
                        uint64_t tag = nbrs.dst_id();
                        if (*std::lower_bound(tags.begin(), tags.end(), tag) == tag)
                        {
                            flag = true;
                        }
                        nbrs.next();
                    }
                }
                if (flag)
                    commonInterestScore++;
                else
                    commonInterestScore--;
                nbrs.next();
            }
            auto key = std::make_pair(-commonInterestScore, person->id);
            auto value = person;

            if (idx.size() < (size_t)request.limit || idx.rbegin()->first > key)
            {
                idx.emplace(key, value);
                while (idx.size() > (size_t)request.limit)
                    idx.erase(idx.rbegin()->first);
            }
        }
    }
    for (auto p : idx)
    {
        _return.emplace_back();
        auto person = p.second;
        auto place = (snb::PlaceSchema::Place *)engine.get_vertex(person->place).data();
        _return.back().personId = person->id;
        _return.back().personFirstName = std::string(person->firstName(), person->firstNameLen());
        _return.back().personLastName = std::string(person->lastName(), person->lastNameLen());
        _return.back().commonInterestSore = -p.first.first;
        _return.back().personGender = std::string(person->gender(), person->genderLen());
        _return.back().personCityName = std::string(place->name(), place->nameLen());
    }
}
#endif