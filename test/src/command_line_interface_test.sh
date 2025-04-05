#!/bin/bash

CLIENT=$2

if [ -z "$CLIENT" ]; then
  echo -e "CLIENT not set"
  exit 1
fi

# Function to generate a random binary file of a given size
generate_random_file() {
  local filename=$1
  local size=$2

  # Generate random data and save it to the file
  head -c "$size" </dev/urandom > "$filename"
  echo "Generated $filename with size $size bytes"
}

# Function to upload a file using the specified command
upload_file() {
  local local_file=$1
  local remote_file=$2

  # Run the upload command
  $CLIENT -command=put -storage_type="RS<7,5,16>" "$local_file" "$remote_file"
  echo "Uploaded $local_file to $remote_file"
}

# Function to create a directory with a random name
create_directory() {
  local random_dir=$1

  # Create random directory (only once for each file)
  $CLIENT -command=mkdir "/$random_dir"
  echo "Created directory /$random_dir"
}

# Function to compare files using sha512sum
compare_files() {
  local local_file=$1
  local remote_file=$2

  # Calculate sha512sum of the local file
  local local_sha512=$(sha512sum "$local_file" | awk '{print $1}')
  
  # Extract file name and extension
  local filename=$(basename "$remote_file")
  local extension="${filename##*.}"
  local name_without_extension="${filename%.*}"

  # Generate the downloaded file name by appending _downloaded to the name without extension
  local downloaded_file="/tmp/${name_without_extension}_downloaded.${extension}"

  rm -rf ${downloaded_file}
  
  # Download the file to temporary location
  $CLIENT -command=get "$remote_file" "$downloaded_file"

  # Calculate sha512sum of the downloaded file
  local remote_sha512=$(sha512sum "$downloaded_file" | awk '{print $1}')
  
  # Compare the sha512 sums
  if [ "$local_sha512" == "$remote_sha512" ]; then
    # If sha512sum is the same, print green log
    echo -e "\033[32mFiles match. SHA512 checksums are the same.\033[0m"
  else
    # If sha512sum is different, print red log
    echo -e "\033[31mERROR: Files do not match! SHA512 checksums are different.\033[0m"
    exit -1
  fi
}

# Get the file size from the first argument
size=$1

# Validate if the size argument is passed
if [ -z "$size" ]; then
  echo "Please provide a file size as an argument (in bytes)."
  exit 1
fi

# Generate a random directory name once
random_dir="random_dir_$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 10)"

# Create the random directory (only once)
create_directory "$random_dir"

# Set file name with size in /tmp directory
local_file="/tmp/random_file_${size}B.bin"
# Generate file
generate_random_file "$local_file" "$size"

# Set the remote file paths
remote_file_root="/random_file_${size}B.bin"  # Upload to root directory
remote_file_subdir="/$random_dir/random_file_${size}B.bin"  # Upload to subdirectory

# Upload the file to the root directory
upload_file "$local_file" "$remote_file_root"

# Upload the file to the subdirectory
upload_file "$local_file" "$remote_file_subdir"

# Compare the uploaded file with the original file for both locations
compare_files "$local_file" "$remote_file_root"
compare_files "$local_file" "$remote_file_subdir"
