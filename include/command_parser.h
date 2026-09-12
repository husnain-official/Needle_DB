/**
 * @file command_parser.h
 * @brief Component responsible for decoding raw client network strings into actionable engine operations.
 */
#pragma once
#include "vector_store.h" // For 'Parse_result'
#include "schema.hpp"
//---------------------------- Parsing For 'Vector_Server' ----------------------------------
/**
 * @brief Utility class for decoding line-delimited client network strings into structured database operations.
 */
class Parser
{
public:
    /**
     * @brief Parses a client 'INSERT' command string and populates a database record structure for storage.
     * @param entry Output record structure populated upon successful extraction.
     * @param extracted_text Output string populated with exactly the extracted text payload.
     * @param command Raw input command string to be parsed.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Expects the exact format: INSERT <id> <text_length> <text> <dims> [key=val ...] f1 f2 ... fn
     * @warning Fails securely if the dimensional count, string lengths, or metadata limits exceed configured schema constraints.
     */
    Parse_result insert_parsing(DB_entry &entry, std::string &extracted_text, const std::string &command);

    /**
     * @brief Parses a client 'QUERY' command string and populates a transient vector object for similarity searches.
     * @param v Output vector object populated with query metadata and target embeddings.
     * @param top_k Output reference populated with the requested maximum match count.
     * @param command Raw input command string to be parsed.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Expects the exact format: QUERY <top_k> <dims> [key=val ...] f1 f2 ... fn
     * @warning Silently clamps the requested match count to the schema maximum if the provided value is excessively large.
     */
    Parse_result query_parsing(Vector &v, size_t &top_k, const std::string &command);

    /**
     * @brief Parses a client 'DELETE' command string and extracts the target vector identifier.
     * @param id Output string populated with the extracted identifier.
     * @param command Raw input command string to be parsed.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Expects the exact format: DELETE <id>
     * @warning Does not verify if the extracted identifier actually exists within the storage engine.
     */
    Parse_result delete_parsing(std::string &id, const std::string &command);

    /**
     * @brief Parses a client 'SAVE' or 'LOAD' command string based on the expected state.
     * @param command Raw input command string to be parsed.
     * @param state Boolean flag distinguishing between SAVE (0) and LOAD (1) parsing modes.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Expects the exact format: SAVE or LOAD
     */
    Parse_result save_parsing(std::string &, bool);

    /**
     * @brief Parses a client 'OPTIMIZE' command string.
     * @param command Raw input command string to be parsed.
     * @return Carrier structure containing operation success status and a diagnostic message on failure.
     * @note Expects the exact format: OPTIMIZE
     */
    Parse_result optimize_parsing(std::string &);

private:
    // --- Helpers
    /**
     * @brief Scans forward to locate the next space delimiter and calculates the advancement stride.
     * @param command Raw input command string being parsed.
     * @param index Current cursor position within the string.
     * @param next_space_index Output reference populated with the found delimiter position.
     * @param to_move Output reference populated with the calculated substring length.
     * @warning Sets the delimiter index to std::string::npos if no matching character remains.
     */
    void next_space_changes(const std::string &, const std::size_t &, std::size_t &, std::size_t &);
};
