import random

def generate_insert_command():
    vector_id = "Chunk-020"
    dims = 1056
    
    # Generate 1056 random floating point numbers between -50.0 and 50.0
    # Rounded to 4 decimal places to keep the payload size reasonable
    vector_data = [str(round(random.uniform(-50.0, 50.0), 4)) for _ in range(dims)]
    
    # Construct the exact format: INSERT Chunk-001 1056 0.23 ... 9.332
    command = f"INSERT {vector_id} {dims} " + " ".join(vector_data)
    
    # Save it to a text file
    filename = "test_payload.txt"
    with open(filename, "w") as f:
        # Adding the newline \n is crucial so your server parser knows the command is done!
        f.write(command + "\n") 
        
    print(f"Success! Generated {dims} dimensions for '{vector_id}'.")
    print(f"Data saved to {filename}")

if __name__ == "__main__":
    generate_insert_command()
    