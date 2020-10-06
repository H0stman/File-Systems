#include <iostream>
#include <fstream>

#ifndef __DISK_H__
#define __DISK_H__

#define DISKNAME "diskfile.bin"
#define BLOCK_SIZE 4096
#define DEBUG false

class Disk
{
private:
    std::fstream diskfile;
    const unsigned no_blocks = 2048;
    const unsigned disk_size = BLOCK_SIZE * no_blocks;
    bool disk_file_exists(const std::string& name);
public:
    Disk();
    ~Disk();
    unsigned get_no_blocks() { return no_blocks; }
    unsigned get_disk_size() { return disk_size; }
    // writes one block to the disk
    int write(unsigned block_no, uint8_t* blk);
    // reads one block from the disk
    int read(unsigned block_no, uint8_t* blk);
};

#endif // __DISK_H__
