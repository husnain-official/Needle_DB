#include "vector_store.h"
//--- math and similarity functions ---
bool Vector_store::normalise_vector(std::vector<float> &vec)
{ // called before each save into the database, makes dot-product math easier(magnitude of each vector is 1)
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
    return (similarity > 1 or similarity < -1) ? 0 : similarity;
}
float cosine_similarity(const std::vector<float> &vec_a, const float *vec_b)
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
    return (similarity > 1 or similarity < -1) ? 0 : similarity;
}
float dot_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
{

    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b.begin(), 0.0f);
    // long double dot_product = 0;             // use the optimised function
    // for (std::size_t i = 0; i < vec_a.size(); i++)
    //     dot_product += (vec_a[i] * vec_b[i]);
    // float similarity = dot_product;
    // return similarity;
}
float dot_similarity(const std::vector<float> &vec_a, const float *vec_b)
{
    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b, 0.0f);
}

//  Vector_store implementation
// getters
const float *Vector_store::get_embedding(size_t i) const
{
    return (embeddings_.data() + (i * dims_));
}
const std::string &Vector_store::get_id(size_t i) const
{
    return ids_[i];
}
// setters
Parse_result Vector_store::set_dims_(const std::size_t dim)
{
    if (dim != dimensions_set)
        return {false, "ERROR <Mismatch of dimensions in file and in program set value>\n"};
    dims_ = dim;
    return {true, ""};
}
Parse_result Vector_store::set_count_(const int count)
{
    if (count < 0)
        return {false, "ERROR <File corrupted, count read is negative>\n"};
    this->count_ = count;
    return {true, ""};
}
void Vector_store::clear()
{
    ids_.clear();
    embeddings_.clear();
    count_ = 0;
    ids_.reserve(count_);
    embeddings_.reserve(count_ * dims_);
}
void Vector_store::make_entry(const std::string id_buf, std::vector<float> embd_buf)
{
    ids_.push_back(id_buf);
    embeddings_.insert(embeddings_.end(), embd_buf.begin(), embd_buf.end());
    count_++;
}
bool Vector_store::insert(const Vector &v)
{
    if (v.data.size() != dims_)
        return false;
    embeddings_.insert(embeddings_.end(), v.data.begin(), v.data.end());
    ids_.push_back(v.id);
    count_++;
    return true;
}
//  search
std::vector<std::pair<std::string, float>> Vector_store::brute_force_search(const std::vector<float> &query, const int top_n)
{
    std::vector<std::pair<std::string, float>> results; // will contains id's and similarities

    for (size_t i = 0; i < count_; i++) // compare 'query' with each Vector
    {
        float similarity = cosine_similarity(query, get_embedding(i));
        results.push_back({get_id(i), similarity});
    }
    // sort the 'results' in desending order of 'cosine-similarity'
    std::sort(results.begin(), results.end(), [](const auto &x, const auto &y)
              { return (x.second > y.second); });
    if (results.size() > top_n)
        results.resize(top_n); // only keep 'n' pairs
    return results;
}
void Vector_store ::return_k_most_similar(const Vector &query_v, size_t top_k, std::vector<std::size_t> &return_index, std::vector<float> &similarities)
{
    // add a check to make sure that the database has been loaded, i.e all vectors are now in the ram
    size_t stored_vectors = count_;
    std::vector<Query_result> results;
    results.reserve(stored_vectors);
    float similarity;
    // calculate all similarities
    for (size_t i = 0; i < stored_vectors; i++)
    {
        similarity = dot_similarity(query_v.data, get_embedding(i));
        results.push_back({similarity, i});
    }
    top_k = std::min(top_k, count_);
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
    {
        return_index.push_back(results[i].index);
        similarities.push_back(results[i].similarity);
    }
    //
}
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
bool Vector_store::read_all_ids(std::vector<std::string> &read_ids, const std::vector<std::size_t> &index, std::size_t top_k)
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
