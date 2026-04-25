#include "file_manager.h"
// constructor
File_manager::File_manager(const std::string &path, uint32_t dims, uint8_t id_len, uint8_t kv_len, uint8_t kv_max_pairs)
{
    // Purpose: Boot-up the litteral 'file-manager'
    // First initilize the header
    header_.dimensions = dims;
    header_.id_length = id_len;
    header_.kv_length = kv_len;
    header_.max_kv = kv_max_pairs;
    memcpy(header_.magic_number, "VDB", 4);
    memset(header_.padding, 0, sizeof(header_.padding));
    header_.total_vector_count = 0;
    header_.live_vector_count = 0;
    header_.version = 3;
    // Initilize other elements
    record_size_ = 1 + header_.id_length + (sizeof(float) * header_.dimensions) + (header_.kv_length * (header_.max_kv * 2));
    // record_size_ = 1 + header_.id_length + (sizeof(float) * header_.dimensions) + (sizeof(Metadata_entry)); thiss will make it not work
    if (!(std::filesystem::exists(path)))
    {
        this->file_.open(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        if (!(file_.is_open()))
            throw std::runtime_error("Cannot create DB file: " + path);
        flush_header();
        std::cout << "Data-Base Created\n";
    }
    else
    {
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open())
        throw std::runtime_error("Cannot create DB file: " + path);
    header_ = read_header();
    if (std::strncmp(header_.magic_number, "VDB", 3) != 0)
    {
        throw std::runtime_error("Invalid file format: Magic number mismatch.");
    }
    if (header_.version != 3)
    {
        throw std::runtime_error("Schema mismatch: Incompatible database version. Expected Version 3.");
    }
    if (header_.dimensions != dims or header_.id_length != id_len or header_.kv_length != kv_len or header_.max_kv != kv_max_pairs)
    {
        throw std::runtime_error("Schema mismatch: File dimensions/ID length/Meta-data do not match provided arguments.");
    }
    std::cout << "Data-Base path opened successfully\n";
}
//  Header-Related-Functions
Header File_manager::read_header()
{
    Header h;
    file_.clear();
    file_.seekg(0);
    file_.read(reinterpret_cast<char *>(&h), sizeof(Header));
    return h;
}
bool File_manager::flush_header()
{
    file_.clear();
    // After any change/insersion update header in the database
    file_.seekp(0);
    file_.write(reinterpret_cast<char *>(&header_), sizeof(Header));
    file_.flush();
    return file_.good();
}
//  Helper-Functions/Getters
uint64_t File_manager::get_live_vector_count() const { return header_.live_vector_count; }
uint64_t File_manager::get_total_vector_count() const { return header_.total_vector_count; }
uint64_t File_manager::get_record_size() const { return record_size_; }
uint64_t File_manager::get_record_offset(uint64_t index) const { return (sizeof(Header) + index * record_size_); }
void File_manager::compact()
{
}
//  Core-Operations
bool File_manager::write_vector(const std::string &id, const float *embeddings, const Metadata_entry *mdata_arr) // .data() must be passed
{
    file_.clear();
    // 1.   move the write cursor to the eof
    // file_.seekp(get_record_offset(header_.total_vector_count));
    file_.seekp(0, std::ios::end);
    // 2.   intilize the flag
    char active_flag = 1;
    file_.write(&active_flag, 1);
    // 3.   WRITE:ID         add padding in case, id.length() < header_.id_length
    std::vector<char> id_padded(header_.id_length, '\0');
    size_t chars_to_copy = std::min(id.length(), static_cast<size_t>(header_.id_length));
    std::copy(id.begin(), id.begin() + chars_to_copy, id_padded.begin());
    file_.write(id_padded.data(), header_.id_length);
    // 4.   WRITE:META-DATA
    if (!mdata_arr)
    {
        Metadata_entry mdata = {0};
        for (int i = 0; i < header_.max_kv; i++)
            file_.write(reinterpret_cast<char *>(&mdata), header_.kv_length * 2);
    }
    else
    {
        Metadata_entry mdata;
        for (int i = 0; i < header_.max_kv; i++)
        {
            // i) add padding to the mdata
            memset(&mdata, 0, sizeof(mdata));
            // ii) measure source length
            size_t key_len = strnlen(mdata_arr[i].key, header_.kv_length);
            size_t val_len = strnlen(mdata_arr[i].value, header_.kv_length);
            // iii) copy data
            memcpy(mdata.key, mdata_arr[i].key, key_len);
            memcpy(mdata.value, mdata_arr[i].value, val_len);
            // iv) Write these values into the file
            file_.write(reinterpret_cast<char *>(mdata.key), header_.kv_length);
            file_.write(reinterpret_cast<char *>(mdata.value), header_.kv_length);
        }
    }
    // 5.   WRITE:EMBEDDINGS
    file_.write(reinterpret_cast<const char *>(embeddings), (sizeof(float) * header_.dimensions));
    // 5.   Cleanup
    if (!file_.good())
        return false;
    header_.total_vector_count++;
    header_.live_vector_count++;
    flush_header();
    return file_.good();
}
bool File_manager::read_vector(uint64_t index, std::string &id_out, float *data_out, Metadata_entry *mdata_arr)
{
    file_.clear();
    // 0.   Check
    if (index >= ((header_.total_vector_count)))
        return false;
    // 1.   We check the flag first, if deleted return immediately.
    file_.seekg(get_record_offset(index));
    char active_flag;
    file_.read(&active_flag, 1);
    if (active_flag == 0)
        return false;
    // 2.   Read the id, and prepare id_out
    std::vector<char> id(header_.id_length);
    file_.read(id.data(), header_.id_length);
    id_out.resize(header_.id_length, '\0');
    id_out.assign(id.data(), header_.id_length);
    size_t first_null = id_out.find('\0'); //      trim the extra entries of
    if (first_null != std::string::npos)
        id_out.resize(first_null);
    //  3.  Read the meta-data
    // i) necessary vars
    std::vector<char> key(header_.kv_length, '\0');
    std::vector<char> value(header_.kv_length, '\0');
    // ii) read
    if (!mdata_arr)
    {
        file_.seekg(header_.kv_length * 2 * header_.max_kv, std::ios::cur);
    }
    else
    {
        for (int i = 0; i < header_.max_kv; i++)
        {
            // Zero out the struct first to ensure clean padding
            std::memset(&mdata_arr[i], 0, sizeof(mdata_arr[i]));

            // Read directly from the file into the struct's memory
            file_.read(reinterpret_cast<char *>(mdata_arr[i].key), header_.kv_length);
            file_.read(reinterpret_cast<char *>(mdata_arr[i].value), header_.kv_length);
            // it is the callers responsibility to properly manage the C - string, as it may not have a null terminator
        }
    }
    //  4.  Read the embeddings
    file_.read(reinterpret_cast<char *>(data_out), sizeof(float) * header_.dimensions);
    //  5.  Return file state
    return file_.good();
}
bool File_manager::delete_vector(uint64_t index)
{
    file_.clear();
    // 0.   Check
    if (index >= ((header_.total_vector_count)))
        return false;
    // 1.   Move to intended Record
    file_.seekp(get_record_offset(index));
    // 2.   Change flag to 0
    char x = 0;
    file_.write(&x, 1);
    // 3.cleanup
    header_.live_vector_count--;
    file_.flush();
    flush_header();
    // 4.   Return file-state
    return file_.good();
}
int64_t File_manager::find_by_id(const std::string &id)
{
    // Prevent false matches if search ID is too long
    if (id.length() > header_.id_length)
        return -1;
    file_.clear();
    //     Loop through each 'Record' and compare ids
    std::vector<char> id_extracted(header_.id_length);
    for (uint64_t i = 0; i < header_.total_vector_count; i++)
    {
        file_.seekg(get_record_offset(i));

        char flag_state;
        // Ensure read was successful
        if (!file_.read(&flag_state, 1))
            break;

        // file_.read(&flag_state, 1);
        if (flag_state == 1)
        {
            file_.read(id_extracted.data(), header_.id_length);
            if (std::strncmp(id_extracted.data(), id.c_str(), std::min(id.length() + 1, (size_t)header_.id_length)) == 0)
                return i;
        }
    }
    return -1;
}
