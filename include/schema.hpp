/**
 * @file schema.hpp
 * @brief Core binary schema definitions, packed structures, and engine-wide operational constants.
 */
/*
    1. DE_entry and Vector both have many overlapping elements, but they are different as they are used for seperate purposes and places.
        o   DB_entry usage is limited to file_manager's internals.
        o   Vector is for vector_store's internals and other parts of the system.
*/
#pragma once
#include <cstdint>
#include <vector>
//  ----------------------------------------- Engine-Schema ------------------------------------------
/**
 * @brief Engine-wide operational constants and binary schema constraints.
 */
namespace schema
{
    /**
     * @brief Fixed number of floating-point elements in every vector embedding.
     */
    constexpr uint16_t DIMENSIONS = 1024;

    /**
     * @brief Exact character count expected when parsing the dimensions integer in client commands.
     */
    constexpr uint8_t DIMENSIONS_NO_OF_DIGITS = 4;

    /**
     * @brief Maximum length in bytes for a vector's unique string identifier.
     */
    constexpr uint8_t ID_LENGTH = 32;

    /**
     * @brief Maximum length in bytes for a single metadata key or value string.
     */
    constexpr uint8_t META_DATA_LENGTH = 32;

    /**
     * @brief Maximum number of key-value metadata pairs allowed per vector record.
     */
    constexpr uint8_t META_DATA_KP_PAIRS = 3;

    /**
     * @brief Maximum byte length allowed for an inserted text payload.
     */
    constexpr uint16_t TEXT_MAX_LENGTH = 999;

    /**
     * @brief Binary schema version identifier written to the persistent database header.
     */
    constexpr uint8_t VERSION = 6;

    /**
     * @brief 4-byte signature required at the beginning of a valid binary database file.
     */
    constexpr char MAGIC_NUMBER[4] = {'V', 'D', 'B', '\0'};

    /**
     * @brief Hard upper limit for the requested candidate match count in similarity queries.
     */
    constexpr uint8_t MAX_K_SIMILAR = 30;

    /**
     * @brief Target number of k-means clusters generated during IVF index construction.
     */
    constexpr uint8_t MAX_CENTROIDS = 100;

    /**
     * @brief Number of adjacent clusters evaluated during an IVF similarity search.
     */
    constexpr uint8_t MAX_PROBES_SEARCH = 5;

    /**
     * @brief Absolute minimum vector count required before the server evaluates optimization triggers.
     */
    constexpr uint16_t OPTIMIZE_REM_STARTS_AT = 500;

    /**
     * @brief Growth multiplier applied to the previous index build size to trigger a client optimization warning.
     */
    constexpr uint8_t OPTIMIZE_FACTOR = 2;

    /**
     * @brief Multiplier applied to the dead entry count to trigger automatic database compaction when the product exceeds the total record count.
     */
    constexpr uint8_t DELETE_FACTOR = 8;

    /**
     * @brief Maximum number of stored vectors sampled to seed centroids during an IVF index build.
     */
    constexpr uint32_t MAX_KMEANS_SAMPLE = 50000;
}

/**
 * @brief Defines system-wide operational constraints populated during startup.
 * @note Instances are passed by constant reference throughout the application lifecycle.
 */
struct Config
{
    /**
     * @brief Target network port string for binding incoming client connections.
     */
    std::string port = "8080";

    /**
     * @brief Target filesystem path for the persistent entry database file.
     */
    std::string vecdb_entry_file_path = "./data/database_entry.vdb";

    /**
     * @brief Target filesystem path for the persistent text payload database file.
     */
    std::string vecdb_text_file_path = "./data/database_text.vdb";

    /**
     * @brief Target filesystem path for the persistent IVF index topology file.
     */
    std::string vecdb_index_file_path = "./data/database_index.vdb";
};

//  ---------------------------------------- Data-Base-Schema ----------------------------------------
#pragma pack(push, 1) // No hidden padding! Keep the bytes explicit.

/**
 * @brief 32-byte packed binary schema definition for the persistent database file.
 * @note Strictly packed to 1-byte alignment to prevent cross-platform memory padding inconsistencies.
 */
struct DB_header
{
    //  |   8-byte types
    /**
     * @brief Count of active records currently residing in the database.
     */
    uint64_t live_vector_count = 0;

