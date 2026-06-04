
#ifndef TYPES
#define TYPES
#include <string>
#include <map>
#include <vector>
// --- Data-Structures
/**
 * @brief Fixed-size character arrays storing a single key-value string pair.
 * @note Maximum length for both key and value is 32 bytes.
 * @warning Does not guarantee null termination if strings exactly match the 32-byte limit.
 */
struct Metadata_entry
{
    char key[32];
    char value[32];
};
/**
 * @brief Core memory representation of an embedding and its associated metadata.
 * @note Supports a strict maximum of 3 metadata entries per record.
 */
struct Vector
{
    std::string id;
    int dims;
    Metadata_entry metadata[3];
    std::vector<float> data;
};
/**
 * @brief Carrier structure for operation outcomes and diagnostic text.
 */
struct Parse_result
{
    bool success;
    std::string message; // Error if failed.
};
/**
 * @brief Transient record pairing a computed similarity score with its internal memory offset.
 */
struct Query_result
{
    float similarity;
    std::size_t index; // directly realted to database indexes
                       // to eaily sort the similar vectors
    /**
     * @brief Compares two search results based strictly on their computed similarity scores.
     * @param other Target result instance to compare against
     * @return True if the left operand has a strictly greater similarity score
     */
    bool operator>(const Query_result &other) const
    {
        return (this->similarity > other.similarity);
    }
};
#endif