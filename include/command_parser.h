#ifndef COMMAND_PARSER
#define COMMAND_PARSER
#include "vector_store.h" // For Vector Struct
#include "env_config.hpp" // For Config Struct
//---------------------------- Parsing For 'Vector_Server' ----------------------------------
Parse_result insert_parsing(Vector &, const std::string &, const Config);
Parse_result query_parsing(Vector &, size_t &, const std::string &, const Config);
Parse_result delete_parsing(std::string &, const std::string &, const Config);
Parse_result save_parsing(std::string &, bool, const Config);
// --- Helpers
void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
void next_equal_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
#endif