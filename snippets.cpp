// // {
// // #include "vector_store.h"
// // #include "file_manager.h"
// // #include "vector_server.h"
// // #include "../tests/integration/json.hpp"
// // using namespace std;
// // void next_space_changes(string &command, const size_t &index, size_t &next_space_index, size_t &to_move)
// // {
// //     next_space_index = command.find(' ', index);
// //     to_move = next_space_index - index;
// // }
// // int main()
// // {
// //     // {
// //     // cout << "VectorDB   starting...\n";
// //     // // ----------------------Declare All Components-------------------------
// //     // Vector_store database;
// //     // //
// //     // std::string path = "../data/database.vdb";
// //     // File_manager file_handler(path, dimenstions_set, id_length_set);
// //     // //
// //     // std::string port = "8080";
// //     // Vector_Server server(port, database, file_handler);
// //     // //----------------------------Main Body---------------------------------
// //     // server.setup();
// //     // server.run();
// //     // server.stop();
// //     // //----------------------------Program End-------------------------------
// //     // }

// //     size_t id_size = 32,
// //            dimensions = 5,
// //            dimensions_no_of_digitss = 1;
// //     Vector v1;
// //     string command = "INSERT Chunk-1......................... 5 1.21 2.12321 232.2132 2.1223 0.00001";
// //     //
// //     if ((command.rfind("INSERT")) == 0)
// //     {
// //         // Declaration of necessry variables
// //         // Vector v1;
// //         size_t index = 0, next_space_index = 0, to_move = 0;
// //         // Check-01
// //         next_space_index = command.find(' ', 0);
// //         if (next_space_index != 6 or next_space_index == std::string::npos)
// //         {
// //             // we have not done anything yet, so nothing to reset
// //             cout << "ERROR<Incorrect format for 'INSERT'>\n";
// //             // continue;
// //         }
// //         index = next_space_index + 1; // 7
// //         next_space_changes(command, index, next_space_index, to_move);
// //         // Check-02
// //         if ((to_move < 1 or to_move > 32) or (next_space_index == std::string::npos)) // range 1-32
// //         {
// //             //  again we have not done anything yet, so nothing to reset
// //             if (to_move == 100)
// //                 cout << "ERROR<Incorrect format for 'INSERT'>\n";
// //             // continue;
// //             else if (to_move < 1)
// //                 cout << "ERROR<Id size can not be zero>\n";
// //             // continue;
// //             else
// //                 cout << "ERROR<Id size can not greater than 32>\n";
// //             // continue;
// //         }
// //         v1.id = command.substr(index, to_move);
// //         index = next_space_index + 1; // 40(if to_move was 33)
// //         next_space_changes(command, index, next_space_index, to_move);
// //         //  Check-03

// //         if ((to_move != dimensions_no_of_digitss) or (next_space_index == std::string::npos))
// //         {
// //             //  we should reset v1.id but unnecessary
// //             cout << "ERROR<Incorrect 'Dimension' value entered>\n";
// //             // continue;
// //         }
// //         v1.dims = (stoi(command.substr(index, to_move)));
// //         // Embeddings loop
// //         v1.data.resize(v1.dims);
// //         for (size_t i = 0; i < v1.dims; i++) // already validated that v1.dims == the 'Dimentions' set
// //         {
// //             index = next_space_index + 1;
// //             if (i != (dimensions - 1))
// //             {
// //                 next_space_changes(command, index, next_space_index, to_move);
// //                 //  Check-04
// //                 if ((next_space_index == std::string::npos))
// //                 {
// //                     cout << "ERROR<Float values do not match the dimenstions>\n";
// //                 }
// //                 v1.data[i] = stof(command.substr(index, to_move));
// //             }
// //             else
// //             {
// //                 to_move = command.size() - index;
// //                 v1.data[i] = stof(command.substr(index, to_move));
// //             }
// //         }
// //     }

// //     cout << "Id: " << v1.id << endl;
// //     cout << "Dims: " << v1.dims << endl;
// //     cout << std::fixed << std::setprecision(5);
// //     for (int i = 0; i < dimensions; i++)
// //     {
// //         cout << "Data[" << i << "] is: " << v1.data[i] << "      ";
// //     }

