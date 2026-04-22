#ifndef COMMAND_PARSER
#define COMMAND_PARSER
#include "vector_store.h"
//----------------------------Parsing For 'Vector_Server' ----------------------------------
Parse_result insert_parsing(Vector &, const std::string &);
Parse_result query_parsing(Vector &, size_t &, const std::string &);
Parse_result delete_parsing(std::string &, const std::string &);
Parse_result save_parsing(std::string &, bool);
//--helpers
void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
#endif