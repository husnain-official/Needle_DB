#ifndef COMMAND_PARSER
#define COMMAND_PARSER
#include "vector_store.h" // For Vector Struct
#include "env_config.hpp" // For Config Struct
//---------------------------- Parsing For 'Vector_Server' ----------------------------------
/**
 * @brief Extracts identifier, metadata, and embedding components from a raw insertion command string.
 * @param v Vector instance populated upon successful extraction
 * @param command Raw newline-terminated text string from the client
 * @param con Immutable environment constraints for bounds checking
 * @return Success boolean paired with a diagnostic message on failure
 * @note Expects the exact format: INSERT <id> <dims> [key=val ...] f1 f2 ... fn
 * @warning Fails securely if the dimensional count or string lengths exceed configured limits
 */
Parse_result insert_parsing(Vector &, const std::string &, const Config);
/**
 * @brief Decodes network queries into actionable search constraints and target embeddings.
 * @param v Vector struct populated with query metadata and floats
 * @param top_k Output reference for the requested maximum match count
 * @param command Raw newline-terminated text string
 * @param con Immutable environment constraints
 * @return Status object indicating structural validity of the protocol text
 * @note Limits metadata constraints strictly to the configured maximum pairs
 * @warning Silently clamps top_k to a hardcoded maximum if the requested value is excessively large
 */
Parse_result query_parsing(Vector &, size_t &, const std::string &, const Config);
/**
 * @brief Extracts a target vector identifier from a client deletion command.
 * @param id Output string populated with the extracted identifier
 * @param command Raw network string containing the deletion request
 * @param con Configured system constraints
 * @return Success status paired with an empty string or error diagnostic
 * @warning Does not verify if the extracted identifier actually exists within the storage engine
 */
Parse_result delete_parsing(std::string &, const std::string &, const Config);
/**
 * @brief Validates syntax for explicit persistence synchronization or memory reload commands.
 * @param command Raw input string directly from the socket buffer
 * @param state Boolean flag distinguishing between SAVE (0) and LOAD (1) parsing modes
 * @param con Global configurations
 * @return Struct containing operation success status
 * @warning Mutates the input command string by stripping all whitespace characters internally
 */
Parse_result save_parsing(std::string &, bool, const Config);
// --- Helpers
/**
 * @brief Scans forward to locate the next space delimiter and calculates the advancement stride.
 * @param command Target string being parsed
 * @param index Current cursor position within the string
 * @param next_space_index Output reference updated to the found delimiter position
 * @param to_move Output reference tracking the substring length
 * @warning Sets the delimiter index to std::string::npos if no matching character remains
 */
void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
/**
 * @brief Scans forward to locate the next assignment delimiter and calculates the advancement stride.
 * @param command Target string being parsed
 * @param index Current cursor position within the string
 * @param next_space_index Output reference updated to the found equals sign position
 * @param to_move Output reference tracking the substring length
 * @warning Sets the delimiter index to std::string::npos if no matching character remains
 */
void next_equal_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
#endif