// //     return 0;
// // }
// // }

// // //

// #include "vector_store.h"
// #include "file_manager.h"
// #include "vector_server.h"
// #include "../tests/integration/json.hpp"
// using namespace std;
// int main()
// {
//     // {
//     // cout << "VectorDB   starting...\n";
//     // // ----------------------Declare All Components-------------------------
//     // Vector_store database;
//     // //
//     // std::string path = "../data/database.vdb";
//     // File_manager file_handler(path, dimenstions_set, id_length_set);
//     // //
//     // std::string port = "8080";
//     // Vector_Server server(port, database, file_handler);
//     // //----------------------------Main Body---------------------------------
//     // server.setup();
//     // server.run();
//     // server.stop();
//     // //----------------------------Program End-------------------------------
//     // }

//     Vector v;
//     std::string command = "QUERY";
//     size_t top_k = 0;
//     size_t dimensions = 5, dimensions_no_digits = 1;
//     std::size_t index = 0, next_space_index = 0, to_move = 0;
//     v.id = "";
//     const uint8_t MAX_TOP_K = 30;
//     const uint8_t MAX_TOP_K_DIGITS = 2;
//     int top_k_raw = 0;
//     // Check-01: verify command starts with "QUERY "
//     next_space_index = command.find(' ', 0);
//     if (next_space_index != 5 || next_space_index == std::string::npos)
//     {
//         std::cout << "ERROR<Incorrect format for 'QUERY'>\n";
//         return false;
//     }
//     // Check-02: parse and validate top_k
//     index = next_space_index + 1;
//     next_space_changes(command, index, next_space_index, to_move);
//     if (next_space_index == std::string::npos or to_move < 1)
//     {
//         std::cout << "ERROR<Value of 'top_k' not provided>\n";
//         return false;
//     }
//     top_k_raw = std::stoi(command.substr(index, to_move));
//     if (top_k_raw < 1)
//     {
//         std::cout << "ERROR<top_k must be a positive number>\n";
//         return false;
//     }
//     if (top_k_raw > MAX_TOP_K)
//     {
//         top_k_raw = MAX_TOP_K; // silently clamp
//     }
//     top_k = static_cast<size_t>(top_k_raw);
//     // Check-03: parse and validate dims
//     index = next_space_index + 1;
//     next_space_changes(command, index, next_space_index, to_move);
//     if (next_space_index == std::string::npos || to_move < 1)
//     {
//         std::cout << "ERROR<Value of 'dims' not provided>\n";
//         return false;
//     }
//     int dims_raw = std::stoi(command.substr(index, to_move));
//     if (dims_raw < 1)
//     {
//         std::cout << "ERROR<dims must be a positive number>\n";
//         return false;
//     }
//     v.dims = static_cast<size_t>(dims_raw);
//     // Embeddings loop
//     v.data.resize(dimensions);
//     for (std::size_t i = 0; i < dimensions; i++)
//     {
//         index = next_space_index + 1;
//         if (i != (dimensions - 1))
//         {
//             next_space_changes(command, index, next_space_index, to_move);
//             //  Check-04
//             if ((next_space_index == std::string::npos))
//             {
//                 std::cout << "ERROR<Float values do not match the dimenstions>\n";
//                 return false;
//             }
//             v.data[i] = std::stof(command.substr(index, to_move));
//         }
//         else
//         {
//             if (command.size() >= index)
//                 to_move = command.size() - index;
//             v.data[i] = std::stof(command.substr(index, to_move));
//         }
//     }
//     // return true;

//     cout << "Id: " << v.id << endl;
//     cout << "Top_k: " << top_k << endl;
//     cout << "Dims: " << v.dims << endl;
//     cout << std::fixed << std::setprecision(5);
//     for (int i = 0; i < dimensions; i++)
//     {
//         cout << "Data[" << i << "] is: " << v.data[i] << "      ";
//     }

//     return 0;
// }