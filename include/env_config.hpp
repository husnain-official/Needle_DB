#ifndef ENV_CONFIG_HPP
#define ENV_CONFIG_HPP

#include <fstream>
#include <string>
#include <unordered_map>

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
// ----- Helper ------------------------------
// Helper to trim spaces, quotes, and carriage returns
inline std::string trimEnv(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\"\'\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\"\'\r\n");
    return str.substr(first, (last - first + 1));
}

/*
 * Reads the .env file and assigns the found values to the provided references.
 * Returns true if the file was opened successfully, false otherwise.
 */
// ----- To_call ------------------------------
/**
 * @brief Parses an environment file to populate global operational constraints.
 * @param envFilePath File system path targeting the plain text configuration file
 * @param con Configuration structure modified with successfully extracted values
 * @return True upon successful extraction and conversion, false otherwise
 * @warning Catches string-to-integer conversion exceptions internally and returns false silently
 * @note Unmapped keys present in the file are safely ignored without raising errors
 */
inline bool loadServerConfig(const std::string &envFilePath, Config &con)
{
    std::ifstream file(envFilePath);
    if (!file.is_open())
    {
        return false;
    }
    std::string line;
    std::unordered_map<std::string, std::string> envMap;

    // Parse file into map
    while (std::getline(file, line))
    {
        size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#')
            continue;

        size_t delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos)
        {
            std::string key = trimEnv(line.substr(firstNonSpace, delimiterPos - firstNonSpace));
            std::string value = trimEnv(line.substr(delimiterPos + 1));
            envMap[key] = value;
        }
    }
    // Assign to references, safely handling string-to-int conversions
    try
    {
        if (envMap.count("EMBEDDING_DIMS"))
            con.dims = std::stoul(envMap["EMBEDDING_DIMS"]);
        if (envMap.count("DIMENSIONS_NO_OF_DIGITS"))
            con.dims_no_of_digits = std::stoi(envMap["DIMENSIONS_NO_OF_DIGITS"]);
        if (envMap.count("ID_LENGTH_SET"))
            con.id_length = std::stoi(envMap["ID_LENGTH_SET"]);
        if (envMap.count("META_DATA_LENGTH_SET"))
            con.meta_data_length = std::stoi(envMap["META_DATA_LENGTH_SET"]);
        if (envMap.count("META_DATA_KP_PAIRS_SET"))
            con.meta_data_pairs = std::stoi(envMap["META_DATA_KP_PAIRS_SET"]);
        if (envMap.count("PORT"))
            con.port = (envMap["PORT"]);
        if (envMap.count("VECDB_DATA_PATH"))
            con.vecdb_file_path = envMap["VECDB_DATA_PATH"];
    }
    catch (...)
    {
        // Catch mapping or conversion errors (e.g. if a value is not a valid integer)
        return false;
    }
    return true;
}
#endif // ENV_CONFIG_HPP