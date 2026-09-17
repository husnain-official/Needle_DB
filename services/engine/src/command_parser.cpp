#include "command_parser.h"
//---------------------------- Parsing For 'Vector_Server' ----------------------------------
Parse_result Parser::insert_parsing(DB_entry &entry, std::string &extracted_text, const std::string &command)
{
    try
    {
        // Setup
        std::size_t index = 0, next_space_index = 0, to_move = 0;
        std::size_t chars_processed = 0;
        // Check-01 [INSERT]
        next_space_index = command.find(' ', 0);
        if (next_space_index != 6 or command.substr(0, 6) != "INSERT" or next_space_index == std::string::npos)
        {
            return {false, "ERROR <Incorrect format for 'INSERT'>\n"};
        }

        // Check-02 [Id]
        index = next_space_index + 1; // start of <id>
        next_space_changes(command, index, next_space_index, to_move);
        if ((to_move < 1 or to_move > schema::ID_LENGTH) or (next_space_index == std::string::npos)) // range 1-32
        {
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
                return {false, "ERROR <Id size can not greater than '" + std::to_string(schema::ID_LENGTH) + "' >\n"};
            }
        }
        std::string id = command.substr(index, to_move);
        memcpy(entry.id, id.data(), id.size());

        // Check-03 [text-length]
        index = next_space_index + 1; // start of <text_length>
        next_space_changes(command, index, next_space_index, to_move);
        if (to_move < 1)
        {
            return {false, "ERROR <Text size can not be 0 >\n"};
        }
        if (next_space_index == std::string::npos)
        {
            return {false, "ERROR <Incorrect format for 'INSERT'>\n"};
        }
        std::string text_length = command.substr(index, to_move);
        entry.text_length = static_cast<uint16_t>(std::stoi(text_length, &chars_processed));
        if (chars_processed != text_length.size())
            return {false, "ERROR <Trailing characters after text_length>\n"};
        if (entry.text_length > schema::TEXT_MAX_LENGTH)
        {
            return {false, "ERROR <Text size can not greater than '" + std::to_string(schema::TEXT_MAX_LENGTH) + "' >\n"};
        }

        // Check-04 [Text]
        index = next_space_index + 1; // start of <text>
        std::string text = command.substr(index, entry.text_length);
        if (index + entry.text_length > command.size() || command[index + entry.text_length] != ' ')
        {
            return {false, "ERROR <Text length mismatch or missing space>\n"};
        }
        extracted_text.resize(entry.text_length);
        memcpy(extracted_text.data(), text.data(), text.size());

        //  Check-05 [dimensions]
        index += entry.text_length + 1; // start of <dims>
        next_space_changes(command, index, next_space_index, to_move);
        if ((to_move != schema::DIMENSIONS_NO_OF_DIGITS) or (next_space_index == std::string::npos))
        {
            return {false, "ERROR <Incorrect 'Dimension' value entered>\n"};
        }
        std::string dims_str = command.substr(index, to_move);
        int dims = std::stoi(dims_str, &chars_processed);
        if (dims != schema::DIMENSIONS)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }
        if (chars_processed != dims_str.size())
            return {false, "ERROR <Trailing characters after dimensions>\n"};

        // Check-06 [Meta-Data]
        entry.meta_data_count = 0;
        while (entry.meta_data_count <= schema::META_DATA_KP_PAIRS and next_space_index != std::string::npos)
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
            if (entry.meta_data_count == schema::META_DATA_KP_PAIRS)
                return {false, "ERROR <Metadata pairs exceed schema value of '" + std::to_string(schema::META_DATA_KP_PAIRS) + "' >\n"};

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
            if (key.size() > schema::META_DATA_LENGTH)
            {
                return {false, "ERROR <Metadata key '" + key + "' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n"};
            }
            if (val.empty())
            {
                return {false, "ERROR <Metadata value for key '" + key + "' is empty>\n"};
            }
            if (val.size() > schema::META_DATA_LENGTH)
            {
                return {false, "ERROR <Metadata value for key '" + key + "' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n"};
            }

            std::strncpy(entry.meta_data[entry.meta_data_count].key, key.c_str(), schema::META_DATA_LENGTH);
            std::strncpy(entry.meta_data[entry.meta_data_count].value, val.c_str(), schema::META_DATA_LENGTH);
            entry.meta_data_count++;
        }

        // Check-07 [Embeddings loop]
        index = next_space_index + 1; // start of float values
        for (std::size_t i = 0; i < schema::DIMENSIONS; i++)
        {
            size_t idx = 0;
            if (i != (schema::DIMENSIONS - 1))
            {
                next_space_changes(command, index, next_space_index, to_move);
                //  Check-7.1
                if ((next_space_index == std::string::npos))
                {
                    return {false, "ERROR <Float values do not match the dimenstions>\n"};
                }
                std::string float_str = command.substr(index, to_move);
                entry.embeddings[i] = std::stof(float_str, &chars_processed);
                if (chars_processed != float_str.size())
                    return {false, "ERROR <Trailing characters in embeddings>\n"};

                index = next_space_index + 1; // start of next float
            }
            else
            {
                if (command.size() > index)
                    to_move = command.size() - index;
                else
                    return {false, "ERROR <Command string too short for given dimensions>\n"};

                std::string last_float_str = command.substr(index, to_move);
                entry.embeddings[i] = std::stof(last_float_str, &chars_processed);
                if (last_float_str.find_first_not_of(" \t\r\n", chars_processed) != std::string::npos)
                    return {false, "ERROR <Too many embeddings provided or trailing garbage>\n"};

                index = command.size();
            }
        }
        return {true, ""};
    }
    catch (const std::invalid_argument &)
    {
        return {false, "ERROR <Parsing failed: Expected a number but found non-numeric characters>\n"};
    }
    catch (const std::out_of_range &)
    {
        return {false, "ERROR <Parsing failed: Value out of range or string too short>\n"};
    }
    catch (const std::exception &e) // Catch-all for any other standard exceptions
    {
        return {false, std::string("ERROR <Unexpected error: ") + e.what() + ">\n"};
    }
}

