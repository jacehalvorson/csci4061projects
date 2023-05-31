#! /bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <test_num>"
    exit 1
fi

# Top-level project directory
top_dir=$(pwd)

# Archive program to test
prog="$top_dir/minitar"
if [ ! -f "$prog" ]; then
    echo "Error: Test subject program $prog does not exist"
    exit 1
fi

# Temporary working directory in which all tests will be run
temp_dir="$top_dir/testing_dir"
# Directory containing all sample files
test_file_dir="$top_dir/test_files"

test_create() {
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in $@
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done

    cd $temp_dir
    $prog -c -f test.tar $* &> /dev/null
    rm -f $*
    tar -xvf test.tar
    for file_name in $@
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
}

test_append() {
    local -n start_files=$1
    local -n add_files=$2
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${start_files[@]}
    do
       cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done

    cd $temp_dir
    $prog -c -f test.tar ${start_files[*]} &> /dev/null
    rm -f ${start_files[*]}
    tar -xvf test.tar
    for file_name in ${start_files[@]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
    rm -f ${start_files[*]}

    for file_name in ${add_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done

    $prog -a -f test.tar ${add_files[*]} &> /dev/null
    rm -f ${add_files[*]}
    tar -xvf test.tar
    for file_name in ${start_files[@]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
    for file_name in ${add_files[*]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
}

test_list() {
    local -n start_files=$1
    local -n append_files=$2
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${start_files[@]}
    do
       cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    for file_name in ${append_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done

    cd $temp_dir
    $prog -c -f test.tar ${start_files[*]} &> /dev/null
    $prog -t -f test.tar

    if [[ ! -z "$append_files" ]]; then
        $prog -a -f test.tar ${append_files[*]} &> /dev/null
        $prog -t -f test.tar
    fi
}

test_remove() {
    local -n orig_files=$1
    local -n delete_files=$2
    local -n leftover_files=$3
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${orig_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${orig_files[*]} &> /dev/null
    $prog -r -f test.tar ${delete_files[*]} &> /dev/null

    rm -f ${orig_files[*]}
    tar -xvf test.tar
    for file_name in ${leftover_files[@]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
}

# Create a simple one-file archive
if [ $1 == 1 ]; then
    test_create hello.txt
fi

# Create archive with a binary file
if [ $1 == 2 ]; then
    test_create f1.bin
fi

# Create a multi-file archive mixing text and binary data
if [ $1 == 3 ]; then
    test_create hello.txt f2.bin
fi

# Create archive with single large text file
if [ $1 == 4 ]; then
    test_create gatsby.txt
fi

# Create archive with single large binary file
if [ $1 == 5 ]; then
    test_create large.bin
fi

# Create an archive with many files, both text and binary
if [ $1 == 6 ]; then
    test_create hello.txt gatsby.txt f1.txt f1.bin f2.txt f2.bin f3.txt f3.bin \
        f4.txt f4.bin f5.txt f5.bin f6.txt f6.bin f7.txt f7.bin f8.txt f8.bin \
        f9.txt f9.bin f10.txt f10.bin
fi

# Append a single file to an existing archive
if [ $1 == 7 ]; then
    orig_files=("hello.txt" "f1.bin" "f2.bin")
    append_files=("f1.txt")
    test_append orig_files append_files
fi

# Append a single large file to an existing archive
if [ $1 == 8 ]; then
    orig_files=("f1.txt f2.txt f17.bin f13.txt")
    append_files=("gatsby.txt")
    test_append orig_files append_files
fi

# Append multiple files to an archive
if [ $1 == 9 ]; then
    orig_files=("f4.txt" "f8.bin" "f15.bin" "f16.txt")
    append_files=("f2.bin hello.txt f9.bin")
    test_append orig_files append_files
fi

# List members of single-file archive
if [ $1 == 10 ]; then
    base_files=("hello.txt")
    add_files=()
    test_list base_files add_files
fi

# List members of multi-file archive
if [ $1 == 11 ]; then
    base_files=("hello.txt" "f18.txt" "f20.bin" "f19.bin" "f13.txt")
    add_files=()
    test_list base_files add_files
fi

# List members of multi-file archive, before and after append
if [ $1 == 12 ]; then
    base_files=("large.bin" "f6.txt" "f19.bin" "f11.txt")
    add_files=("f8.txt" "f4.txt" "f18.bin" "gatsby.txt")
    test_list base_files add_files
fi

# Create an archive and update one of its files to a new version
if [ $1 == 13 ]; then
    base_files=("gatsby.txt" "f20.bin" "f15.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]} &> /dev/null
    cat f15.txt "$test_file_dir/f20.txt" > temp
    mv temp f15.txt
    $prog -u -f test.tar f15.txt &> /dev/null
    rm -f ${base_files[*]}

    tar -xvf test.tar
    diff -q "$test_file_dir/gatsby.txt" gatsby.txt
    diff -q "$test_file_dir/f20.bin" f20.bin
    cat "$test_file_dir/f15.txt" "$test_file_dir/f20.txt" > f15_expected.txt
    diff -q f15_expected.txt f15.txt
fi

# Create an archive and attempt to update a file not present in the archive
if [ $1 == 14 ]; then
    base_files=("large.bin f19.txt f7.bin")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cp "$test_file_dir/f1.txt" $temp_dir
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]} &> /dev/null
    rm -f large.bin f19.txt f7.bin
    $prog -u -f test.tar f1.txt &> /dev/null
    rm -f f1.txt

    tar -xvf test.tar
    for file_name in ${base_files[@]}
    do
        diff -q "$test_file_dir/$file_name" $file_name
    done
fi

# Create an archive and update multiple files to new versions
if [ $1 == 15 ]; then
    base_files=("gatsby.txt" "f20.bin" "f15.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]}
    cat "f15.txt" "$test_file_dir/f20.txt" > temp
    mv temp f15.txt
    cat "f20.bin" "$test_file_dir/f19.bin" > temp
    mv temp f20.bin
    $prog -u -f test.tar f15.txt f20.bin
    rm -f ${base_files[*]}

    tar -xvf test.tar
    diff -q "$test_file_dir/gatsby.txt" gatsby.txt
    cat "$test_file_dir/f20.bin" "$test_file_dir/f19.bin" > f20_expected.bin
    diff -q f20_expected.bin f20.bin
    cat "$test_file_dir/f15.txt" "$test_file_dir/f20.txt" > f15_expected.txt
    diff -q f15_expected.txt f15.txt
fi

# Create an archive and attempt to update several files not present in the archive
if [ $1 == 16 ]; then
    base_files=("large.bin f19.txt f7.bin")
    add_files=("f8.bin f9.txt f10.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    for file_name in ${add_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]} &> /dev/null
    rm -f ${base_files[*]}
    $prog -u -f test.tar ${add_files[*]} &> /dev/null
    rm -f ${add_files[*]}

    tar -xvf test.tar
    for file_name in ${base_files[@]}
    do
        diff -q "$test_file_dir/$file_name" $file_name
    done
fi

# Create an archive and attempt to update files both present and not present in the archive
if [ $1 == 17 ]; then
    base_files=("large.bin f19.txt f7.bin")
    add_files=("f8.bin" "large.bin" "f8.txt f10.txt" "f19.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    for file_name in ${add_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]} &> /dev/null
    rm -f f7.bin
    $prog -u -f test.tar ${add_files[*]} &> /dev/null
    rm -f ${add_files[*]}

    tar -xvf test.tar
    for file_name in ${base_files[@]}
    do
        diff -q "$test_file_dir/$file_name" $file_name
    done
fi

# Extract files from an archive created by standard tar program
if [ $1 == 18 ]; then
    base_files=("large.bin" "f1.txt" "f9.txt" "f9.bin" "f1.bin" "gatsby.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    tar -cf test.tar ${base_files[*]}
    rm -f ${base_files[*]}

    $prog -t -f test.tar
    $prog -x -f test.tar &> /dev/null
    for file_name in ${base_files[@]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
fi

# Extract files from an archive created by minitar tool
if [ $1 == 19 ]; then
    base_files=("large.bin" "f1.txt" "f9.txt" "f9.bin" "f1.bin" "gatsby.txt")
    mkdir -p $temp_dir
    rm -f "$temp_dir"/*
    for file_name in ${base_files[@]}
    do
        cp "$test_file_dir/$file_name" "$temp_dir/$file_name"
    done
    cd $temp_dir

    $prog -c -f test.tar ${base_files[*]} &> /dev/null
    rm -f ${base_files[*]}

    $prog -t -f test.tar
    $prog -x -f test.tar &> /dev/null
    for file_name in ${base_files[@]}
    do
        diff -q "$test_file_dir/$file_name" "$file_name"
    done
fi
