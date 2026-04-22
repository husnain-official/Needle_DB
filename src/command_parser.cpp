#include "command_parser.h"
//----------------------------Functionality For 'Vector_Server'----------------------------------
// add try-catch blocks in all of these(later) {solved}
// another probleam all errors are printed to the server and not the client {solved}
Parse_result insert_parsing(Vector &v, const std::string &command)
{
    try
    {
        // Declaration of necessry variables
        std::size_t index = 0, next_space_index = 0, to_move = 0;
        // Check-01
        next_space_index = command.find(' ', 0);
        if (next_space_index != 6 or next_space_index == std::string::npos)
        {
            // we have not done anything yet, so nothing to reset
            return {false, "ERROR <Incorrect format for 'INSERT'>\n"};
        }
        index = next_space_index + 1; // 7
        next_space_changes(command, index, next_space_index, to_move);
        // Check-02
        if ((to_move < 1 or to_move > id_length_set) or (next_space_index == std::string::npos)) // range 1-32
        {
            //  again we have not done anything yet, so nothing to reset
            if (next_space_index == std::string::npos)
            {
                return {false, "ERROR <Incorrect format for 'INSERT'>\n"};
            }
            else if (to_move < 1)
            {
                return {false, "ERROR <Id size can not be zero>\n"};
            }
            else
            {
                return {false, "ERROR <Id size can not greater than 32>\n"};
            }
        }
        v.id = command.substr(index, to_move);
        index = next_space_index + 1; // 40(if to_move was 33)
        next_space_changes(command, index, next_space_index, to_move);
        //  Check-03

        if ((to_move != dimensions_no_of_digits) or (next_space_index == std::string::npos))
        {
            //  we should reset v1.id but unnecessary
            return {false, "ERROR <Incorrect 'Dimension' value entered>\n"};
        }
        v.dims = (std::stoi(command.substr(index, to_move)));
        // Check-04
        if (v.dims != dimensions_set)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }
        // Embeddings loop
        v.data.resize(dimensions_set);
        for (std::size_t i = 0; i < dimensions_set; i++)
        {
            index = next_space_index + 1;
            if (i != (dimensions_set - 1))
            {
                next_space_changes(command, index, next_space_index, to_move);
                //  Check-05
                if ((next_space_index == std::string::npos))
                {
                    return {false, "ERROR <Float values do not match the dimenstions>\n"};
                }
                v.data[i] = std::stof(command.substr(index, to_move));
            }
            else
            {
                if (command.size() >= index)
                    to_move = command.size() - index;
                v.data[i] = std::stof(command.substr(index, to_move));
            }
        }
        return {true, ""};
    }
    catch (const std::exception &)
    {
        return {false, "ERROR <Parsing failed: Non-numeric value encountered>\n"};
    }
}
Parse_result query_parsing(Vector &v, size_t &top_k, const std::string &command)
{
    try
    {
        std::size_t index = 0,
                    next_space_index = 0, to_move = 0;
        v.id = "";
        const size_t MAX_TOP_K = 30;
        // const size_t MAX_TOP_K_DIGITS = 2;
        int top_k_raw = 0;
        // Check-01: verify command starts with "QUERY "
        next_space_index = command.find(' ', 0);
        if (next_space_index != 5 or next_space_index == std::string::npos)
        {
            return {false, "ERROR <Incorrect format for 'QUERY'>\n"};
        }
        // Check-02: parse and validate top_k
        index = next_space_index + 1;
        next_space_changes(command, index, next_space_index, to_move);
        if (next_space_index == std::string::npos or to_move < 1)
        {
            return {false, "ERROR <Value of 'top_k' not provided>\n"};
        }
        top_k_raw = std::stoi(command.substr(index, to_move));
        if (top_k_raw < 1)
        {
            return {false, "ERROR <top_k must be a positive number>\n"};
        }
        if (top_k_raw > MAX_TOP_K)
        {
            top_k_raw = MAX_TOP_K; // silently clamp
        }
        top_k = static_cast<size_t>(top_k_raw);
        // Check-03: parse and validate dims
        index = next_space_index + 1;
        next_space_changes(command, index, next_space_index, to_move);
        if (next_space_index == std::string::npos || to_move < 1)
        {
            return {false, "ERROR <Value of 'dims' not provided>\n"};
        }
        int dims_raw = std::stoi(command.substr(index, to_move));
        if (dims_raw < 1)
        {
            return {false, "ERROR <dims must be a positive number>\n"};
        }
        v.dims = static_cast<size_t>(dims_raw);
        // Check-04
        if (v.dims != dimensions_set)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }
        // Embeddings loop
        v.data.resize(dimensions_set);
        for (std::size_t i = 0; i < dimensions_set; i++)
        {
            index = next_space_index + 1;
            if (i != (dimensions_set - 1))
            {
                next_space_changes(command, index, next_space_index, to_move);
                //  Check-05
                if ((next_space_index == std::string::npos))
                {
                    return {false, "ERROR <Float values do not match the dimenstions>\n"};
                }
                v.data[i] = std::stof(command.substr(index, to_move));
            }
            else
            {
                if (command.size() >= index)
                    to_move = command.size() - index;
                v.data[i] = std::stof(command.substr(index, to_move));
            }
        }
        return {true, ""};
    }
    catch (const std::exception &)
    {
        return {false, "ERROR <Parsing failed: Non-numeric value encountered>\n"};
    }
}
Parse_result delete_parsing(std::string &id, const std::string &command)
{
    std::size_t index = 0,
                next_space_index = 0, to_move = 0;
    // Check-01: verify command starts with "DELETE"
    next_space_index = command.find(' ', 0);
    if (next_space_index != 6 or next_space_index == std::string::npos)
        return {false, "ERROR <Incorrect format for 'DELETE'>\n"};
    //
    index = next_space_index + 1; // 7
    // Check-02 verify format of 'id' and parse it
    to_move = command.size() - index;
    if (to_move < 1 or to_move > 32) // range 1-32
    {
        //  again we have not done anything yet, so nothing to reset
        if (to_move < 1)
        {
            return {false, "ERROR <Id size can not be zero>\n"};
        }
        else
        {
            return {false, "ERROR<Id size can not greater than 32>\n"};
        }
    }
    id = command.substr(index, to_move);
    return {true, ""};
}
Parse_result save_parsing(std::string &command, bool state)
{
    command.erase(std::remove(command.begin(), command.end(), ' '), command.end());
    int len = command.length();
    if (len != 4)
    {
        if (state == 0)
            return {false, "ERROR <Invalid format for SAVE>\n"};
        else
            return {false, "ERROR <Invalid format for LOAD>\n"};
    }
    return {true, ""};
}
bool Vector_store::read_all_ids(std::vector<std::string> &read_ids, const std::vector<std::size_t> &index, std::size_t &top_k)
{
    top_k = std::min(top_k, index.size()); // guard against index being too small
    read_ids.clear();
    read_ids.reserve(top_k);
    for (std::size_t i = 0; i < top_k; i++)
    {
        if (index[i] >= ids_.size())
            return false; // stale or invalid index
        read_ids.push_back(ids_[index[i]]);
    }
    return true;
}
//------------Helpers----------------
void next_space_changes(const std::string &command, const std::size_t &index, std::size_t &next_space_index, std::size_t &to_move)
{
    next_space_index = command.find(' ', index);
    to_move = next_space_index - index;
}
