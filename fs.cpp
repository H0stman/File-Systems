#include <iostream>
#include "fs.h"

FS::FS()
{
	std::cout << "FS::FS()... Creating file system\n";
	disk.read(FAT_BLOCK, (uint8_t *)fat);
	path = "root/";
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
	uint8_t zeroblob[BLOCK_SIZE] = {0};
	for (int i = 0; i < nrBlocks; i++)
	{
		disk.write(i, zeroblob);
	}

	//Configure root direcotry block
	std::string name("root");
	uint8_t blob[BLOCK_SIZE] = {0};
	dir_entry *root = reinterpret_cast<dir_entry *>(blob);
	root->access_rights = READ | WRITE | EXECUTE;
	name.copy(root->file_name, name.size());
	root->first_blk = ROOT_BLOCK;
	root->size = 0;
	root->type = TYPE_DIR;

	//Mark root dir and FAT as EOF in the FAT
	fat[0] = FAT_EOF;
	fat[1] = FAT_EOF;

	//Mark rest of blocks as FAT_FREE
	for (int i = 2; i < nrBlocks; i++)
		fat[i] = FAT_FREE;

	//Write blocks to disk
	disk.write(ROOT_BLOCK, (uint8_t *)root);
	disk.write(FAT_BLOCK, (uint8_t *)fat);

	path = "root/";

	return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
	std::cout << "FS::create(" << filepath << ")\n";

	if (find_dir_entry(filepath).file_name[0] != '\0')
	{
		std::cout << "Error! That file or directory already exists." << std::endl;
		return -1;
	}

	//Read input from user.
	std::string input = "", result = "";
	while (getline(std::cin, input) and !input.empty())
		result += input;

	//Calculate how many blocks will be needed for the string.
	size_t numBlocks = std::ceil(static_cast<float>(result.size()) / static_cast<float>(BLOCK_SIZE));

	std::vector<int> empty_spots(numBlocks);

	char strblock[BLOCK_SIZE] = {0};

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

		result.copy(strblock, BLOCK_SIZE);
		disk.write(empty_spots[0], (uint8_t *)strblock);
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

		size_t i = 0, j = 0;
		while (result.copy(strblock, BLOCK_SIZE, i) == BLOCK_SIZE) //Copy string to strblock while the amount of copied characters is BLOCK_SIZE.
		{
			disk.write(empty_spots[j], (uint8_t *)strblock);
			i += BLOCK_SIZE;
			j++;
		}

		//Write last block of the file to disk.
		disk.write(empty_spots[j], (uint8_t *)strblock);
	}

	dir_entry currentDir = {0};

	if (filepath.find('/') == std::string::npos)
		currentDir = find_dir_entry(this->path);
	else
	{
		std::string tempPath = filepath;
		while (tempPath.back() != '/')
			tempPath.pop_back();

		currentDir = find_dir_entry(tempPath);
	}

	std::string lastdir = "";
	size_t lastslash = filepath.find_last_of("/");
	lastdir = filepath.substr(lastslash + 1, filepath.size() - lastslash);

	//Create the directory entry for the new file.
	dir_entry fentry = {0};
	lastdir.copy(fentry.file_name, filepath.size());

	fentry.access_rights = READ | WRITE | EXECUTE; //HÅRDKODAD
	fentry.first_blk = empty_spots[0];
	fentry.type = TYPE_FILE;
	fentry.size = result.size();

	if (updateSize(fentry.size, filepath) == -1)
	{
		std::cout << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	//Read current dir block.
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(strblock);
	disk.read(currentDir.first_blk, (uint8_t *)dirblock);

	//Find an empty spot for the new directory/file.
	size_t k = 1;
	while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
		k++;

	//If there is no empty spot.
	if (dirblock[k].file_name[0] != '\0')
	{
		std::cout << "ERROR! No more space for dir_entries in the current directory." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		dirblock[k] = fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (size_t i = 0; i < empty_spots.size(); i++)
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

	//Uppdate the FAT and current directory block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(currentDir.first_blk, reinterpret_cast<uint8_t *>(dirblock));
	return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
	std::cout << "FS::cat(" << filepath << ")\n";

	dir_entry entry = find_dir_entry(filepath);

	if (entry.file_name[0] == '\0')
	{
		std::cout << "Error! File does not exist." << std::endl;
		return 1;
	}
	if (entry.type == 1)
	{
		std::cout << "Error! That is a directory!" << std::endl;
		return -1;
	}
	if (!(entry.access_rights & READ))
	{
		std::cout << "Error! You do not have access rights to read that file." << std::endl;
		return -1;
	}

	uint8_t block[BLOCK_SIZE] = {0};
	//Read the block.
	disk.read(entry.first_blk, block);

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (int16_t *it = fat + entry.first_blk; *it not_eq FAT_EOF; it += (*it - (it - fat) / sizeof(int16_t)))
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

	uint8_t buff[BLOCK_SIZE] = {0};
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(buff);
	dir_entry currentDir = find_dir_entry(this->path);
	disk.read(currentDir.first_blk, reinterpret_cast<uint8_t *>(dirblock));

	dir_entry *file_entry = nullptr;
	//Set the first file entry to be the start of the directory block.
	file_entry = dirblock;
	//Write out the current blocks content.

	std::string accessRights = "";
	if (file_entry->access_rights & READ)
	{
		accessRights.append("r");
	}
	else
	{
		accessRights.append("-");
	}

	if (file_entry->access_rights & WRITE)
	{
		accessRights.append("w");
	}
	else
	{
		accessRights.append("-");
	}

	if (file_entry->access_rights & EXECUTE)
	{
		accessRights.append("x");
	}
	else
	{
		accessRights.append("-");
	}

	std::cout << file_entry->file_name << "\t" << static_cast<int>(file_entry->type) << "\t" << accessRights << "\t" << static_cast<int>(file_entry->size) << std::endl;

	for (long unsigned int i = 1; i < BLOCK_SIZE / sizeof(dir_entry); i++)
	{
		accessRights = "";
		file_entry++;
		if (file_entry->file_name[0] != '\0')
		{
			if (file_entry->access_rights & READ)
			{
				accessRights.append("r");
			}
			else
			{
				accessRights.append("-");
			}

			if (file_entry->access_rights & WRITE)
			{
				accessRights.append("w");
			}
			else
			{
				accessRights.append("-");
			}

			if (file_entry->access_rights & EXECUTE)
			{
				accessRights.append("x");
			}
			else
			{
				accessRights.append("-");
			}
			std::cout << file_entry->file_name << "\t" << static_cast<int>(file_entry->type) << "\t" << accessRights << "\t" << static_cast<int>(file_entry->size) << std::endl;
		}
	}

	return 0;
}

// cp <sourcefilepath> <destfilepath> makes an exact copy of the file
// <sourcefilepath> to a new file <destfilepath>
int FS::cp(std::string sourcefilepath, std::string destfilepath)
{
	std::cout << "FS::cp(" << sourcefilepath << "," << destfilepath << ")\n";

	//Find the dir_entry for the source.
	dir_entry sourceDir = find_dir_entry(sourcefilepath);
	if (sourceDir.file_name[0] == '\0')
	{
		std::cout << "Error! Source not found." << std::endl;
		return -1;
	}
	if (sourceDir.type == 1)
	{
		std::cout << "Error! Source is a directory, not a file." << std::endl;
		return -1;
	}

	if (find_dir_entry(destfilepath).file_name[0] != '\0')
	{
		std::cout << "Error! Destination already exists." << std::endl;
		return -1;
	}

	//Calculate the number of blocks that the source occupies.
	size_t nrBlocks = std::ceil(static_cast<float>(sourceDir.size) / static_cast<float>(BLOCK_SIZE));
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
		uint8_t sourceBlock[BLOCK_SIZE] = {0};
		disk.read(sourceDir.first_blk, sourceBlock);

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

		uint8_t sourceBlock[BLOCK_SIZE] = {0};
		int fatNr = sourceDir.first_blk;

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

	if (destfilepath.find("root") != 0)
	{
		destfilepath = path + destfilepath;
	}
	std::string temppath = destfilepath;
	if (destfilepath.find('/') != std::string::npos)
	{
		temppath = destfilepath.substr(destfilepath.find_last_of('/') + 1, destfilepath.length() - 1);
		destfilepath.erase(destfilepath.find_last_of('/'), destfilepath.length() - 1);
	}

	dir_entry currentDir = find_dir_entry(destfilepath);

	//Create the directory entry for the new file.
	dir_entry fentry = {0};
	temppath.copy(fentry.file_name, temppath.size());
	fentry.access_rights = READ | WRITE | EXECUTE; //HÅRDKODAD
	fentry.first_blk = empty_spots[0];
	fentry.type = TYPE_FILE;
	fentry.size = dataSize;

	//Update folders sizes.
	if (updateSize(fentry.size, destfilepath) == -1)
	{
		std::cout << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	//Read current directory block and add dir entry for file.
	uint8_t buff[BLOCK_SIZE] = {0};
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(buff);
	disk.read(currentDir.first_blk, (uint8_t *)dirblock);

	//Find an empty spot for the new directory.
	size_t k = 1;
	while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry)) //Fel antal
		k++;

	//If there is no empty spot.
	if (dirblock[k].file_name[0] != '\0')
	{
		std::cout << "ERROR! No more space for dir_entries in the current block." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		dirblock[k] = fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (size_t i = 0; i < empty_spots.size(); i++)
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

	//Uppdate the FAT and current directory block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(currentDir.first_blk, reinterpret_cast<uint8_t *>(dirblock));
	return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
	std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

	dir_entry currentDir = {0};
	dir_entry destDir = find_dir_entry(destpath);

	//Move if dest is a directory.
	if (destDir.type == 1)
	{
		currentDir = find_dir_entry(sourcepath);
		if (currentDir.file_name[0] == '\0')
		{
			std::cout << "Error! The source file does not exist!" << std::endl;
			return -1;
		}
		if (currentDir.type == 1)
		{
			std::cout << "Error! The source is a directory, not a file." << std::endl;
		}

		rm(sourcepath);
		//Update the sizes
		if (updateSize(currentDir.size, destpath) == -1)
		{
			std::cout << "Error! Could not update the sizes." << std::endl;
			return -1;
		}

		//Read dest block.
		uint8_t buff[BLOCK_SIZE] = {0};
		dir_entry *destblock = reinterpret_cast<dir_entry *>(buff);
		disk.read(destDir.first_blk, (uint8_t *)destblock);

		//Find an empty spot for the file directory.
		size_t k = 1;
		while (destblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry)) //Fel antal
			k++;

		//If there is no empty spot.
		if (destblock[k].file_name[0] != '\0')
		{
			std::cout << "ERROR! No more space for dir_entries in the current block." << std::endl;
			return -1;
		}
		//Put the new directory in the empty spot.
		else
			destblock[k] = currentDir;

		//Update the block.
		disk.write(destDir.first_blk, reinterpret_cast<uint8_t *>(destblock));

		return 0;
	}
	if (destDir.file_name[0] != '\0' && destDir.type == 0)
	{
		std::cout << "Error! New filename already exists!" << std::endl;
		return -1;
	}

	//If filename is too long
	if (destpath.length() > 56)
	{
		std::cout << "Error! New filename is too long!" << std::endl;
		return -1;
	}

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = {0};
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(buff);

	if (sourcepath.find("root") != 0)
	{
		sourcepath = path + sourcepath;
	}
	std::string temppath = sourcepath.substr(sourcepath.find_last_of('/') + 1, sourcepath.length() - 1);
	sourcepath.erase(sourcepath.find_last_of('/'), sourcepath.length() - 1);

	currentDir = find_dir_entry(sourcepath);

	disk.read(currentDir.first_blk, buff);

	//Find the spot where the dir is.
	int i = 0;
	while (temppath.compare(dirblock->file_name) && i < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
	{
		i++;
		dirblock++;
	}
	//Change the dir name and write it back to disk.
	memset(dirblock->file_name, 0, 56);
	destpath.copy(dirblock->file_name, 56);
	disk.write(currentDir.first_blk, reinterpret_cast<uint8_t *>(buff));
	return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
	std::cout << "FS::rm(" << filepath << ")\n";

	uint8_t block[BLOCK_SIZE] = {0};
	std::string temppath = "";

	if (filepath.find("root") != 0)
		filepath = path + filepath;

	temppath = filepath.substr(filepath.find_last_of('/') + 1, filepath.length() - 1);
	filepath.erase(filepath.find_last_of('/'), filepath.length() - 1);

	dir_entry currentDir = find_dir_entry(filepath);
	disk.read(currentDir.first_blk, block);

	if (currentDir.file_name[0] == '\0')
		return -1;

	dir_entry *entry;
	size_t i = 0;
	for (dir_entry *it = reinterpret_cast<dir_entry *>(block); temppath.compare(it->file_name); entry = ++it)
	{
		if (i++ > std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		{
			std::cout << "Could not find file or directory on path: " << filepath << std::endl;
			return 1;
		}
	}

	int save = 0;
	for (size_t i = fat[entry->first_blk];; i = save)
	{
		save = fat[i];
		fat[i] = FAT_FREE;
		if (save == EOF)
			break;
	}

	entry->access_rights = 0u;
	for (size_t i = 0; i < 56; i++)
		entry->file_name[i] = '\0';
	entry->first_blk = 0u;
	//Save the size for the update function.
	uint32_t tempSize = entry->size;
	entry->size = 0u;
	entry->type = 0u;

	disk.write(currentDir.first_blk, block);

	if (updateSize(-tempSize, filepath) == -1)
	{
		std::cout << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
	std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

	dir_entry sourceDir = find_dir_entry(filepath1);
	if (sourceDir.file_name[0] == '\0' || sourceDir.type == 1)
	{
		std::cout << "Error! The source file does not exist, or the source is a directory." << std::endl;
		return -1;
	}
	if (!(sourceDir.access_rights & READ))
	{
		std::cout << "Error! You do not have access rights to read from the source." << std::endl;
		return -1;
	}

	dir_entry destDir = find_dir_entry(filepath2);
	if (destDir.file_name[0] == '\0' || destDir.type == 1)
	{
		std::cout << "Error! The destination file does not exist, or the source is a directory." << std::endl;
		return -1;
	}
	if (!(destDir.access_rights & WRITE))
	{
		std::cout << "Error! You do not have access rights to write to the destination." << std::endl;
		return -1;
	}

	//Number of blocks that the append data and dest data take up.
	float nrBlocks = (static_cast<float>(sourceDir.size) / static_cast<float>(BLOCK_SIZE)) + (static_cast<float>(destDir.size) / static_cast<float>(BLOCK_SIZE));
	std::vector<int> empty_spots;
	int newBlocks;
	uint8_t block[BLOCK_SIZE] = {0};
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

		int fatNrSrc = sourceDir.first_blk;
		int fatNrDest = destDir.first_blk;
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

		disk.read(sourceDir.first_blk, block);
		std::string s((char *)block);
		memset(block, 0, BLOCK_SIZE);
		disk.read(destDir.first_blk, block);

		dataSize += s.length();

		s.append((char *)block);
		memset(block, 0, BLOCK_SIZE);
		s.copy((char *)block, s.length());
		disk.write(destDir.first_blk, block);
	}

	//Update the FAT
	int fatNr = destDir.first_blk;
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

	if (filepath2.find("root") != 0)
	{
		filepath2 = path + filepath2;
	}
	std::string temppath = filepath2;
	if (filepath2.find('/') != std::string::npos)
	{
		temppath = filepath2.substr(filepath2.find_last_of('/') + 1, filepath2.length() - 1);
		filepath2.erase(filepath2.find_last_of('/'), filepath2.length() - 1);
	}

	dir_entry currentDir = find_dir_entry(filepath2);

	//Update directories size
	if (updateSize(dataSize, filepath2) == -1)
	{
		std::cout << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = {0};
	dir_entry *currentblock = reinterpret_cast<dir_entry *>(buff);
	disk.read(currentDir.first_blk, (uint8_t *)currentblock);

	//Find the spot where the dir is.
	int k = 0;
	while (temppath.compare(reinterpret_cast<dir_entry *>(currentblock)[k].file_name) && k < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		++k;

	//Increase dest size.
	currentblock[k].size += dataSize;

	//Uppdate the FAT and current directory block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t *)fat);
	disk.write(currentDir.first_blk, reinterpret_cast<uint8_t *>(currentblock));

	return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
	std::cout << "FS::mkdir(" << dirpath << ")\n";

	//New directory
	dir_entry newDir = {0};
	dirpath.copy(newDir.file_name, 56);
	std::vector<int> empty_spot(1);
	empty_spot[0] = find_empty();
	newDir.first_blk = empty_spot[0];
	newDir.size = 0;
	newDir.type = 1;
	newDir.access_rights = READ | WRITE | EXECUTE;

	dir_entry currentDir = find_dir_entry(this->path);

	//Return directory
	dir_entry returnDir = {0};
	returnDir.file_name[0] = '.';
	returnDir.file_name[1] = '.';
	returnDir.first_blk = currentDir.first_blk;
	returnDir.size = 0;
	returnDir.type = 1;
	returnDir.access_rights = READ | WRITE | EXECUTE;

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = {0};
	dir_entry *currentblock = reinterpret_cast<dir_entry *>(buff);
	disk.read(currentDir.first_blk, (uint8_t *)currentblock);

	//Find an empty spot for the new directory.
	unsigned int k = 1;
	while (currentblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
		k++;

	//If there is no empty spot.
	if (currentblock[k].file_name[0] != '\0')
	{
		std::cout << "ERROR! No more space for dir_entries in the current block." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		currentblock[k] = newDir;

	//Write back the current block.
	disk.write(currentDir.first_blk, (uint8_t *)currentblock);

	//Read new block, that is for the new directory entry.
	uint8_t buff2[BLOCK_SIZE] = {0};
	dir_entry *newblock = reinterpret_cast<dir_entry *>(buff2);
	newblock[0] = returnDir;

	//Write the return dir.
	disk.write(empty_spot[0], (uint8_t *)newblock);

	//Update fat.
	fat[empty_spot[0]] = FAT_EOF;
	disk.write(FAT_BLOCK, (uint8_t *)fat);

	return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
	std::cout << "FS::cd(" << dirpath << ")\n";

	if (find_dir_entry(dirpath).file_name[0] == '\0') //Checks if path is valid.
		return -1;

	if (dirpath.back() != '/')
		dirpath.append("/");
	if (dirpath.find("root") != std::string::npos) //Absolute path. NOTE: directory name may contain 'root'. Consider changing this later.
		path = dirpath;
	else //Relative path
	{
		size_t newpos = dirpath.find('/'), oldpos = 0;
		std::string dir = "";
		dir = dirpath.substr(oldpos, newpos - oldpos);
		for (; newpos != std::string::npos; dir = dirpath.substr(oldpos, newpos - oldpos))
		{
			if (!dir.compare(".."))
			{
				if (path.back() == '/')
					path.pop_back();
				while (path.back() != '/')
					path.pop_back();
			}
			else
				path += dir.append("/");
			oldpos = newpos++;
			newpos = dirpath.find("/", newpos);
		}
	}

	return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
	std::cout << "FS::pwd()\n";
	std::cout << path << std::endl;
	return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
	std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
	dir_entry valid = find_dir_entry(filepath);
	if (*valid.file_name == '\0')
		return 1;

	uint8_t block[BLOCK_SIZE];

	if (filepath.find("root") != 0)
		filepath = path + filepath;

	//Remove last folder/file from filepath.
	size_t lastslash = filepath.find_last_of("/");
	filepath.erase(lastslash + 1, filepath.size() - lastslash);

	//Retrive the dir etntry of the dir/file to be changed.
	dir_entry dirtoload = find_dir_entry(filepath);

	disk.read(dirtoload.first_blk, block);

	//Look for valid in block.
	dir_entry *it;
	for (it = reinterpret_cast<dir_entry *>(block); std::strcmp(it->file_name, valid.file_name); it++)
		;

	size_t accsessnum = std::stoul(accessrights);
	switch (accsessnum)
	{
	case READ:
		it->access_rights = READ;
		break;
	case WRITE:
		it->access_rights = WRITE;
		break;
	case EXECUTE:
		it->access_rights = EXECUTE;
		break;
	case WRITE | READ:
		it->access_rights = WRITE | READ;
		break;
	case WRITE | EXECUTE:
		it->access_rights = WRITE | EXECUTE;
		break;
	case READ | EXECUTE:
		it->access_rights = READ | EXECUTE;
		break;
	case READ | WRITE | EXECUTE:
		it->access_rights = READ | WRITE | EXECUTE;
		break;
	case 0:
		it->access_rights = 0;
		break;
	default:
		return 1;
		break;
	}

	disk.write(dirtoload.first_blk, block);

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

//Returns the directort of filepath
dir_entry FS::find_dir_entry(std::string filepath)
{
	//Read root block.
	uint8_t block[BLOCK_SIZE] = {0};
	disk.read(ROOT_BLOCK, block);
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(block);

	std::string fullpath("");

	if (filepath.back() != '/')
		filepath.append("/");

	if (filepath.find("root") != 0u)
		filepath = path + filepath;

	size_t start_i = 0, end_i = 0, i = 0;

	while ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
	{
		i = 0;
		std::string dirname = filepath.substr(start_i, end_i - start_i);
		//Check if dir_entry name exists in this folder.
		while (dirname.compare(dirblock->file_name))
		{
			dirblock++;
			i++;
			if (i == std::floor(BLOCK_SIZE / sizeof(dir_entry)))
				return dir_entry{0};
		}

		start_i = ++end_i;

		//This folder exists, load in that folders disk block.
		if ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
		{
			disk.read(dirblock->first_blk, block);
			dirblock = reinterpret_cast<dir_entry *>(block);
		}
	}
	return (reinterpret_cast<dir_entry *>(block))[i];
}

int FS::updateSize(uint32_t size, std::string updateFrom)
{
	std::string currentDir = "";

	//If the last part of the path is a file. Remove it from the string so that the path only contains directories.
	if (find_dir_entry(updateFrom).type == 0)
	{
		while (updateFrom.back() != '/' && updateFrom != "")
			updateFrom.pop_back();
	}

	//if it is a relative path, make it an absolute path.
	if (updateFrom.find("root") == std::string::npos)
	{
		updateFrom = path + updateFrom;
	}

	dir_entry currentEntry = {0};
	uint8_t block[BLOCK_SIZE] = {0};

	while (updateFrom.find('/') != std::string::npos && updateFrom != "root/")
	{
		currentDir = "";

		//Extract the folder at the end of the path.
		size_t lastslash = updateFrom.find_last_of("/");
		currentDir = updateFrom.substr(lastslash + 1, updateFrom.size() - lastslash);

		//Do this for all fodlers except root, which is handled later.
		if (currentDir != "root")
		{
			//Get the dir for the current folder and increase the size.
			currentEntry = find_dir_entry(updateFrom);
			currentEntry.size += size;

			//Read this folder's block
			dir_entry *dirblock = reinterpret_cast<dir_entry *>(block);
			disk.read(currentEntry.first_blk, (uint8_t *)dirblock);

			//Save the index of the block where the directory lies.
			int block_number = dirblock->first_blk;
			//Then read the block where the directory was.
			disk.read(dirblock->first_blk, (uint8_t *)dirblock);

			//Find the spot where the dir is.
			int j = 0;
			while (std::strcmp(currentEntry.file_name, dirblock->file_name) && j < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
			{
				j++;
				dirblock++;
			}
			//Replace it with the entry that has an updated size.
			*dirblock = currentEntry;

			//Write back the block.
			disk.write(block_number, block);
		}

		if (updateFrom.back() == '/')
		{
			updateFrom.pop_back();
		}
		//Erase the found folder from the end of the path.
		updateFrom.erase(updateFrom.find(currentDir), currentDir.length());
		//Remove the slash at the end of the path.
		updateFrom.pop_back();
	}

	if (updateFrom.find('/') != std::string::npos)
	{
		updateFrom.pop_back();
	}

	//For the root map.
	//---------------------------------------------
	//Get the root dir and update its size.
	currentEntry = find_dir_entry(updateFrom);
	currentEntry.size += size;

	//Read the root block.
	dir_entry *dirblock = reinterpret_cast<dir_entry *>(block);
	disk.read(ROOT_BLOCK, block);

	//Put back the updated root directory.
	dirblock[0] = currentEntry;

	disk.write(ROOT_BLOCK, block);
	return 0;
}