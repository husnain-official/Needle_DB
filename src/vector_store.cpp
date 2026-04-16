#include "vector_store.h"
//  For performance
bool Vector_store::normalise_vector(std::vector<float> &vec) // call before storing into file
{
    double long vec_mag = 0;
    for (size_t i = 0; i < vec.size(); i++)
        vec_mag += (vec[i] * vec[i]);
    vec_mag = sqrt(vec_mag);
    if (vec_mag == 0)
        return false;
    // divide each element with magnitude.
    for (size_t i = 0; i < vec.size(); i++)
        vec[i] = vec[i] / vec_mag;
    // Now, dot_similarity and cosine_similarity will give same restuls
    // we have removed the 'magnitude' of overlap and only 'direction' exist now
    return true;
}
//  similarity-functions
float cosine_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
{
    long double mag_a = 0;
    long double mag_b = 0;
    long double dot_product = 0;
    for (std::size_t i = 0; i < vec_a.size(); i++)
    {
        mag_a += (vec_a[i] * vec_a[i]);
        mag_b += (vec_b[i] * vec_b[i]);
        dot_product += (vec_a[i] * vec_b[i]);
    }
    mag_a = sqrtl(mag_a);
    mag_b = sqrtl(mag_b);
    if (mag_a == 0 or mag_b == 0) // in case of null vectors.
        return 0;
    float similarity = dot_product / (mag_a * mag_b);
    if (similarity < -1 or similarity > 1) // in case of wrong calcultion.
        return 0;
    return similarity;
}
float dot_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
{
    long double dot_product = 0;
    for (std::size_t i = 0; i < vec_a.size(); i++)
        dot_product += (vec_a[i] * vec_b[i]);
    float similarity = dot_product;
    return similarity;
}
// helper-functions
std::vector<float> &Vector_store::get_vector_data(size_t i)
{
    return store[i].data;
}
void Vector_store::insert(const Vector &v)
{
    store.push_back(v);
}
//  search
std::vector<std::pair<std::string, float>> Vector_store::brute_force_search(const std::vector<float> &query, const int top_n)
{
    std::vector<std::pair<std::string, float>> results; // will contains id's and similarities

    for (const Vector &v : store) // compare 'query' with each Vector
    {
        float similarity = cosine_similarity(query, v.data);
        results.push_back({v.id, similarity});
    }
    // sort the 'results' in desending order of 'cosine-similarity'
    std::sort(results.begin(), results.end(), [](const auto &x, const auto &y)
              { return (x.second > y.second); });
    if (results.size() > top_n)
        results.resize(top_n); // only keep 'n' pairs
    return results;
}
void Vector_store ::return_k_most_similar(const Vector &query_v, size_t top_k, std::vector<std::size_t> &return_index)
{
    // add a check to make sure that the database has been loaded, i.e all vectors are now in the ram
    size_t stored_vectors = store.size();
    std::vector<Query_result> results;
    float similarity;
    // calculate all similarities
    for (size_t i = 0; i < stored_vectors; i++)
    {
        similarity = dot_similarity(query_v.data, store[i].data);
        results.push_back({similarity, i});
    }
    // Partially sort so only the top 'top_k' elements are sorted at the front
    std::partial_sort(
        results.begin(),
        results.begin() + top_k,
        results.end(),
        [](const Query_result &a, const Query_result &b)
        {
            return a.similarity > b.similarity;
        });
    // returning only top_k
    for (size_t i = 0; i < top_k; i++)
        return_index.push_back(results[i].index);
}
//----------------------------Functionality For 'Vector_Server'----------------------------------
// add try-catch blocks in all of these(later).
// another probleam all errors are printed to the server and not the client {solved}
Parse_result insert_parsing(Vector &v, const std::string &command)
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
Parse_result query_parsing(Vector &v, size_t &top_k, const std::string &command)
{
    std::size_t index = 0,
                next_space_index = 0, to_move = 0;
    v.id = "";
    const size_t MAX_TOP_K = 30;
    const size_t MAX_TOP_K_DIGITS = 2;
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
//------------Helpers----------------
void next_space_changes(const std::string &command, const std::size_t &index, std::size_t &next_space_index, std::size_t &to_move)
{
    next_space_index = command.find(' ', index);
    to_move = next_space_index - index;
}
