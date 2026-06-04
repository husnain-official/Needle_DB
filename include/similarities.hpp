#ifndef SIMILARITIES
#define SIMILARITIES
// --- Similarity functions
/**
 * @brief Computes the normalized angular distance between two dense embedding arrays.
 * @param vec_a First floating-point embedding sequence
 * @param vec_b Second floating-point embedding sequence
 * @return Computed metric clamped between -1.0 and 1.0, or zero for null vectors
 * @warning Returns exactly zero if floating-point imprecision forces the calculation out of bounds
 * @note Caller must ensure both input sequences possess identical dimensionality to prevent undefined behaviour
 */
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
/**
 * @brief Computes the normalized angular distance against a raw contiguous memory block.
 * @param vec_a Reference floating-point embedding sequence
 * @param vec_b Raw pointer to a contiguous block of floating-point embedding data
 * @return Computed metric clamped between -1.0 and 1.0, or zero for null vectors
 * @warning Returns exactly zero if floating-point imprecision forces the calculation out of bounds
 * @note The raw array must contain at least as many elements as the reference vector
 */
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
/**
 * @brief Executes an unnormalized scalar projection using highly optimized inner products.
 * @param vec_a First floating-point embedding sequence
 * @param vec_b Second floating-point embedding sequence
 * @return Unbounded scalar representing the inner product of the inputs
 * @note Caller must ensure identical sequence lengths to avoid memory segmentation faults
 */
inline float dot_similarity(const std::vector<float> &vec_a, const std::vector<float> &vec_b)
{
    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b.begin(), 0.0f);
}
/**
 * @brief Executes an unnormalized scalar projection against a raw contiguous memory block.
 * @param vec_a Reference floating-point embedding sequence
 * @param vec_b Raw pointer to a contiguous block of floating-point embedding data
 * @return Unbounded scalar representing the inner product of the inputs
 * @note The raw array must contain at least as many elements as the reference vector
 */
inline float dot_similarity(const std::vector<float> &vec_a, const float *vec_b)
{
    return std::inner_product(vec_a.begin(), vec_a.end(), vec_b, 0.0f);
}

#endif