
#ifndef TYPES
#define TYPES
#include <string>
#include <map>
#include <vector>
//--Data-Structures
struct Metadata_entry
{
    char key[32];
    char value[32];
};
struct Vector
{
    std::string id;
    int dims;
    // std::map<std::string, std::string> metadata;
    Metadata_entry metadata[3];
    std::vector<float> data;
};
struct Parse_result // Helper to identify success/failure of a function call/parse
{
    bool success;
    std::string message; // Error if failed.
};
struct Query_result // for vector_server use(only)
{
    float similarity;
    std::size_t index; // directly realted to database indexes
                       // to eaily sort the similar vectors
    bool operator>(const Query_result &other) const
    {
        return (this->similarity > other.similarity);
    }
};
#endif