Parse_result Parser::query_parsing(Vector &v, size_t &top_k, const std::string &command)
{
    try
    {
        // Setup
        std::size_t index = 0, next_space_index = 0, to_move = 0;
        int top_k_raw = 0;
        std::size_t chars_processed = 0;

        // Check-01 [QUERY]
        next_space_index = command.find(' ', 0);
        if (next_space_index != 5 or command.substr(0, 5) != "QUERY" or next_space_index == std::string::npos)
        {
            return {false, "ERROR <Incorrect format for 'QUERY'>\n"};
        }

        // Check-02 [Top_k]
        index = next_space_index + 1; // start of top_k
        next_space_changes(command, index, next_space_index, to_move);
        if (next_space_index == std::string::npos or to_move < 1)
        {
            return {false, "ERROR <Value of 'top_k' not provided>\n"};
        }
        std::string top_k_str = command.substr(index, to_move);
        top_k_raw = std::stoi(top_k_str, &chars_processed);
        if (chars_processed != top_k_str.size())
            return {false, "ERROR <Trailing characters in top_k similar value>\n"};
        if (top_k_raw < 1)
        {
            return {false, "ERROR <top_k must be a positive number>\n"};
        }
        if (top_k_raw > schema::MAX_K_SIMILAR)
        {
            top_k_raw = schema::MAX_K_SIMILAR; // silently clamp
        }
        top_k = static_cast<size_t>(top_k_raw);

        // Check-03 [Dimentions]
        index = next_space_index + 1; // start of dims
        next_space_changes(command, index, next_space_index, to_move);
        if (next_space_index == std::string::npos or to_move < 1)
        {
            return {false, "ERROR <Value of 'dims' not provided>\n"};
        }
        std::string dims_str = command.substr(index, to_move);
        int dims_raw = std::stoi(dims_str, &chars_processed);
        if (chars_processed != dims_str.size())
            return {false, "ERROR <Trailing characters in dimensions>\n"};
        if (dims_raw != schema::DIMENSIONS)
        {
            return {false, "ERROR <Invalid dimensions entered\n>"};
        }

        // Check-04 [Meta-Data]
        v.meta_data_count = 0;
        while (v.meta_data_count <= schema::META_DATA_KP_PAIRS and next_space_index != std::string::npos)
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
            if (v.meta_data_count == schema::META_DATA_KP_PAIRS)
                return {false, "ERROR <Metadata pairs exceed schema value of '" + std::to_string(schema::META_DATA_KP_PAIRS) + "' >\n"};

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
            if (key.size() > schema::META_DATA_LENGTH)
            {
                return {false, "ERROR <Metadata key '" + key + "' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n"};
            }
            if (val.empty())
            {
                return {false, "ERROR <Metadata value for key '" + key + "' is empty>\n"};
            }
            if (val.size() > schema::META_DATA_LENGTH)
            {
                return {false, "ERROR <Metadata value for key '" + key + "' exceeds '" + std::to_string(schema::META_DATA_LENGTH) + "' chars>\n"};
            }

            std::strncpy(v.meta_data[v.meta_data_count].key, key.c_str(), schema::META_DATA_LENGTH);
            std::strncpy(v.meta_data[v.meta_data_count].value, val.c_str(), schema::META_DATA_LENGTH);
            v.meta_data_count++;
        }

        // Check-05 [Embeddings loop]
        for (std::size_t i = 0; i < schema::DIMENSIONS; i++)
        {
            index = next_space_index + 1;
            if (i != (schema::DIMENSIONS - 1))
            {
                next_space_changes(command, index, next_space_index, to_move);
                if ((next_space_index == std::string::npos))
                {
                    return {false, "ERROR <Float values do not match the dimenstions>\n"};
                }
                std::string float_str = command.substr(index, to_move);
                v.embeddings[i] = std::stof(float_str, &chars_processed);
                if (chars_processed != float_str.size())
                    return {false, "ERROR <Trailing characters in embeddings>\n"};
            }
            else
            {
                if (command.size() > index)
                    to_move = command.size() - index;
                else
                    return {false, "ERROR <Command string too short for given dimensions>\n"};

                std::string last_float_str = command.substr(index, to_move);
                v.embeddings[i] = std::stof(last_float_str, &chars_processed);
                if (last_float_str.find_first_not_of(" \t\r\n", chars_processed) != std::string::npos)
                    return {false, "ERROR <Too many embeddings provided or trailing garbage>\n"};

                index = command.size();
            }
        }
        return {true, ""};
    }
    catch (const std::invalid_argument &)
    {
        return {false, "ERROR <Parsing failed: Expected a number but found non-numeric characters>\n"};
    }
    catch (const std::out_of_range &)
    {
        return {false, "ERROR <Parsing failed: Value out of range or string too short>\n"};
    }
    catch (const std::exception &e) // Catch-all for any other standard exceptions
    {
        return {false, std::string("ERROR <Unexpected error: ") + e.what() + ">\n"};
    }
}

