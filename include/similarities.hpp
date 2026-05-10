#ifndef SIMILARITIES
#define SIMILARITIES
// real reson for hpp, too lazy for individual .h and .cpp files
// --- Similarity functions
inline float cosine_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
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
inline float cosine_similarity(const std::vector<float> &vec_a, const float *vec_b)
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
inline float dot_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
{
    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b.begin(), 0.0f);
}
inline float dot_similarity(const std::vector<float> &vec_a, const float *vec_b)
{
    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b, 0.0f);
}

#endif