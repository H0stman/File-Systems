#include <iostream>
#include "fs.h"

FS::FS()
{
	std::cout << "FS::FS()... Creating file system\n";
	disk.read(FAT_BLOCK, (uint8_t *)fat);
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
	std::cout << "FS::format()\n";

	//Set the whole disk to 0.
	int nrBlocks = disk.get_no_blocks();
	uint8_t *zeroblob = new uint8_t[BLOCK_SIZE]();
	for (int i = 0; i < nrBlocks; i++)
	{
		disk.write(i, zeroblob);
	}

	//Configure root direcotry block
	std::string name("root");
	dir_entry *root = reinterpret_cast<dir_entry *>(new char[BLOCK_SIZE]);
	root->access_rights = READ | WRITE | EXECUTE;
	name.copy(root->file_name, name.size());
	root->first_blk = ROOT_BLOCK;
	root->size = sizeof(dir_entry);
	root->type = TYPE_DIR;

	//Mark root dir and FAT as EOF in the FAT
	fat[0] = FAT_EOF;
	fat[1] = FAT_EOF;

	//Mark rest of blocks as FAT_FREE
	for (size_t i = 2; i < 2048; i++)
		fat[i] = FAT_FREE;

	//Write blocks to disk
	disk.write(ROOT_BLOCK, (uint8_t *)root);
	disk.write(FAT_BLOCK, (uint8_t *)fat);

	return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
	std::cout << "FS::create(" << filepath << ")\n";

	//Read input from user.
	std::string input = "", result = "";
	while (getline(std::cin, input) and !input.empty())
		result += input;

	//Calculate how many blocks will be needed for the string.
	int numBlocks = std::ceil(static_cast<float>(result.size()) / static_cast<float>(BLOCK_SIZE));

	std::vector<int> empty_spots(numBlocks);

	//If the string is smaller than the block size it is all put in one block.
	if (numBlocks <= 1)
	{
		//Find an empty spot in the FAT for the new file.
		empty_spots[0] = this->find_empty();
		if (empty_spots[0] == -1)
		{
			std::cout << "ERROR! No empty spots in the FAT." << std::endl;
			return -1;
		}

		//Write the data to the disk.
		char *strblock = new char[BLOCK_SIZE]();
		result.copy(strblock, BLOCK_SIZE);
		disk.write(empty_spots[0], (uint8_t *)strblock);
		delete[] strblock;
	}
	//Split the string in to BLOCK_SIZE big parts if the string is bigger than one BLOCK_SIZE and write to disk.
	else
	{
		//Find enough empty spots in the FAT for the new file.
		empty_spots = find_multiple_empty(numBlocks);
		if (empty_spots[0] == -1)
		{
			std::cout << "ERROR! Not enough empty spots in the FAT." << std::endl;
			return -1;
		}

		char *strblock = new char[BLOCK_SIZE]();
		size_t i = 0, j = 0;
		while (result.copy(strblock, BLOCK_SIZE, i) == BLOCK_SIZE) //Copy string to strblock while the amount of copied characters is BLOCK_SIZE.
		{
			disk.write(empty_spots[j], (uint8_t *)strblock);
			i += BLOCK_SIZE;
			j++;
		}

		//Write last block of the file to disk.
		disk.write(empty_spots[j], (uint8_t *)strblock);
		delete[] strblock;
	}

	//Read root block and add dir entry for file.
	dir_entry *rootblock = (dir_entry *)new char[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, (uint8_t *)rootblock);

	//Create the directory entry for the new file.
	dir_entry *fentry = new dir_entry;
	filepath.copy(fentry->file_name, filepath.size());
	fentry->access_rights = READ | WRITE | EXECUTE; //HÅRDKODAD
	fentry->first_blk = empty_spots[0];
	fentry->type = TYPE_FILE;
	fentry->size = result.size();

	//Update roots size.
	rootblock->size += fentry->size;

	//Find an empty spot for the new directory.
	int k = 1;
	while (rootblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry)) //Fel antal
		k++;

	//If there is no empty spot.
	if (rootblock[k].file_name[0] != '\0')
	{
		std::cout << "ERROR! No more space for dir_entries in the rootblock." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		rootblock[k] = *fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (int i = 0; i < empty_spots.size(); i++)
	{
		if (i + 1 < numBlocks)
		{
			fat[empty_spots[i]] = empty_spots[i + 1];
		}
		else
		{
			fat[empty_spots[i]] = FAT_EOF;
		}
	}

	//Uppdate the FAT and ROOT block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t *>(rootblock));
	return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
	std::cout << "FS::cat(" << filepath << ")\n";

	dir_entry *entry = find_filedata(filepath);

	if (!entry)
		return 1;

	uint8_t block[BLOCK_SIZE];
	//Read the block.
	disk.read(entry->first_blk, block);

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (int16_t *it = fat + entry->first_blk; *it not_eq FAT_EOF; it += (*it - (it - fat) / sizeof(int16_t)))
	{
		disk.read(*it, block);
		for (size_t i = 0; i < BLOCK_SIZE; i++)
			std::cout << block[i];
	}
	for (size_t i = 0; i < BLOCK_SIZE; i++)
		std::cout << block[i];

	std::cout << std::endl;

	return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
	std::cout << "FS::ls()\n";

	dir_entry *rootblock = reinterpret_cast<dir_entry *>(new char[BLOCK_SIZE]);
	disk.read(ROOT_BLOCK, reinterpret_cast<uint8_t *>(rootblock));

	dir_entry *file_entry = nullptr;
	//Set the first file entry to the rootblock.
	file_entry = rootblock;
	//Write out the rootblock
	std::cout << file_entry->file_name << "\t" << file_entry->size << std::endl;

	for (long unsigned int i = 1; i < BLOCK_SIZE / sizeof(dir_entry); i++)
	{
		file_entry++;
		if (file_entry->file_name[0] != '\0')
			std::cout << file_entry->file_name << "\t" << file_entry->size << std::endl;
	}

	delete rootblock;
	return 0;
}

