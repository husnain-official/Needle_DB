#include "vector_store.h"
#include "similarities.hpp"
//--- math and similarity functions ---
bool Vector_store::normalise_vector(std::vector<float> &vec)
{ // called before each query/save into the database, makes dot-product math easier(magnitude of each vector is 1)
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
    // we have removed the 'magnitude' of overlap and only 'direction' exists now
    return true;
}
//--- Vector_store implementation
// getters
const float *Vector_store::get_embedding(size_t i) const
{
    return (embeddings_.data() + (i * dims_));
}
const std::string &Vector_store::get_id(size_t i) const
{
    return ids_[i];
}
const std::size_t Vector_store::get_dims() const { return dims_; }
const std::size_t Vector_store::get_count() const { return count_; }
Parse_result Vector_store::get_index_in_ram(const std::string &id)
{
    for (size_t i = 0; i < count_; i++)
    {
        if (ids_[i] == id)
            return {true, std::to_string(i)};
    }
    return {false, "ERROR<Id not found in Memory>\n"};
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
bool Vector_store::remove_entry(const std::string &id)
{
    // Parse_result p;          .. Method-01
    // p = get_index_in_ram(id);
    // if (!p.success)
    //     return false;
    // std::size_t index = std::stoi(p.message);
    // // remove
    // ids_.erase(ids_.begin() + index);
    // embeddings_.erase(embeddings_.begin() + index, embeddings_.begin() + index + dimensions_set);
    // count_--;
    // return true;             .. Method-02
    Parse_result p = get_index_in_ram(id);
    if (!p.success)
        return false;

    size_t target = std::stoi(p.message);
    size_t last = count_ - 1;

    // 1. Swap ids
    std::swap(ids_[target], ids_[last]);

    // 2. Swap embedding blocks in the flat array
    float *target_block = embeddings_.data() + (target * dims_);
    float *last_block = embeddings_.data() + (last * dims_);
    std::swap_ranges(target_block, target_block + dims_, last_block);

    // 3. Pop the last entry
    ids_.pop_back();
    embeddings_.resize(embeddings_.size() - dims_);
    count_--;
    return true;
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
        // float similarity = cosine_similarity(query, get_embedding(i));
        float similarity = dot_similarity(query, get_embedding(i));
        results.push_back({get_id(i), similarity});
    }
    // sort the 'results' in desending order of 'cosine-similarity'
    std::sort(results.begin(), results.end(), [](const auto &x, const auto &y)
              { return (x.second > y.second); });
    if (results.size() > top_n)
        results.resize(top_n); // only keep 'n' pairs
    return results;
}
void Vector_store ::return_k_most_similar(const Vector &query_v, size_t &top_k, std::vector<std::size_t> &return_index, std::vector<float> &similarities)
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
