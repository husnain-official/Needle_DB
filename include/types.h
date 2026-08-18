
#ifndef TYPES
#define TYPES
#include <string>
#include <map>
#include <vector>
#include "schema.hpp"
// --- Data-Structures

// ----- Config-Stuct -------------------------
/**
 * @brief Defines system-wide operational constraints populated during startup.
 * @note Instances are passed by constant reference throughout the application lifecycle.
 */
struct Config
{
    std::string port = "8080";
    std::string vecdb_file_path = "./data/database.vdb";
    std::size_t dims = 1024;
    std::size_t dims_no_of_digits = 4;
    std::size_t id_length = 32;
    std::size_t meta_data_length = 32;
    std::size_t meta_data_pairs = 3;
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