// cp <sourcefilepath> <destfilepath> makes an exact copy of the file
// <sourcefilepath> to a new file <destfilepath>
int FS::cp(std::string sourcefilepath, std::string destfilepath)
{
	std::cout << "FS::cp(" << sourcefilepath << "," << destfilepath << ")\n";

	//Cannot copy root directory.
	if (sourcefilepath == "root")
	{
		std::cout << "ERROR! Cannot copy root directory." << std::endl;
		return -1;
	}

	//Find the dir_entry for the source.
	dir_entry *sourceDir = find_filedata(sourcefilepath);
	if (sourceDir == nullptr)
	{
		std::cout << "Error! Source not found." << std::endl;
		return -1;
	}

	//Calculate the number of blocks that the source occupies.
	int nrBlocks = std::ceil(static_cast<float>(sourceDir->size) / static_cast<float>(BLOCK_SIZE));
	std::vector<int> empty_spots(nrBlocks);

	int dataSize = 0;
	//If it just occupies one or zero blocks.
	if (nrBlocks == 1)
	{
		//Find one empty spot.
		empty_spots[0] = find_empty();
		if (empty_spots[0] == -1)
		{
			std::cout << "ERROR! No empty spots in the FAT." << std::endl;
			return -1;
		}

		//Read the source data from the disk.
		uint8_t sourceBlock[BLOCK_SIZE];
		disk.read(sourceDir->first_blk, sourceBlock);

		std::string s((char *)sourceBlock);

		//Add the size of the block to the dataSize
		dataSize += s.length();

		//Write the data to the disk in the new destination.
		disk.write(empty_spots[0], sourceBlock);
	}
	//If the file data occupies more than 1 block.
	else if (nrBlocks != 0)
	{
		empty_spots = find_multiple_empty(nrBlocks);
		if (empty_spots[0] == -1)
		{
			std::cout << "ERROR! Not enough empty spots in the FAT." << std::endl;
			return -1;
		}

		uint8_t sourceBlock[BLOCK_SIZE];
		int fatNr = sourceDir->first_blk;

		size_t i = 0;
		while (i < nrBlocks)
		{
			disk.read(fatNr, sourceBlock);

			std::string s((char *)sourceBlock);

			//Add the size of the block to the dataSize
			dataSize += s.length();

			disk.write(empty_spots[i], sourceBlock);

			memset(sourceBlock, 0, BLOCK_SIZE);
			fatNr = fat[fatNr];
			i++;
		}
	}

	//Read root block and add dir entry for file.
	dir_entry *rootblock = (dir_entry *)new char[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, (uint8_t *)rootblock);

	//Create the directory entry for the new file.
	dir_entry *fentry = new dir_entry;
	destfilepath.copy(fentry->file_name, destfilepath.size());
	fentry->access_rights = READ | WRITE | EXECUTE; //HÅRDKODAD
	fentry->first_blk = empty_spots[0];
	fentry->type = TYPE_FILE;
	fentry->size = dataSize;

	//Update roots size.
	rootblock->size += fentry->size;

	//Find an empty spot for the new directory.
	int k = 1;
	while (rootblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry)) //Fel antal
		k++;

	//If there is no empty spot.
	if (rootblock[k].file_name[0] != '\0')
	{
		std::cout << "ERROR! No more space for dir_entries in the rootblock." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		rootblock[k] = *fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (int i = 0; i < empty_spots.size(); i++)
	{
		if (i + 1 < nrBlocks)
		{
			fat[empty_spots[i]] = empty_spots[i + 1];
		}
		else
		{
			fat[empty_spots[i]] = FAT_EOF;
		}
	}

	//Uppdate the FAT and ROOT block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t *>(rootblock));
	return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
	std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

	//If filename is too long
	if (destpath.length() > 56)
	{
		std::cout << "Error! New filename is too long!" << std::endl;
		return -1;
	}

	//Read root block.
	dir_entry *rootblock = (dir_entry *)new char[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, (uint8_t *)rootblock);

	//Find the spot where the dir is.
	int i = 0;
	while (sourcepath.compare(reinterpret_cast<dir_entry *>(rootblock)[i].file_name) && i < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		++i;

	//Change the dir name and write it back to disk.
	destpath.copy(rootblock[i].file_name, 56);
	disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t *>(rootblock));
	return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
	std::cout << "FS::rm(" << filepath << ")\n";

	uint8_t block[BLOCK_SIZE];

	disk.read(ROOT_BLOCK, block);

	dir_entry *entry;
	size_t i = 0;
	for (dir_entry *it = reinterpret_cast<dir_entry *>(block); filepath.compare(it->file_name); entry = ++it)
	{
		if (i++ > std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		{
			std::cout << "Could not find file or directory on path " << filepath << std::endl;
			return 1;
		}
	}

	size_t save = 0;
	for (size_t i = fat[entry->first_blk];; i = save)
	{
		save = fat[i];
		fat[i] = FAT_FREE;
		if (save = EOF)
			break;
	}

	dir_entry *root = (dir_entry *)block;
	root->size -= entry->size;

	entry->access_rights = 0u;
	for (size_t i = 0; i < 56; i++)
		entry->file_name[i] = '\0';
	entry->first_blk = 0u;
	entry->size = 0u;
	entry->type = 0u;

	disk.write(ROOT_BLOCK, block);

	return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
	std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

	dir_entry *sourceDir = find_filedata(filepath1);
	dir_entry *destDir = find_filedata(filepath2);

	//Number of blocks that the append data and dest data take up.
	float nrBlocks = (static_cast<float>(sourceDir->size) / static_cast<float>(BLOCK_SIZE)) + (static_cast<float>(destDir->size) / static_cast<float>(BLOCK_SIZE));
	std::vector<int> empty_spots;
	int newBlocks;
	uint8_t block[BLOCK_SIZE];
	int dataSize = 0;

	if (nrBlocks > 1)
	{
		newBlocks = std::ceil(nrBlocks) - 1;

		//If its just 1 block that is to be appended.
		if (newBlocks == 1)
		{
			empty_spots[0] = find_empty();
			if (empty_spots[0] == -1)
			{
				std::cout << "ERROR! Not enough empty spots in the FAT." << std::endl;
				return -1;
			}
		}
		//If multiple blocks are to be appended.
		else
		{
			empty_spots = find_multiple_empty(nrBlocks);
			if (empty_spots[0] == -1)
			{
				std::cout << "ERROR! Not enough empty spots in the FAT." << std::endl;
				return -1;
			}
		}

		int fatNrSrc = sourceDir->first_blk;
		int fatNrDest = destDir->first_blk;
		while (fat[fatNrDest] != FAT_EOF)
		{
			fatNrDest = fat[fatNrDest];
		}

		//Read the first block that is to be appended.
		disk.read(fatNrSrc, block);
		//Put the data into a string.
		std::string s((char *)block);

		//Add the size of the data to the dataSize
		dataSize += s.length();

		//Reset block.
		memset(block, 0, BLOCK_SIZE);

		//Read the block that it will be appended TO.
		disk.read(fatNrDest, block);
		std::string s2((char *)block);

		//The empty bytes available in the destination block.
		int emptyBytes = BLOCK_SIZE - (destDir->size % BLOCK_SIZE);

		//Add data from source to the dest string.
		s2 += s;

		memset(block, 0, BLOCK_SIZE);
		//Copy emptyBytes amount of characters from
		s2.copy((char *)block, BLOCK_SIZE);
		//Erase the chars that was copied into block.
		s2.erase(0, BLOCK_SIZE - 1);

		//Write back this block to disk, with the appended data.
		disk.write(fatNrDest, block);
		//The data that did not fit in that block is still stored in s2.

		int i = 0;
		while (i < newBlocks)
		{
			if (fat[fatNrSrc] != -1)
			{
				fatNrSrc = fat[fatNrSrc];
				disk.read(fatNrSrc, block);

				s2.append((char *)block);
			}
			memset(block, 0, BLOCK_SIZE);
			dataSize += s2.copy((char *)block, BLOCK_SIZE);
			s2.erase(0, BLOCK_SIZE - 1);

			disk.write(empty_spots[i], block);
			memset(block, 0, BLOCK_SIZE);
			i++;
		}
	}
	else
	{
		newBlocks = 0;

		disk.read(sourceDir->first_blk, block);
		std::string s((char *)block);
		memset(block, 0, BLOCK_SIZE);
		disk.read(destDir->first_blk, block);

		dataSize += s.length();

		s.append((char *)block);
		memset(block, 0, BLOCK_SIZE);
		s.copy((char *)block, s.length());
		disk.write(destDir->first_blk, block);
	}

	//Update the FAT
	int fatNr = destDir->first_blk;
	while (fat[fatNr] != FAT_EOF)
	{
		fatNr = fat[fatNr];
	}

	for (int i = 0; i < newBlocks; i++)
	{
		fat[fatNr] = empty_spots[i];
		fatNr = fat[fatNr];
	}
	fat[fatNr] = FAT_EOF;

	//Read root block.
	dir_entry *rootblock = (dir_entry *)new char[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, (uint8_t *)rootblock);

	//Update roots size.
	rootblock->size += dataSize;

	//Find the spot where the dir is.
	int k = 0;
	while (filepath2.compare(reinterpret_cast<dir_entry *>(rootblock)[k].file_name) && k < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		++k;

	rootblock[k].size += dataSize;

	//Uppdate the FAT and ROOT block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t *>(rootblock));
	return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
	std::cout << "FS::mkdir(" << dirpath << ")\n";
	return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
	std::cout << "FS::cd(" << dirpath << ")\n";
	return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
	std::cout << "FS::pwd()\n";
	return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
	std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
	return 0;
}

//Helper functions
//----------------------------------------------------------------------------

//Helper function to find an empty spot for the new file. Called in create
int FS::find_empty()
{
	//After the root entry.
	int i = 2;
	//Go through the FAT until a free spot is found or the end of the array is reached.
	while (fat[i] != FAT_FREE and i < BLOCK_SIZE / 2)
		i++;

	//If it found a free spot.
	if (fat[i] == FAT_FREE)
		return i;

	//If no free spot was found.
	else
		return -1;
}

//Helper function to find multiple empty spots for the new file. Called in create
std::vector<int> FS::find_multiple_empty(int numBlocks)
{
	std::vector<int> empty_spots(numBlocks);
	//After the root and fat entries.
	int j = 2;
	for (int i = 0; i < numBlocks; i++)
	{
		//Step through the fat until a free spot is found or the end of the fat is reached.
		while (fat[j] != FAT_FREE && j < BLOCK_SIZE / 2)
			j++;

		//If it found a free spot.
		if (fat[j] == FAT_FREE)
		{
			empty_spots.push_back(j);

			j++;
		}
		//If it did not, then there are none.
		else
			empty_spots[0] = -1;
	}

	return empty_spots;
}

dir_entry *FS::find_filedata(std::string filepath)
{
	//Read root block.
	uint8_t *block = new uint8_t[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, block);

	if (filepath.find('/') == std::string::npos)
		filepath += '/';

	size_t start_i = 0, end_i = 0, i = 0;

	while ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
	{
		i = 0;
		std::string dirname = filepath.substr(start_i, end_i - start_i);
		//Check if dir_entry name exists in this folder.
		while (dirname.compare(reinterpret_cast<dir_entry *>(block)[i].file_name))
		{
			++i;
			if (i == std::floor(BLOCK_SIZE / sizeof(dir_entry)))
			{
				delete block;
				return nullptr;
			}
		}

		start_i = ++end_i;

		//This folder exists, load in that folders disk block.
		if ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
			disk.read(reinterpret_cast<dir_entry *>(block)[i].first_blk, block);
	}
	delete block;
	return &((reinterpret_cast<dir_entry *>(block)[i]));
}
