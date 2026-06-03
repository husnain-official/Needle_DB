#include "command_parser.h"
// --- Helpers
void next_space_changes(const std::string &command, const std::size_t &index, std::size_t &next_space_index, std::size_t &to_move)
{
    next_space_index = command.find(' ', index);
    to_move = next_space_index - index;
}
void next_equal_changes(const std::string &command, const std::size_t &index, std::size_t &next_space_index, std::size_t &to_move)
{
    next_space_index = command.find('=', index);
    to_move = next_space_index - index;
}
//---------------------------- Parsing For 'Vector_Server' ----------------------------------
Parse_result insert_parsing(Vector &v, const std::string &command, const Config con)
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
        if ((to_move < 1 or to_move > con.id_length) or (next_space_index == std::string::npos)) // range 1-32
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
        if ((to_move != con.dims_no_of_digits) or (next_space_index == std::string::npos))
        {
            //  we should reset v1.id but unnecessary
            return {false, "ERROR <Incorrect 'Dimension' value entered>\n"};
        }
        v.dims = (std::stoi(command.substr(index, to_move)));
        // Check-04
        if (v.dims != con.dims)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }
        index = next_space_index + 1; //  now standing at the first char of the 1st key
        //  META-DATA new PARSING
        //  Psudo-code
        //  make a loop -> check if a '=' sign is found in the next 32chars(excluding the 1st one,where we currently stand)
        //  -> if it does find its index -> take your current position and that index and [ , ) into a substr, thats your key
        //  -> now find the index of next space ' ' and the data between [ , ) into a substr, thats your value
        //  -> repeat the loop for the set fixed kv pairs, PROBLEM -> command_parser.cpp is a stand alone file, it does not know preset
        //  constants
        //  index right now is first char of either metadata or float, if no metadat
        constexpr int max_kv = sizeof(v.metadata) / sizeof(v.metadata[0]);
        std::string temp_str;
        memset(v.metadata, 0, sizeof(v.metadata));
        for (int i = 0; i < max_kv; i++)
        {
            size_t lookahead = command.find('=', index);
            if (lookahead == std::string::npos || lookahead - index > 32)
            {
                break; // no more metadata, floats start here
            }
            next_equal_changes(command, index, next_space_index, to_move);
            if (to_move < 1)
            {
                return {false, "ERROR<Meta-data key size is 0>\n"};
            }
            if (to_move > 32)
            {
                return {false, "ERROR<Meta-data key size exceeds 32>\n"};
            }
            temp_str = command.substr(index, to_move);
            memset(v.metadata[i].key, 0, sizeof(v.metadata[i].key));
            memcpy(v.metadata[i].key, temp_str.data(), to_move);
            // do the same for value
            index = next_space_index + 1;
            next_space_changes(command, index, next_space_index, to_move);
            if (next_space_index == std::string::npos)
            {
                return {false, "ERROR<Incorrect Meta-Data format>\n"};
            }
            if (to_move < 1)
            {
                return {false, "ERROR<Meta-data value size is 0>\n"};
            }
            if (to_move > 32)
            {
                return {false, "ERROR<Meta-data value size exceeds 32>\n"};
            }
            temp_str = command.substr(index, to_move);
            memset(v.metadata[i].value, 0, sizeof(v.metadata[i].value));
            memcpy(v.metadata[i].value, temp_str.data(), to_move);
            index = next_space_index + 1;
            //
        }
        // Embeddings loop
        v.data.resize(con.dims);
        for (std::size_t i = 0; i < con.dims; i++)
        {
            if (i != (con.dims - 1))
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
                if (command.size() > index)
                    to_move = command.size() - index;
                else
                    return {false, "ERROR <Command string too short for given dimensions>\n"};

                v.data[i] = std::stof(command.substr(index, to_move));
            }
            index = next_space_index + 1;
        }
        return {true, ""};
    }
    catch (const std::exception &)
    {
        return {false, "ERROR <Parsing failed: Non-numeric value encountered>\n"};
    }
}
Parse_result query_parsing(Vector &v, size_t &top_k, const std::string &command, const Config con)
{
    try
    {
        // Setup
        std::size_t index = 0, next_space_index = 0, to_move = 0;
        v.id = "";
        const size_t MAX_TOP_K = 30;
        int top_k_raw = 0;
        // Zero-initialise all metadata slots upfront
        for (size_t m = 0; m < con.meta_data_pairs; m++)
        {
            v.metadata[m].key[0] = '\0';
            v.metadata[m].value[0] = '\0';
        }
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
        if (v.dims != con.dims)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }

        size_t meta_count = 0;
        while (meta_count < con.meta_data_pairs && next_space_index != std::string::npos)
        {
            // Peek at the next token without committing index yet
            std::size_t peek_start = next_space_index + 1;
            std::size_t peek_end = command.find(' ', peek_start);
            std::size_t peek_len = (peek_end == std::string::npos) ? command.size() - peek_start : peek_end - peek_start;

            if (peek_len == 0)
                break;
            std::string token = command.substr(peek_start, peek_len);

            // No '=' means this token is a float — leave index/next_space_index; where they are so the embedding loop picks up correctly
            if (token.find('=') == std::string::npos)
                break;

            // Commit: advance past this token
            index = peek_start;
            next_space_index = peek_end;
            to_move = peek_len;

            // Validate: exactly one '='
            size_t eq_pos = token.find('=');
            if (token.find('=', eq_pos + 1) != std::string::npos)
            {
                return {false, "ERROR <Metadata token '" + token + "' has multiple '='>\n"};
            }

            std::string key = token.substr(0, eq_pos);
            std::string val = token.substr(eq_pos + 1);

            if (key.empty())
            {
                return {false, "ERROR <Metadata key is empty>\n"};
            }
            if (key.size() > con.meta_data_length)
            {
                return {false, "ERROR <Metadata key '" + key + "' exceeds 32 chars>\n"};
            }
            if (val.empty())
            {
                return {false, "ERROR <Metadata value for key '" + key + "' is empty>\n"};
            }
            if (val.size() > con.meta_data_length)
            {
                return {false, "ERROR <Metadata value for key '" + key + "' exceeds 32 chars>\n"};
            }

            std::strncpy(v.metadata[meta_count].key, key.c_str(), con.meta_data_length);
            std::strncpy(v.metadata[meta_count].value, val.c_str(), con.meta_data_length);
            // this is handeled by, write vector(DISK) and make entry(RAM)
            // v.metadata[meta_count].key[meta_data_length_set] = '\0';
            // v.metadata[meta_count].value[meta_data_length_set] = '\0';

            meta_count++;
        }
        // Embeddings loop
        v.data.resize(con.dims);
        for (std::size_t i = 0; i < con.dims; i++)
        {
            index = next_space_index + 1;
            if (i != (con.dims - 1))
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
Parse_result delete_parsing(std::string &id, const std::string &command, const Config con)
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
Parse_result save_parsing(std::string &command, bool state, const Config con)
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