Parse_result Parser::delete_parsing(std::string &id, const std::string &command)
{
    try
    {
        // Setup
        std::size_t index = 0, next_space_index = 0, to_move = 0;
        // Check-01 [Delete]
        next_space_index = command.find(' ', 0);
        if (next_space_index != 6 or command.substr(0, 6) != "DELETE" or next_space_index == std::string::npos)
            return {false, "ERROR <Incorrect format for 'DELETE'>\n"};
        // Check-02 [Id]
        index = next_space_index + 1;
        to_move = command.size() - index;
        if (to_move < 1 or to_move > schema::ID_LENGTH)
        {
            if (to_move < 1)
            {
                return {false, "ERROR <Id size can not be 0 >\n"};
            }
            else
            {
                return {false, "ERROR <Id size can not greater than '" + std::to_string(schema::ID_LENGTH) + "' >\n"};
            }
        }
        id = command.substr(index, to_move);
        return {true, ""};
    }
    catch (const std::out_of_range &)
    {
        return {false, "ERROR <Parsing failed: Value out of range or string too short>\n"};
    }
    catch (const std::exception &e) // Catch-all for any other standard exceptions
    {
        return {false, std::string("ERROR <Unexpected error: ") + e.what() + ">\n"};
    }
}

Parse_result Parser::save_parsing(std::string &command, bool state)
{
    try
    {
        const std::string expected = state ? "LOAD" : "SAVE";
        if (command.substr(0, 4) != expected)
            return {false, state ? "ERROR <Invalid format for LOAD>\n" : "ERROR <Invalid format for SAVE>\n"};
        return {true, ""};
    }
    catch (const std::exception &e) // Catch-all for any other standard exceptions
    {
        return {false, std::string("ERROR <Unexpected error: ") + e.what() + ">\n"};
    }
}

Parse_result Parser::optimize_parsing(std::string &command)
{
    try
    {
        if (command.substr(0, 8) != "OPTIMIZE")
            return {false, "ERROR <Invalid format for OPTIMIZE>\n"};
        return {true, ""};
    }
    catch (const std::exception &e)
    {
        return {false, std::string("ERROR <Unexpected error: ") + e.what() + ">\n"};
    }
}

// --- Helpers
void Parser::next_space_changes(const std::string &command, const std::size_t &index, std::size_t &next_space_index, std::size_t &to_move)
{
    /*
    <Hello This is me >
    <01234*6789*12*45*>
    Start:
    index = 0, next_space_index = 5,  to_move = 5
    index = 6, next_space_index = 10, to_move = 4
    index =11, next_space_index = 13, to_move = 2

    next_space_index -> index of next space.
    to_move          -> how many chars to copy starting from index.
    */
    next_space_index = command.find(' ', index);
    to_move = next_space_index - index;
}
