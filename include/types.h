#ifndef TYPES
#define TYPES
#include <string>
#include <map>
#include <vector>
#include "schema.hpp"
// --- Data-Structures
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
    std::size_t index; // directly realted to database(RAM or DISK ?) indexes
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

// --- Conversion-Function
inline bool entry_to_vector(const DB_entry &entry, Vector &vec)
{
    try
    {
        //  ID:
        size_t id_size = strnlen(entry.id, schema::ID_LENGTH);
        vec.id = std::string(entry.id, id_size);
        //  Text length:
        vec.text_length = entry.text_length;
        vec.text_offset = entry.text_offset;
        //  Meta-Data:
        vec.meta_data_count = entry.meta_data_count;
        for (size_t i = 0; i < entry.meta_data_count; ++i)
        {
            // Struct assignment automatically and safely copies the internal char[32] arrays
            vec.meta_data[i] = entry.meta_data[i];
        }
        // Embeddings:
        std::copy(entry.embeddings, entry.embeddings + schema::DIMENSIONS, vec.embeddings.begin());
        return true;
    }
    catch (const std::exception &e)
    {
        return false;
    }
}

#endif // TYPES