    /**
     * @brief Count of all records appended to the file, including soft-deleted tombstones.
     */
    uint64_t total_vector_count = 0;

    //  |   4-byte types
    /**
     * @brief 4-byte signature required at the beginning of a valid binary database file.
     */
    char magic_number[4] = {'V', 'D', 'B', '\0'};

    //  |   2-byte types
    /**
     * @brief Dimensionality of the vectors stored within this database file.
     */
    uint16_t dimensions = schema::DIMENSIONS;

    //  |   1-byte types
    /**
     * @brief Binary schema version identifier.
     */
    uint8_t version = schema::VERSION;

    /**
     * @brief Maximum length in bytes for a vector's unique string identifier.
     */
    uint8_t id_length = schema::ID_LENGTH;

    /**
     * @brief Maximum length in bytes for a single metadata key or value string.
     */
    uint8_t kv_length = schema::META_DATA_LENGTH;

    /**
     * @brief Maximum number of key-value metadata pairs allowed per vector record.
     */
    uint8_t max_kv = schema::META_DATA_KP_PAIRS;

    //  |   Pad to exactly 32 bytes (32 - 26 = 6 bytes)
    /**
     * @brief Unused padding bytes ensuring the struct size aligns exactly to 32 bytes.
     */
    uint8_t padding[6]{};
};
static_assert(sizeof(DB_header) == 32, "[schema.hpp]   |   Header layout mismatch.");

/**
 * @brief Fixed-size character arrays storing a single key-value string pair.
 * @warning Does not guarantee null termination if strings exactly match the schema length limit.
 */
struct Metadata_entry
{
    /**
     * @brief Fixed-size character array storing the metadata key.
     */
    char key[32]{};

    /**
     * @brief Fixed-size character array storing the metadata value.
     */
    char value[32]{};
};
static_assert(sizeof(Metadata_entry) == 64, "[schema.hpp]  |  Key-Value layout mismatch.");

/**
 * @brief Packed representation of a single vector record for persistent database storage.
 */
struct DB_entry
{
    /**
     * @brief Tombstone status flag indicating if the record is active (1) or soft-deleted (0).
     */
    uint8_t flag = 1;

    /**
     * @brief Fixed-size character array storing the vector's unique string identifier.
     */
    char id[schema::ID_LENGTH]{};

    /**
     * @brief Absolute byte offset in the text database file where the associated payload begins.
     */
    uint64_t text_offset = 0;

    /**
     * @brief Byte length of the associated text payload.
     */
    uint16_t text_length = 0;

    /**
     * @brief Array of structured key-value pairs storing associated metadata.
     */
    Metadata_entry meta_data[schema::META_DATA_KP_PAIRS]{};

    /**
     * @brief Number of active key-value metadata pairs populated in this record.
     */
    uint8_t meta_data_count = 0;

    /**
     * @brief Raw floating-point embedding sequence.
     */
    float embeddings[schema::DIMENSIONS]{};
};
static_assert(sizeof(DB_entry) == (sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint64_t) + (sizeof(char) * schema::ID_LENGTH) + (schema::DIMENSIONS) * sizeof(float) + (sizeof(Metadata_entry) * schema::META_DATA_KP_PAIRS)), "[schema.hpp]    |    Entry layout mismatch.");

/**
 * @brief Transient in-memory representation of a single vector record.
 */
struct Vector
{
    /**
     * @brief String storing the vector's unique identifier.
     */
    std::string id = std::string(schema::ID_LENGTH, '\0');

    /**
     * @brief Absolute byte offset in the text database file where the associated payload begins.
     */
    size_t text_offset = 0;

    /**
     * @brief Byte length of the associated text payload.
     */
    size_t text_length = 0;

    /**
     * @brief Array of structured key-value pairs storing associated metadata.
     */
    Metadata_entry meta_data[schema::META_DATA_KP_PAIRS]{};

    /**
     * @brief Number of active key-value metadata pairs populated in this record.
     */
    uint8_t meta_data_count = 0;

    /**
     * @brief Floating-point embedding sequence.
     */
    std::vector<float> embeddings = std::vector(schema::DIMENSIONS, 0.0f);
};
#pragma pack(pop)