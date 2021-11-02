#include <iostream>
#include "fs.h"

FS::FS()
{
	std::cout << "FS::FS()... Creating file system\n";
	disk.read(FAT_BLOCK, (uint8_t*)fat);
	path = "/";
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
	//Set the whole disk to 0.
	int nrBlocks = disk.get_no_blocks();
	uint8_t zeroblob[BLOCK_SIZE] = { 0 };
	for (int i = 0; i < nrBlocks; i++)
		disk.write(i, zeroblob);

	//Configure root direcotry block
	std::string name("/");
	uint8_t blob[BLOCK_SIZE] = { 0 };
	dir_entry* root = (dir_entry*)blob;
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
	disk.write(ROOT_BLOCK, (uint8_t*)root);
	disk.write(FAT_BLOCK, (uint8_t*)fat);

	path = "/";

	return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
	//Check if the filepath entered already exists.
	if (find_dir_entry(filepath).file_name[0] != '\0')
	{
		std::cerr << "Error! That file or directory already exists." << std::endl;
		return -1;
	}

	//Read input from user.
	std::string input = "", result = "";
	while (getline(std::cin, input) and !input.empty())
		result += input;

	//Calculate how many blocks will be needed for the string.
	size_t numBlocks = std::ceil((float)result.size() / (float)BLOCK_SIZE);
	std::vector<int> empty_spots(numBlocks);
	char strblock[BLOCK_SIZE] = { 0 };

	//If the string is smaller than the block size it is all put in one block.
	if (numBlocks <= 1)
	{
		//Find an empty spot in the FAT for the new file.
		empty_spots[0] = this->find_empty();
		if (empty_spots[0] == -1)
		{
			std::cerr << "ERROR! No empty spots in the FAT." << std::endl;
			return -1;
		}

		//Write the data to the disk.
		result.copy(strblock, BLOCK_SIZE);
		disk.write(empty_spots[0], (uint8_t*)strblock);
	}
	//Split the string in to BLOCK_SIZE big parts if the string is bigger than one BLOCK_SIZE and write to disk.
	else
	{
		//Find enough empty spots in the FAT for the new file.
		empty_spots = find_multiple_empty(numBlocks);
		if (empty_spots[0] == -1)
		{
			std::cerr << "ERROR! Not enough empty spots in the FAT." << std::endl;
			return -1;
		}

		//Copy string to strblock while the amount of copied characters is BLOCK_SIZE.
		size_t i = 0, j = 0;
		while (result.copy(strblock, BLOCK_SIZE, i) == BLOCK_SIZE)
		{
			disk.write(empty_spots[j], (uint8_t*)strblock);
			i += BLOCK_SIZE;
			j++;
			memset(strblock, '\0', BLOCK_SIZE);
		}

		//Write last block of the file to disk.
		disk.write(empty_spots[j], (uint8_t*)strblock);
	}
	dir_entry currentDir;

	//Check if there is a slash in the filepath
	//If there is not, the current path is fine.
	if (filepath.find('/') == std::string::npos)
		currentDir = find_dir_entry(this->path);

	//If there is a slash, remove the new file's name at the end of the filepath. Then put this path with only directories in tempPath.
	else
	{
		std::string tempPath = filepath;
		while (tempPath.back() != '/')
			tempPath.pop_back();
		currentDir = find_dir_entry(tempPath);
	}

	//Get the name of the new file and put it in lastdir.
	std::string lastdir = "";
	size_t lastslash = filepath.find_last_of("/");
	lastdir = filepath.substr(lastslash + 1, filepath.size() - lastslash);

	//Create the directory entry for the new file.
	dir_entry fentry;
	lastdir.copy(fentry.file_name, filepath.size());

	fentry.access_rights = READ | WRITE | EXECUTE;
	fentry.first_blk = empty_spots[0];
	fentry.type = TYPE_FILE;
	fentry.size = result.size();

	//Update all the sizes in the hierarchy.
	if (updateSize(fentry.size, filepath) == -1)
	{
		std::cerr << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	//Read current dir block.
	dir_entry* dirblock = (dir_entry*)strblock;
	disk.read(currentDir.first_blk, (uint8_t*)dirblock);

	//Find an empty spot for the new directory/file.
	size_t k = 1;
	while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
		k++;

	//If there is no empty spot.
	if (dirblock[k].file_name[0] != '\0')
	{
		std::cerr << "ERROR! No more space for dir_entries in the current directory." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		dirblock[k] = fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (size_t i = 0; i < empty_spots.size(); i++)
	{
		if (i + 1 < numBlocks)
			fat[empty_spots[i]] = empty_spots[i + 1];
		else
			fat[empty_spots[i]] = FAT_EOF;
	}

	//Uppdate the FAT and current directory block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t*)fat);
	disk.write(currentDir.first_blk, (uint8_t*)(dirblock));
	return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
	dir_entry entry = find_dir_entry(filepath); //Get the dir entry for the filepath entered.
	if (entry.file_name[0] == '\0')
	{
		std::cerr << "Error! File does not exist." << std::endl;
		return 1;
	}
	if (entry.type == 1)
	{
		std::cerr << "Error! That is a directory!" << std::endl;
		return -1;
	}
	if (!(entry.access_rights & READ))
	{
		std::cerr << "Error! You do not have access rights to read that file." << std::endl;
		return -1;
	}

	uint8_t block[BLOCK_SIZE] = { 0 };
	disk.read(entry.first_blk, block); //Read the first block.

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (int i = entry.first_blk; i != EOF; i = fat[i])
	{
		disk.read(i, block);
		for (size_t i = 0; i < BLOCK_SIZE; i++)
			std::cout << block[i];
	}
	std::cout << std::endl;
	return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
	//Read the current paths dir and block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* dirblock = (dir_entry*)buff;
	dir_entry currentDir = find_dir_entry(this->path);
	disk.read(currentDir.first_blk, (uint8_t*)dirblock);
	dir_entry* file_entry = nullptr;
	file_entry = dirblock; //Set the first file entry to be the start of the directory block.

	//Check the access rights and construct the string.
	std::string accessRights = "";
	if (file_entry->access_rights & READ)
		accessRights.append("r");
	else
		accessRights.append("-");

	if (file_entry->access_rights & WRITE)
		accessRights.append("w");
	else
		accessRights.append("-");

	if (file_entry->access_rights & EXECUTE)
		accessRights.append("x");
	else
		accessRights.append("-");

	//Write out the current blocks content.
	std::cout << file_entry->file_name << "\t" << (int)file_entry->type << "\t" << accessRights << "\t" << (int)file_entry->size << std::endl;

	for (long unsigned int i = 1; i < BLOCK_SIZE / sizeof(dir_entry); i++)
	{
		accessRights = "";
		file_entry++;
		if (file_entry->file_name[0] != '\0')
		{
			if (file_entry->access_rights & READ)
				accessRights.append("r");
			else
				accessRights.append("-");

			if (file_entry->access_rights & WRITE)
				accessRights.append("w");
			else
				accessRights.append("-");

			if (file_entry->access_rights & EXECUTE)
				accessRights.append("x");
			else
				accessRights.append("-");

			std::cout << file_entry->file_name << "\t" << (int)file_entry->type << "\t" << accessRights << "\t" << (int)file_entry->size << std::endl;
		}
	}
	return 0;
}

// cp <sourcefilepath> <destfilepath> makes an exact copy of the file
// <sourcefilepath> to a new file <destfilepath>
int FS::cp(std::string sourcefilepath, std::string destfilepath)
{
	//Find the dir_entry for the source.
	dir_entry sourceDir = find_dir_entry(sourcefilepath);
	if (sourceDir.file_name[0] == '\0')
	{
		std::cerr << "Error! Source not found." << std::endl;
		return -1;
	}

	if (sourceDir.type == 1)
	{
		std::cerr << "Error! Source is a directory, not a file." << std::endl;
		return -1;
	}

	if (find_dir_entry(destfilepath).file_name[0] != '\0')
	{
		std::cerr << "Error! Destination already exists." << std::endl;
		return -1;
	}

	//Calculate the number of blocks that the source occupies.
	size_t nrBlocks = std::ceil((float)sourceDir.size / (float)BLOCK_SIZE);
	std::vector<int> empty_spots(nrBlocks);
	int dataSize = 0;
	//If it just occupies one or zero blocks.
	if (nrBlocks == 1)
	{
		//Find one empty spot.
		empty_spots[0] = find_empty();
		if (empty_spots[0] == -1)
		{
			std::cerr << "ERROR! No empty spots in the FAT." << std::endl;
			return -1;
		}

		//Read the source data from the disk.
		uint8_t sourceBlock[BLOCK_SIZE] = { 0 };
		disk.read(sourceDir.first_blk, sourceBlock);
		std::string s((char*)sourceBlock);

		//Add the size of the block to the dataSize
		dataSize += s.length();

		//Write the data to the disk in the new destination.
		disk.write(empty_spots[0], sourceBlock);
	}
	else if (nrBlocks != 0) //If the file data occupies more than 1 block.
	{
		empty_spots = find_multiple_empty(nrBlocks);
		if (empty_spots[0] == -1)
		{
			std::cerr << "ERROR! Not enough empty spots in the FAT." << std::endl;
			return -1;
		}

		uint8_t sourceBlock[BLOCK_SIZE] = { 0 };
		int fatNr = sourceDir.first_blk;

		//Write over all the data to the found free blocks.
		size_t i = 0;
		while (i < nrBlocks)
		{
			disk.read(fatNr, sourceBlock);
			std::string s((char*)sourceBlock);

			if (i != nrBlocks - 1)
				s.pop_back(); //Remove weird null character

			//Add the size of the block to the dataSize
			dataSize += s.size();
			disk.write(empty_spots[i], sourceBlock);
			memset(sourceBlock, '\0', BLOCK_SIZE);
			fatNr = fat[fatNr];
			i++;
		}
	}

	//If the destination path is a relative path, make it absolute.
	if (destfilepath[0] != '/')
		destfilepath = path + destfilepath;

	//Get the name of the destination file, if there is a slash in the destfilepath.
	std::string temppath = destfilepath;
	temppath = destfilepath.substr(destfilepath.find_last_of('/') + 1, destfilepath.length() - 1);
	destfilepath.erase(destfilepath.find_last_of('/'), destfilepath.length() - 1);

	dir_entry currentDir = find_dir_entry(destfilepath);

	//Create the directory entry for the new file.
	dir_entry fentry;
	temppath.copy(fentry.file_name, temppath.size());
	fentry.access_rights = READ | WRITE | EXECUTE;
	fentry.first_blk = empty_spots[0];
	fentry.type = TYPE_FILE;
	fentry.size = dataSize;

	//Update folders sizes.
	if (updateSize(fentry.size, destfilepath) == -1)
	{
		std::cerr << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	//Read current directory block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* dirblock = (dir_entry*)buff;
	disk.read(currentDir.first_blk, (uint8_t*)dirblock);

	//Find an empty spot for the new directory.
	size_t k = 1;
	while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry)) //Fel antal
		k++;

	//If there is no empty spot.
	if (dirblock[k].file_name[0] != '\0')
	{
		std::cerr << "ERROR! No more space for dir_entries in the current block." << std::endl;
		return -1;
	}
	else //Put the new directory in the empty spot.
		dirblock[k] = fentry;

	//Update the FAT table so that it is consistent with the newly added file.
	for (size_t i = 0; i < empty_spots.size(); i++)
	{
		if (i + 1 < nrBlocks)
			fat[empty_spots[i]] = empty_spots[i + 1];
		else
			fat[empty_spots[i]] = FAT_EOF;
	}

	//Uppdate the FAT and current directory block ON THE DISK.
	disk.write(FAT_BLOCK, (uint8_t*)fat);
	disk.write(currentDir.first_blk, (uint8_t*)dirblock);
	return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
	dir_entry currentDir;
	destpath = path + destpath;
	std::string tempPath = destpath;
	while (destpath.back() != '/')
		destpath.pop_back();

	if (destpath.size() != 1)
		destpath.pop_back();

	dir_entry destDir = find_dir_entry(destpath);

	currentDir = find_dir_entry(sourcepath);
	if (currentDir.file_name[0] == '\0')
	{
		std::cerr << "Error! The source file does not exist!" << std::endl;
		return -1;
	}
	if (currentDir.type == 1)
	{
		std::cerr << "Error! The source is a directory, not a file." << std::endl;
		return -1;
	}

	if (destDir.type == 1) //Move if dest is a directory.
	{
		dir_entry tempDir = find_dir_entry(tempPath);
		if (tempDir.file_name[0] != '\0')
			rm(tempPath);
		cp(sourcepath, tempPath);
		rm(sourcepath);

		return 0;
	}
	else if ((destDir.file_name[0] != '\0' && destDir.type == 0)) //OR if the name is taken and it is a file
	{
		rm(destpath);
		cp(sourcepath, destpath);
		rm(sourcepath);

		return 0;
	}

	//If filename is too long
	if (destpath.length() > 56)
	{
		std::cerr << "Error! New filename is too long!" << std::endl;
		return -1;
	}

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* dirblock = (dir_entry*)buff;

	//If the path is relative, make it absolute.
	if (sourcepath[0] != '/')
		sourcepath = path + sourcepath;

	//Get the name of the file, if what was entered was a path.
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
	disk.write(currentDir.first_blk, (uint8_t*)buff);
	return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
	uint8_t block[BLOCK_SIZE] = { 0 };
	std::string temppath = "";

	if (filepath[0] != '/') //Check if path is absolute.
		filepath = path + filepath;

	//Remove last folder/file from path.
	temppath = filepath.substr(filepath.find_last_of('/') + 1, filepath.length() - 1);
	filepath.erase(filepath.find_last_of('/') + 1, filepath.length() - 1);

	if (filepath.size() > 1)
		filepath.pop_back();

	dir_entry currentDir = find_dir_entry(filepath);
	disk.read(currentDir.first_blk, block);

	if (currentDir.file_name[0] == '\0')
		return -1;

	dir_entry* entry = nullptr;
	size_t i = 0;
	//Look for the dir_entry in current block.
	for (dir_entry* it = (dir_entry*)block; temppath.compare(it->file_name); entry = ++it)
	{
		if (i++ > std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		{
			std::cerr << "Could not find file or directory on path: " << filepath << std::endl;
			return 1;
		}
	}

	int save = 0;
	for (int i = fat[entry->first_blk];; i = save)
	{
		if (i == EOF)
		{
			fat[save] = EOF;
			break;
		}
		save = fat[i];
		fat[i] = FAT_FREE;
		if (i == EOF)
			break;
	}

	entry->access_rights = 0u;
	for (size_t i = 0; i < 56; i++)
		entry->file_name[i] = '\0';
	entry->first_blk = 0u;
	uint32_t tempSize = entry->size; //Save the size for the update function.
	entry->size = 0u;
	entry->type = 0u;
	disk.write(currentDir.first_blk, block);

	if (updateSize(-tempSize, filepath) == -1)
	{
		std::cerr << "Error! Could not update the sizes." << std::endl;
		return -1;
	}

	return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
	dir_entry entry1 = find_dir_entry(filepath1);
	dir_entry entry2 = find_dir_entry(filepath2);
	if (entry1.file_name[0] == '\0' or entry2.file_name[0] == '\0')
	{
		std::cerr << "Path not valid." << std::endl;
		return -1;
	}
	uint8_t file1[BLOCK_SIZE];
	size_t binlastblock2 = entry2.size % BLOCK_SIZE;

	size_t lastblockfree = BLOCK_SIZE - binlastblock2 - 1;
	float diff = std::ceil(((float)entry1.size - (float)lastblockfree) / (float)BLOCK_SIZE);
	size_t newblocksneeded = diff < 0 ? 0 : diff;
	uint8_t file2[BLOCK_SIZE];
	int lastfatfile2 = entry2.first_blk;
	while (fat[lastfatfile2] != EOF) //Retrive the last block of a file.
		lastfatfile2 = fat[lastfatfile2];
	int originallastblock = lastfatfile2;
	std::vector<int> zero;
	zero.push_back(0);
	auto empty = (newblocksneeded > 0) ? find_multiple_empty(newblocksneeded) : zero; //Find appropriate amount of needed empty blocks

	if (newblocksneeded == 0)
	{
		disk.read(lastfatfile2, file2);
		disk.read(entry1.first_blk, file1);
		uint8_t* it1 = file1;
		uint8_t* it2 = file2 + binlastblock2;						  //End of file2.
		for (size_t i = 0; i <= entry1.size; i++, it2++, it1++) //Copy part of first block of file1 to lst block of file2.
			*it2 = *it1;
		disk.write(lastfatfile2, file2);

		if (updateSize(entry1.size, filepath2) == -1) //Update the sizes after the move.
		{
			std::cerr << "Error! Could not update the sizes." << std::endl;
			return -1;
		}
		return 0;
	}
	else
	{
		uint8_t* it1 = file1;					  // Copy block pointer (start)
		uint8_t* it2 = file2 + binlastblock2; // File Endpoint
		size_t file1blocktoread = entry1.first_blk;
		size_t byteswandered = 0, i = 0;
		size_t bytestocopy = lastblockfree;
		disk.read(lastfatfile2, file2);
		disk.read(entry1.first_blk, file1);
		while (byteswandered < entry1.size)
		{
			if (it1 == file1 + BLOCK_SIZE)
			{
				file1blocktoread = fat[file1blocktoread];
				disk.read(file1blocktoread, file1);
				it1 = file1;
			}
			if (file1blocktoread == (size_t)EOF)
				bytestocopy = entry1.size - byteswandered;
			else
				bytestocopy = std::min((file1 + BLOCK_SIZE) - it1, (file2 + BLOCK_SIZE) - it2);

			for (size_t i = 0; i < bytestocopy; i++, it2++, it1++, byteswandered++) //Copy part of first block of file1 to lst block of file2.
				*it2 = *it1;

			if (it2 == file2 + BLOCK_SIZE)
			{
				disk.write(lastfatfile2, file2);
				for (size_t i = 0; i < BLOCK_SIZE; i++)
					file2[i] = '\0';
				lastfatfile2 = empty[i++];
				it2 = file2;
			}
		}
	}

	for (size_t j = 0; j < empty.size(); j++)
	{
		fat[originallastblock] = empty[j];
		originallastblock = fat[originallastblock];
	}
	fat[originallastblock] = EOF;

	if (updateSize(entry1.size, filepath2) == -1) //Update the sizes after the append.
	{
		std::cerr << "Error! Could not update the sizes." << std::endl;
		return -1;
	}
	return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
	dir_entry currentDir = find_dir_entry(dirpath);
	if (currentDir.file_name[0] != '\0')
	{
		std::cerr << "Error! That name already exists!" << std::endl;
		return -1;
	}

	if (dirpath[0] != '/')
		dirpath = path + dirpath;

	//Get the name of the new dir.
	std::string temppath = "";

	temppath = dirpath.substr(dirpath.find_last_of('/') + 1, dirpath.length() - 1);
	dirpath.erase(dirpath.find_last_of('/'), dirpath.length() - 1);

	currentDir = find_dir_entry(dirpath);
	//New directory
	dir_entry newDir;
	temppath.copy(newDir.file_name, 56);
	std::vector<int> empty_spot(1);
	empty_spot[0] = find_empty();
	newDir.first_blk = empty_spot[0];
	newDir.size = 0;
	newDir.type = 1;
	newDir.access_rights = READ | WRITE | EXECUTE;

	//Return directory
	dir_entry returnDir;
	returnDir.file_name[0] = '.';
	returnDir.file_name[1] = '.';
	returnDir.first_blk = currentDir.first_blk; // kanske fel.
	returnDir.size = 0;
	returnDir.type = 1;
	returnDir.access_rights = READ | WRITE | EXECUTE;

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* currentblock = (dir_entry*)(buff);
	disk.read(currentDir.first_blk, (uint8_t*)currentblock);

	//Find an empty spot for the new directory.
	unsigned int k = 1;
	while (currentblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
		k++;

	//If there is no empty spot.
	if (currentblock[k].file_name[0] != '\0')
	{
		std::cerr << "ERROR! No more space for dir_entries in the current block." << std::endl;
		return -1;
	}
	//Put the new directory in the empty spot.
	else
		currentblock[k] = newDir;

	//Write back the current block.
	disk.write(currentDir.first_blk, (uint8_t*)currentblock);

	//Read new block, that is for the new directory entry.
	uint8_t buff2[BLOCK_SIZE] = { 0 };
	dir_entry* newblock = (dir_entry*)buff2;
	newblock[0] = returnDir;

	//Write the return dir.
	disk.write(empty_spot[0], (uint8_t*)newblock);

	//Update fat.
	fat[empty_spot[0]] = FAT_EOF;
	disk.write(FAT_BLOCK, (uint8_t*)fat);

	return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
	if (find_dir_entry(dirpath).file_name[0] == '\0') //Checks if path is valid.
		return -1;

	if (dirpath.back() != '/')
		dirpath.append("/");
	if (dirpath[0] == '/') //Absolute path.
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
	std::cout << path << std::endl;
	return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
	dir_entry valid = find_dir_entry(filepath);
	if (*valid.file_name == '\0')
		return 1;

	uint8_t block[BLOCK_SIZE];

	if (filepath[0] != '/')
		filepath = path + filepath;

	//Remove last folder/file from filepath.
	size_t lastslash = filepath.find_last_of("/");
	filepath.erase(lastslash + 1, filepath.size() - lastslash);

	//Retrive the dir etntry of the dir/file to be changed.
	dir_entry dirtoload = find_dir_entry(filepath);

	disk.read(dirtoload.first_blk, block);

	//Look for valid in block.
	dir_entry* it;
	for (it = (dir_entry*)block; std::strcmp(it->file_name, valid.file_name); it++);

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
	//After the root and FAT entry.
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

		if (fat[j] == FAT_FREE) //If it found a free spot.
		{
			empty_spots[i] = j;
			j++;
		}
		else //If it did not, then there are none.
			empty_spots[0] = -1;
	}

	return empty_spots;
}

//Returns the dir_entry of filepath
dir_entry FS::find_dir_entry(std::string filepath)
{
	///TODO: Rerwrite this whole finction since we do not have a folder named root anymore. And there is a simpler way of doing this.
	//Read root block.
	uint8_t block[BLOCK_SIZE] = { 0 };
	disk.read(ROOT_BLOCK, block);
	dir_entry* dirblock = (dir_entry*)(block);

	std::string fullpath("");

	// if (filepath.back() != '/')
	// 	filepath.append("/");

	// if (filepath.find("/") != 0u)
	// 	filepath = path + filepath;

	//filepath = "root" + filepath;

	size_t start_i = 0, end_i = 0, i = 0;

	while ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
	{
		i = 0;
		std::string dirname = filepath.substr(start_i, end_i - start_i);
		while (dirname.compare(dirblock->file_name)) //Check if dir_entry name exists in this folder.
		{
			dirblock++;
			i++;
			if (i == std::floor(BLOCK_SIZE / sizeof(dir_entry)))
				return dir_entry();
		}

		start_i = ++end_i;

		//This folder exists, load in that folders disk block.
		if ((end_i = filepath.find('/', end_i)) not_eq std::string::npos)
		{
			disk.read(dirblock->first_blk, block);
			dirblock = (dir_entry*)block;
		}
	}
	return ((dir_entry*)block)[i];
}

int FS::updateSize(uint32_t size, std::string updateFrom)
{
	std::string currentDir = "";

	//If the last part of the path is a file. Remove it from the string so that the path only contains directories.
	if (find_dir_entry(updateFrom).type == 0)
	{

		dir_entry file = find_dir_entry(updateFrom);
		file.size += size;

		while (updateFrom.back() != '/' && updateFrom != "")
			updateFrom.pop_back();

		if (updateFrom.back() == '/' && updateFrom.size() > 1)
			updateFrom.pop_back();

		dir_entry homeFolder = find_dir_entry(updateFrom);
		uint8_t block[BLOCK_SIZE] = { 0 };
		disk.read(homeFolder.first_blk, block);
		dir_entry* entry = (dir_entry*)block;
		int i = 0;
		while (std::strcmp(file.file_name, entry->file_name) && i < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		{
			i++;
			entry++;
		}
		*entry = file;

		disk.write(homeFolder.first_blk, block);
	}

	//if it is a relative path, make it an absolute path.
	if (updateFrom[0] != '/')
		updateFrom = path + updateFrom;

	dir_entry currentEntry;
	uint8_t block[BLOCK_SIZE] = { 0 };

	while (updateFrom.find('/') != std::string::npos && updateFrom != "/")
	{
		currentDir = "";

		//Extract the folder at the end of the path.
		size_t lastslash = updateFrom.find_last_of("/");
		if (updateFrom.size() > 1)
			currentDir = updateFrom.substr(lastslash + 1, updateFrom.size() - 1);
		else
			currentDir = "/";

		//Do this for all folders except root, which is handled later.
		if (currentDir != "/" && currentDir != "..")
		{
			//Get the dir for the current folder and increase the size.
			currentEntry = find_dir_entry(updateFrom);
			currentEntry.size += size;

			//Read this folder's block
			dir_entry* dirblock = (dir_entry*)block;
			disk.read(currentEntry.first_blk, (uint8_t*)dirblock);

			//Save the index of the block where the directory lies.
			int block_number = dirblock->first_blk;
			//Then read the block where the directory was.
			disk.read(dirblock->first_blk, (uint8_t*)dirblock);

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
			updateFrom.pop_back();

		if (currentDir == "..")
		{
			updateFrom.erase(updateFrom.find(currentDir), currentDir.length());
			updateFrom.pop_back();
			while (updateFrom.back() != '/')
				updateFrom.pop_back();
		}
		else
			updateFrom.erase(updateFrom.find(currentDir), currentDir.length()); //Erase the found folder from the end of the path.

		//Remove the slash at the end of the path.
		updateFrom.pop_back();
	}

	updateFrom = "/";

	//For the root map.
	//---------------------------------------------
	//Get the root dir and update its size.
	currentEntry = find_dir_entry(updateFrom);
	currentEntry.size += size;

	//Read the root block.
	dir_entry* dirblock = (dir_entry*)block;
	disk.read(ROOT_BLOCK, block);

	//Put back the updated root directory.
	dirblock[0] = currentEntry;

	disk.write(ROOT_BLOCK, block);
	return 0;
}