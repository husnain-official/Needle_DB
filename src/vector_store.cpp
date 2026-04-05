#include <iostream>
#include <math.h>
#include <algorithm>
#include "vector_store.h"
// using namespace std;

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
//  member-functions
std::vector<float> &Vector_store::get_vector_data(size_t i) { return store[i].data; }
void Vector_store::insert(const Vector &v)
{
    store.push_back(v);
}
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
