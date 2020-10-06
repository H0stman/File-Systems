#include <iostream>
#include "disk.h"

Disk::Disk()
{
    // first check if the disk file exists, otherwise create it.
    if (!disk_file_exists(DISKNAME))
    {
        std::cout << "No disk file found...\n";
        std::cout << "Creating disk file: " << DISKNAME << std::endl;
        std::ofstream f(DISKNAME, std::ios::binary | std::ios::out);
        f.seekp((1 << 23) - 1);
        f.write("", 1);
    }
    // the disk is simulated as a binary file
    diskfile.open(DISKNAME, std::ios::in | std::ios::out | std::ios::binary);
    if (!diskfile.is_open())
    {
        std::cerr << "ERROR: Can't open diskfile: " << DISKNAME << ", exiting..." << std::endl;
        exit(-1);
    }
}

Disk::~Disk()
{
    diskfile.close();
}

bool Disk::disk_file_exists(const std::string& name)
{
    std::ifstream f(name.c_str());
    return f.good();
}

// writes one block to the disk
int Disk::write(unsigned block_no, uint8_t* blk)
{
    if (DEBUG)
        std::cout << "Disk::write(" << block_no << ")\n";
    // check if valid block number
    if (block_no >= no_blocks)
    {
        std::cout << "Disk::write - ERROR: Invalid block number (" << block_no << ")\n";
        return -1;
    }
    unsigned offset = block_no * BLOCK_SIZE;
    diskfile.seekp(offset, std::ios_base::beg);
    diskfile.write((char*)blk, BLOCK_SIZE);
    diskfile.flush();
    return 0;
}

// reads one block from the disk
int Disk::read(unsigned block_no, uint8_t* blk)
{
    if (DEBUG)
        std::cout << "Disk::read(" << block_no << ")\n";
    // check if valid block number
    if (block_no >= no_blocks) {
        std::cout << "Disk::write - ERROR: Invalid block number (" << block_no << ")\n";
        return -1;
    }
    unsigned offset = block_no * BLOCK_SIZE;
    diskfile.seekg(offset, std::ios_base::beg);
    diskfile.read((char*)blk, BLOCK_SIZE);
    return 0;
}
