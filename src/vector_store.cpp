#include "vector_store.h"
#include "ivf.h"
//--- math and similarity functions ---
bool Vector_store::normalise_vector(std::vector<float> &vec)
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
    // we have removed the 'magnitude' of overlap and only 'direction' exists now. Cosine and Dot product same results now.
    return true;
}
//--- Vector_store implementation
// 1. getters
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
Parse_result Vector_store::get_matching_indices(const Metadata_entry *mdata_arr, std::vector<size_t> &matching_indices)
{
    try
    {
        matching_indices.clear();

        // Count how many query pairs are actually set (non-empty key)
        size_t query_pair_count = 0;
        for (size_t m = 0; m < conditions.meta_data_pairs; m++)
        {
            if (mdata_arr[m].key[0] != '\0')
                query_pair_count++;
        }

        // No metadata in query — every index is a candidate
        if (query_pair_count == 0)
        {
            matching_indices.resize(count_);
            std::iota(matching_indices.begin(), matching_indices.end(), 0);
            return {true, ""};
        }

        for (size_t i = 0; i < count_; i++)
        {
            const std::string &id = get_id(i);

            // Vector has no metadata stored — cannot satisfy any query pair
            auto vec_meta_it = metadata_.find(id);
            if (vec_meta_it == metadata_.end())
                continue;

            const std::map<std::string, std::string> &vec_pairs = vec_meta_it->second;

            // Every non-empty query pair must exist with a matching value
            bool all_match = true;
            for (size_t m = 0; m < conditions.meta_data_pairs; m++)
            {
                if (mdata_arr[m].key[0] == '\0')
                    continue; // unused slot, skip

                std::string qkey(mdata_arr[m].key);
                std::string qval(mdata_arr[m].value);

                auto pair_it = vec_pairs.find(qkey);
                if (pair_it == vec_pairs.end() || pair_it->second != qval)
                {
                    all_match = false;
                    break;
                }
            }

            if (all_match)
                matching_indices.push_back(i);
        }

        return {true, ""};
    }
    catch (const std::exception &e)
    {
        return {false, std::string("ERROR <get_matching_indices failed: ") + e.what() + ">\n"};
    }
}
Vector_index *Vector_store::get_index() const { return index_; }
// 2. setters
Parse_result Vector_store::set_dims_(const std::size_t dim)
{
    if (dim != conditions.dims)
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
void Vector_store::set_metadata(const Metadata_entry *mdata_arr, const std::string &id)
{
    if (!mdata_arr)
        return;
    //  setup the inner map
    std::map<std::string, std::string> inner_key_val;
    for (int i = 0; i < conditions.meta_data_pairs; i++)
    {
        // skip empty key/values
        if (mdata_arr[i].key[0] == '\0')
            continue;
        // overflow prevention
        size_t key_len = strnlen(mdata_arr[i].key, conditions.meta_data_length);
        size_t val_len = strnlen(mdata_arr[i].value, conditions.meta_data_length);
        //  safe-strings
        std::string safe_key(mdata_arr[i].key, key_len);
        std::string safe_val(mdata_arr[i].value, val_len);
        //
        inner_key_val[safe_key] = safe_val;
    }
    // now setup the outer map
    metadata_[id] = std::move(inner_key_val);
    return;
}
void Vector_store::clear()
{
    ids_.clear();
    embeddings_.clear();
    metadata_.clear();
    count_ = 0;
    ids_.reserve(count_);
    embeddings_.reserve(count_ * dims_);
}
void Vector_store::make_entry(const std::string id_buf, std::vector<float> embd_buf, const Metadata_entry *mdata_arr)
{
    ids_.push_back(id_buf);
    embeddings_.insert(embeddings_.end(), embd_buf.begin(), embd_buf.end());
    set_metadata(mdata_arr, id_buf);
    count_++;
    if (index_)
        index_->add_(count_ - 1);
}
bool Vector_store::remove_entry(const std::string &id)
{
    Parse_result p = get_index_in_ram(id);
    if (!p.success)
        return false;

    size_t target = std::stoi(p.message);
    size_t last = count_ - 1;
    // 1. Remove both entries from IVF
    if (index_)
    {
        index_->delete_(target);
        if (target != last)
            index_->delete_(last);
    }

    // 2.1. Swap data
    std::swap(ids_[target], ids_[last]);

    // 2.2. Swap embedding blocks in the flat array
    float *target_block = embeddings_.data() + (target * dims_);
    float *last_block = embeddings_.data() + (last * dims_);
    std::swap_ranges(target_block, target_block + dims_, last_block);

    // 3. Pop the last entry
    ids_.pop_back();
    embeddings_.resize(embeddings_.size() - dims_);

    // 4. Delete metadata
    metadata_.erase(id);
    count_--;

    if (index_ && target != last)
        index_->add_(target);
    return true;
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
void Vector_store::attach_index(Vector_index *idx) { index_ = idx; }
//  3. search
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
void Vector_store::return_k_most_similar(const Vector &query_v, size_t &top_k, std::vector<std::size_t> &return_index, std::vector<float> &similarities, std::vector<size_t> *selected_indexes)
{
    // add a check to make sure that the database has been loaded, i.e all vectors are now in the ram
    size_t stored_vectors = count_;
    std::vector<Query_result> results;
    results.reserve(stored_vectors);
    float similarity;
    // calculate all similarities
    if (selected_indexes != nullptr && !selected_indexes->empty())
    {
        for (size_t i : *selected_indexes)
        {
            similarity = dot_similarity(query_v.data, get_embedding(i));
            results.push_back({similarity, i});
        }
    }
    else
    {
        for (size_t i = 0; i < stored_vectors; i++)
        {
            similarity = dot_similarity(query_v.data, get_embedding(i));
            results.push_back({similarity, i});
        }
    }
    top_k = std::min(top_k, results.size());
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
}
