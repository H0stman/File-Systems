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
	std::cout << "FS::format()\n";

	//Mark root dir and FAT as EOF in the FAT
	fat[0] = FAT_EOF;
	fat[1] = FAT_EOF;

	uint8_t block[BLOCK_SIZE] = { 0 };

	disk.write(ROOT_BLOCK, block);
	for (unsigned int i = 2; i < disk.get_no_blocks(); i++)
		disk.write(i, block);

	dir_entry* entry = (dir_entry*)block;

	entry->access_rights = READ | WRITE | EXECUTE;
	entry->file_name[0] = '/';
	entry->first_blk = ROOT_BLOCK;
	entry->size = 0;
	entry->type = TYPE_DIR;

	//Mark rest of blocks as FAT_FREE
	for (unsigned int i = 2; i < disk.get_no_blocks(); i++)
		fat[i] = FAT_FREE;

	//Write blocks to disk
	disk.write(FAT_BLOCK, (uint8_t*)fat);
	disk.write(ROOT_BLOCK, block);

	path = "/";

	return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
	std::cout << "FS::create(" << filepath << ")\n";

	auto exist = get_entry(filepath);

	//Check if the filepath entered already exists.
	if (exist)
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
	dir_entry* currentDir;

	//Check if there is a slash in the filepath
	//If there is not, the current path is fine.
	if (filepath.find('/') == std::string::npos)
		currentDir = get_entry(this->path);

	//If there is a slash, remove the new file's name at the end of the filepath. Then put this path with only directories in tempPath.
	else
	{
		std::string tempPath = filepath;
		while (tempPath.back() != '/')
			tempPath.pop_back();
		currentDir = get_entry(tempPath);
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
	// if (updateSize(fentry.size, filepath) == -1)
	// {
	// 	std::cerr << "Error! Could not update the sizes." << std::endl;
	// 	return -1;
	// }

	//Read current dir block.
	dir_entry* dirblock = (dir_entry*)strblock;
	disk.read(currentDir->first_blk, (uint8_t*)dirblock);

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
	disk.write(currentDir->first_blk, (uint8_t*)(dirblock));
	return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
	std::cout << "FS::cat(" << filepath << ")\n";

	dir_entry* entry = get_entry(filepath); //Get the dir entry for the filepath entered.
	if (!entry)
	{
		std::cerr << "Error! File does not exist." << std::endl;
		return 1;
	}
	if (entry->type == TYPE_DIR)
	{
		std::cerr << "Error! That is a directory!" << std::endl;
		return -1;
	}
	if (!(entry->access_rights & READ))
	{
		std::cerr << "Error! You do not have access rights to read that file." << std::endl;
		return -1;
	}

	uint8_t block[BLOCK_SIZE] = { 0 };
	disk.read(entry->first_blk, block); //Read the first block.

	//This for-loop will start on the first block of the file and jump to the next block which the file is occupying in the FAT until it reaches FAT_EOF.
	for (int i = entry->first_blk; i != EOF; i = fat[i])
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
	std::cout << "FS::ls()\n";

	//Read the current paths dir and block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* currentDir = get_entry(this->path);
	if (!(currentDir->access_rights & EXECUTE))
	{
		std::cerr << "Error! You do not have access rights to execute this folder. Therefore you can not list its files." << std::endl;
		return -1;
	}
	disk.read(currentDir->first_blk, buff);
	dir_entry* file_entry = (dir_entry*)buff; //Set the first file entry to be the start of the directory block.

	for (size_t i = 0; i < BLOCK_SIZE / sizeof(dir_entry); ++i, ++file_entry)
	{
		if (file_entry->file_name[0] == '/')
			continue;

		std::string accessRights = "";

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
	std::cout << "FS::cp(" << sourcefilepath << "," << destfilepath << ")\n";

	//Find the dir_entry for the source.
	dir_entry* sourceDir = get_entry(sourcefilepath);
	if (sourceDir->file_name[0] == '\0')
	{
		std::cerr << "Error! Source not found." << std::endl;
		return -1;
	}

	if (sourceDir->type == TYPE_DIR)
	{
		std::cerr << "Error! Source is a directory, not a file." << std::endl;
		return -1;
	}

	if (get_entry(destfilepath))
	{
		std::cerr << "Error! Destination already exists." << std::endl;
		return -1;
	}

	if (!(sourceDir->access_rights & WRITE))
	{
		std::cerr << "Error! You do not have access rights to write to that file. Therefore you can not copy it." << std::endl;
		return -1;
	}

	//Calculate the number of blocks that the source occupies.
	size_t nrBlocks = std::ceil((float)sourceDir->size / (float)BLOCK_SIZE);
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
		disk.read(sourceDir->first_blk, sourceBlock);
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
		int fatNr = sourceDir->first_blk;

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

	dir_entry* currentDir = get_entry(destfilepath);

	//Create the directory entry for the new file.
	dir_entry fentry;
	temppath.copy(fentry.file_name, temppath.size());
	fentry.access_rights = READ | WRITE | EXECUTE;
	fentry.first_blk = empty_spots[0];
	fentry.type = TYPE_FILE;
	fentry.size = dataSize;

	//Update folders sizes.
	// if (updateSize(fentry.size, destfilepath) == -1)
	// {
	// 	std::cerr << "Error! Could not update the sizes." << std::endl;
	// 	return -1;
	// }

	//Read current directory block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* dirblock = (dir_entry*)buff;
	disk.read(currentDir->first_blk, (uint8_t*)dirblock);

	//Find an empty spot for the new directory.
	size_t k = 1;
	while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
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
	disk.write(currentDir->first_blk, (uint8_t*)dirblock);
	return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
	std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

	//Create a buffer, used when reading blocks.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* dirblock = (dir_entry*)buff;

	//SOURCE ---------------------------------------------------------------
	//Chelsck if the path is relative or absolute.
	//If front is not /, then the path is relative -> Make it absolute.
	if (sourcepath.front() != '/')
		sourcepath = path + sourcepath;

	dir_entry* sourceDir = get_entry(sourcepath);
	//If it is a directory or doesnt exist at all.
	if (sourceDir->type == TYPE_DIR || !sourceDir)
	{
		std::cerr << "Error! The source file does not exist!" << std::endl;
		return -1;
	}
	//-----------------------------------------------------------------------

	//DEST ------------------------------------------------------------------
	if (destpath.size() == 0 || sourcepath == destpath)
	{
		std::cerr << "Error! A valid path has to be entered. Can not move a file to the same position it is already in, or rename a file to the same name it already has." << std::endl;
		return -1;
	}
	//Check if the path is relative or absolute.
	//If front is not /, then the path is relative -> Make it absolute.
	if (destpath.front() != '/')
		destpath = path + destpath;

	dir_entry* destDir = get_entry(destpath);
	//If the dir_entry doesn't exist check the same path except the last file/folder.
	if (!destDir)
	{
		//Save the old filename of source. We will need it when removing the source dir at the end.
		std::string oldName = sourceDir->file_name;

		//Get the shortdest path (remove the last file/folder from the path.)
		std::string shortdest = destpath;
		//Remove the last dir/file.
		while (shortdest.back() != '/')
		{
			shortdest.pop_back();
		}
		//Remove the slash if it is not root.
		if (shortdest.size() != 1)
			shortdest.pop_back();

		//Check so that the "smaller" destination exists and that it is a folder. We can not move something into a file.
		destDir = get_entry(shortdest);
		if (destDir->type == TYPE_FILE || !destDir)
		{
			std::cerr << "Error! The destination path does not exist!" << std::endl;
			return -1;
		}

		uint16_t destblockNr;
		//If it is the root block slash then move the file to the rootblock.
		if (shortdest.size() == 1)
		{
			destblockNr = ROOT_BLOCK;
		}

		//If it was not the root slash remove it and we now know that the source is to be moved into a destination that is not "/".
		else
		{
			//Here we know that destDir is pointing at a folder that is not "/", and that the source is to be moved into
			//this folder.
			destblockNr = destDir->first_blk;
		}

		//Create a buffer
		memset(buff, 0, 4096);
		dirblock = (dir_entry*)buff;

		//Read the block that it is to be moved to.
		disk.read(destblockNr, buff);

		//Find an empty spot for the dir.
		size_t k = 1;
		while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
			k++;

		//If there is no empty spot.
		if (dirblock[k].file_name[0] != '\0')
		{
			std::cerr << "ERROR! No more space for dir_entries in the root block." << std::endl;
			return -1;
		}
		//Put the source directory in the empty spot, after changing its name.
		else
		{
			//Set the sourceDir name to the appropriate name.
			std::vector<std::string> split_destpath = split_path(destpath);
			std::string newName = split_destpath[split_destpath.size() - 1];
			memset(sourceDir->file_name, '\0', 56);
			newName.copy(sourceDir->file_name, newName.size());
			dirblock[k] = *sourceDir;
		}

		//We only need to update the size when not adding to root..
		if (destblockNr != ROOT_BLOCK)
		{
			// if (updateSize(sourceDir->size, shortdest) == -1)
			// {
			// 	std::cerr << "Error! Could not update the sizes." << std::endl;
			// 	return -1;
			// }
		}
		//Write it back to disk.
		disk.write(destblockNr, (uint8_t*)buff);


		//We now also need to remove the dir_entry from the source.
		//Get the dir of the folder the sourcefile is in.
		std::string shortsource = sourcepath;
		//Remove the filename.
		while (shortsource.back() != '/')
		{
			shortsource.pop_back();
		}

		//If the sourcefile is in root.
		uint16_t sourceblockNr;
		if (shortsource.size() == 1)
		{
			sourceblockNr = ROOT_BLOCK;
		}
		//sourcefile is not in root. Find the dir of the folder and what block the dir is pointing to.
		else
		{
			//Remove the slash at the front.
			shortsource.pop_back();
			sourceblockNr = get_entry(shortsource)->first_blk;
		}

		//Create a buffer
		memset(buff, 0, 4096);
		dirblock = (dir_entry*)buff;

		//Read the rootblock.
		disk.read(sourceblockNr, buff);

		//Find the dir.
		k = 1;
		while (dirblock[k].file_name != oldName && k < BLOCK_SIZE / sizeof(dir_entry))
			k++;

		//Make the dir_entry 0.
		dirblock[k] = dir_entry(); //?????

		//We do not need to update the size of anything in the tree when removing something from root.
		//Only update size if it was not in the rootblock.
		if (sourceblockNr != ROOT_BLOCK)
		{
			// if (updateSize(-sourceDir->size, shortsource) == -1)
			// {
			// 	std::cerr << "Error! Could not update the sizes." << std::endl;
			// 	return -1;
			// }
		}

		//Write it back to disk after removing the old dir_entry.
		disk.write(sourceblockNr, (uint8_t*)buff);

		return 0;
	}

	//If the dir_Entry exists it can be either:
	//1. A folder that the file is to be moved to.
	//2. A file that is to be "overwritten". UPDATE FAT TABLE.
	else
	{
		//It is a folder.
		if (destDir->type == TYPE_DIR)
		{
			//If it is a folder we now need to check if the name of the source is occupied in the folder.
			//If it is occupied by a file we overwrite it. UPDATE FAT TABLE.
			//If it is occupied by a folder we throw an error.
			std::vector<std::string> split_sourcepath = split_path(sourcepath);
			std::string name = split_sourcepath[split_sourcepath.size() - 1];
			std::string longpath = destpath + '/' + name;
			dir_entry* replaced_dir = get_entry(longpath);
			//If it exists
			if (replaced_dir)
			{
				//Check if it is a folder or file.
				//A folder cant be overwritten.
				if (replaced_dir->type == TYPE_DIR)
				{
					std::cerr << "Error! The filename is taken by a folder in the destination folder." << std::endl;
					return -1;
				}
				//Overwrite the file. UPDATE FAT TABLE.
				else
				{
					//First we update the FAT table to remove the destination file that we are to "overwrite".
					//Update fat table.
					int save = 0;
					for (int i = fat[replaced_dir->first_blk];; i = save)
					{
						if (i == EOF)
						{
							fat[save] = EOF;
							break;
						}
						save = fat[i];
						fat[i] = FAT_FREE;
					}
					//Remove first fat entry??? in fat[replaced_dir->first_blk]
					fat[replaced_dir->first_blk] = FAT_FREE;

					//Add the dir to the destination.
					//Create a buffer
					memset(buff, 0, 4096);
					dirblock = (dir_entry*)buff;

					uint16_t destblockNr = destDir->first_blk;

					//Read the block.
					disk.read(destblockNr, buff);

					//Find the dir that is too be replaced.
					size_t k = 1;
					while (dirblock[k].file_name != replaced_dir->file_name && k < BLOCK_SIZE / sizeof(dir_entry))
						k++;

					//Replace the info in that directory with the source dir information.
					//We already know the name is correct.
					dirblock[k] = *sourceDir;

					//Only update the sizes if its not in rootblock.
					if (destblockNr != ROOT_BLOCK)
					{
						//Update size by first subtracting the replaced dir.
						// if (updateSize(-replaced_dir->size, destpath) == -1)
						// {
						// 	std::cerr << "Error! Could not update the sizes." << std::endl;
						// 	return -1;
						// }

						//Then we update the size again to account for the newly added sourcefile in the destination.
						// if (updateSize(sourceDir->size, destpath) == -1)
						// {
						// 	std::cerr << "Error! Could not update the sizes." << std::endl;
						// 	return -1;
						// }
					}

					//Write it back to disk.
					disk.write(destblockNr, (uint8_t*)buff);

					//We now also need to remove the dir_entry from the source.
					//Get the dir of the folder the sourcefile is in.
					std::string shortsource = sourcepath;
					//Remove the filename.
					while (shortsource.back() != '/')
					{
						shortsource.pop_back();
					}

					//If the sourcefile is in root.
					uint16_t sourceblockNr;
					if (shortsource.size() == 1)
					{
						sourceblockNr = ROOT_BLOCK;
					}
					//sourcefile is not in root. Find the dir of the folder and what block the dir is pointing to.
					else
					{
						//Remove the slash at the front.
						shortsource.pop_back();
						sourceblockNr = get_entry(shortsource)->first_blk;
					}

					//Create a buffer
					memset(buff, 0, 4096);
					dirblock = (dir_entry*)buff;

					//Read the rootblock.
					disk.read(sourceblockNr, buff);

					//Find the dir.
					k = 1;
					while (dirblock[k].file_name != sourceDir->file_name && k < BLOCK_SIZE / sizeof(dir_entry))
						k++;

					//Make the dir_entry 0.
					dirblock[k] = dir_entry(); //?????

					//Only update size if it was not in the rootblock.
					if (sourceblockNr != ROOT_BLOCK)
					{
						// if (updateSize(-sourceDir->size, shortsource) == -1)
						// {
						// 	std::cerr << "Error! Could not update the sizes." << std::endl;
						// 	return -1;
						// }
					}

					//Write it back to disk after removing the old dir_entry.
					disk.write(sourceblockNr, (uint8_t*)buff);

					return 0;
				}
			}
			//If it does not exist we just move the file here.
			else
			{
				//Add the dir to the destination.
				//Create a buffer
				memset(buff, 0, 4096);
				dirblock = (dir_entry*)buff;

				uint16_t destblockNr = destDir->first_blk;

				//Read the block.
				disk.read(destblockNr, buff);

				//Find an empty spot for the dir.
				size_t k = 1;
				while (dirblock[k].file_name[0] != '\0' && k < BLOCK_SIZE / sizeof(dir_entry))
					k++;

				//If there is no empty spot.
				if (dirblock[k].file_name[0] != '\0')
				{
					std::cerr << "ERROR! No more space for dir_entries in the block." << std::endl;
					return -1;
				}
				//Put the source directory in the empty spot, after changing its name.
				dirblock[k] = *sourceDir;

				//Only update the sizes if its not in rootblock.
				if (destblockNr != ROOT_BLOCK)
				{
					//We update the size to account for the newly added sourcefile in the destination.
					// if (updateSize(sourceDir->size, destpath) == -1)
					// {
					// 	std::cerr << "Error! Could not update the sizes." << std::endl;
					// 	return -1;
					// }
				}

				//Write it back to disk.
				disk.write(destblockNr, (uint8_t*)buff);

				//We now also need to remove the dir_entry from the source.
				//Get the dir of the folder the sourcefile is in.
				std::string shortsource = sourcepath;
				//Remove the filename.
				while (shortsource.back() != '/')
				{
					shortsource.pop_back();
				}

				//If the sourcefile is in root.
				uint16_t sourceblockNr;
				if (shortsource.size() == 1)
				{
					sourceblockNr = ROOT_BLOCK;
				}
				//sourcefile is not in root. Find the dir of the folder and what block the dir is pointing to.
				else
				{
					//Remove the slash at the front.
					shortsource.pop_back();
					sourceblockNr = get_entry(shortsource)->first_blk;
				}

				//Create a buffer
				memset(buff, 0, 4096);
				dirblock = (dir_entry*)buff;

				//Read the rootblock.
				disk.read(sourceblockNr, buff);

				//Find the dir.
				k = 1;
				while (dirblock[k].file_name != sourceDir->file_name && k < BLOCK_SIZE / sizeof(dir_entry))
					k++;

				//Make the dir_entry 0.
				dirblock[k] = dir_entry(); //?????

				//Only update size if it was not in the rootblock.
				if (sourceblockNr != ROOT_BLOCK)
				{
					// if (updateSize(-sourceDir->size, shortsource) == -1)
					// {
					// 	std::cerr << "Error! Could not update the sizes." << std::endl;
					// 	return -1;
					// }
				}

				//Write it back to disk after removing the old dir_entry.
				disk.write(sourceblockNr, (uint8_t*)buff);

				return 0;
			}
		}
		//It is a file, overwrite it. UPDATE FAT TABLE.
		else
		{
			//Save the oldname of the source, to be able to find the dir later.
			std::string oldName = sourceDir->file_name;

			//Get the shortdest path (remove the last filename from the path.)
			std::string shortdest = destpath;
			//Remove the last dir/file.
			while (shortdest.back() != '/')
			{
				shortdest.pop_back();
			}
			//Remove the slash if it is not root.
			if (shortdest.size() != 1)
				shortdest.pop_back();

			//Check so that the "smaller" destination exists and that it is a folder. We can not move something into a file.
			dir_entry* destFolder = get_entry(shortdest);
			if (destFolder->type == TYPE_FILE || !destFolder)
			{
				std::cerr << "Error! The destination path does not exist!" << std::endl;
				return -1;
			}

			//First we update the FAT table to remove the destination file that we are to "overwrite".
			//Update fat table.
			int save = 0;
			for (int i = fat[destDir->first_blk];; i = save)
			{
				if (i == EOF)
				{
					fat[save] = EOF;
					break;
				}
				save = fat[i];
				fat[i] = FAT_FREE;
			}
			//Remove first fat entry??? in fat[replaced_dir->first_blk]
			fat[destDir->first_blk] = FAT_FREE;

			//Add the dir to the destination.
			//Create a buffer
			memset(buff, 0, 4096);
			dirblock = (dir_entry*)buff;

			uint16_t destblockNr = destFolder->first_blk;

			//Read the block.
			disk.read(destblockNr, buff);

			//Find the dir that is too be replaced.
			size_t k = 1;
			while (dirblock[k].file_name != destDir->file_name && k < BLOCK_SIZE / sizeof(dir_entry))
				k++;

			//Replace the info in that directory with the source dir information, also rename the file.
			memset(sourceDir->file_name, '\0', 56);
			strncpy(sourceDir->file_name, destDir->file_name, 56);
			dirblock[k] = *sourceDir;

			//Only update the sizes if its not in rootblock.
			if (destblockNr != ROOT_BLOCK)
			{
				//Update size by first subtracting the replaced dir.
				// if (updateSize(-destDir->size, destpath) == -1)
				// {
				// 	std::cerr << "Error! Could not update the sizes." << std::endl;
				// 	return -1;
				// }

				//Then we update the size again to account for the newly added sourcefile in the destination.
				// if (updateSize(sourceDir->size, destpath) == -1)
				// {
				// 	std::cerr << "Error! Could not update the sizes." << std::endl;
				// 	return -1;
				// }
			}

			//Write it back to disk.
			disk.write(destblockNr, (uint8_t*)buff);

			//We now also need to remove the dir_entry from the source.
			//Get the dir of the folder the sourcefile is in.
			std::string shortsource = sourcepath;
			//Remove the filename.
			while (shortsource.back() != '/')
			{
				shortsource.pop_back();
			}

			//If the sourcefile is in root.
			uint16_t sourceblockNr;
			if (shortsource.size() == 1)
			{
				sourceblockNr = ROOT_BLOCK;
			}
			//sourcefile is not in root. Find the dir of the folder and what block the dir is pointing to.
			else
			{
				//Remove the slash at the front.
				shortsource.pop_back();
				sourceblockNr = get_entry(shortsource)->first_blk;
			}

			//Create a buffer
			memset(buff, 0, 4096);
			dirblock = (dir_entry*)buff;

			//Read the block.
			disk.read(sourceblockNr, buff);

			//Find the dir.
			k = 1;
			while (dirblock[k].file_name != oldName && k < BLOCK_SIZE / sizeof(dir_entry))
				k++;

			//Make the dir_entry 0.
			dirblock[k] = dir_entry(); //?????

			//Only update size if it was not in the rootblock.
			if (sourceblockNr != ROOT_BLOCK)
			{
				// if (updateSize(-sourceDir->size, shortsource) == -1)
				// {
				// 	std::cerr << "Error! Could not update the sizes." << std::endl;
				// 	return -1;
				// }
			}

			//Write it back to disk after removing the old dir_entry.
			disk.write(sourceblockNr, (uint8_t*)buff);

			return 0;
		}
	}
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
	std::cout << "FS::rm(" << filepath << ")\n";

	uint8_t block[BLOCK_SIZE] = { 0 };
	std::string temppath = "";

	if (filepath[0] != '/') //Check if path is absolute.
		filepath = path + filepath;

	//Remove last folder/file from path.
	temppath = filepath.substr(filepath.find_last_of('/') + 1, filepath.length() - 1);
	filepath.erase(filepath.find_last_of('/') + 1, filepath.length() - 1);

	if (filepath.size() > 1)
		filepath.pop_back();

	dir_entry* currentDir = get_entry(filepath);
	disk.read(currentDir->first_blk, block);

	if (currentDir->file_name[0] == '\0')
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

	//Update fat table.
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
	}
	//Remove first fat entry??? in fat[entry->first_blk]
	fat[entry->first_blk] = FAT_FREE;

	entry->access_rights = 0u;
	for (size_t i = 0; i < 56; i++)
		entry->file_name[i] = '\0';
	entry->first_blk = 0u;
	//uint32_t tempSize = entry->size; //Save the size for the update function.
	entry->size = 0u;
	entry->type = TYPE_FILE;
	disk.write(currentDir->first_blk, block);

	// if (updateSize(-tempSize, filepath) == -1)
	// {
	// 	std::cerr << "Error! Could not update the sizes." << std::endl;
	// 	return -1;
	// }

	return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
	std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

	dir_entry* entry1 = get_entry(filepath1);
	dir_entry* entry2 = get_entry(filepath2);

	if (!entry1 or !entry2)
	{
		std::cerr << "Path not valid." << std::endl;
		return -1;
	}

	if (!(entry2->access_rights & WRITE))
	{
		std::cerr << "Error! You do not have access rights to write to that file. Therefore you can not copy it." << std::endl;
		return -1;
	}

	uint8_t file1[BLOCK_SIZE];
	size_t binlastblock2 = entry2->size % BLOCK_SIZE;

	size_t lastblockfree = BLOCK_SIZE - binlastblock2 - 1;
	float diff = std::ceil(((float)entry1->size - (float)lastblockfree) / (float)BLOCK_SIZE);
	size_t newblocksneeded = diff < 0 ? 0 : diff;
	uint8_t file2[BLOCK_SIZE];
	int lastfatfile2 = entry2->first_blk;
	while (fat[lastfatfile2] != EOF) //Retrive the last block of a file.
		lastfatfile2 = fat[lastfatfile2];
	int originallastblock = lastfatfile2;
	std::vector<int> zero;
	zero.push_back(0);
	auto empty = (newblocksneeded > 0) ? find_multiple_empty(newblocksneeded) : zero; //Find appropriate amount of needed empty blocks

	if (newblocksneeded == 0)
	{
		disk.read(lastfatfile2, file2);
		disk.read(entry1->first_blk, file1);
		uint8_t* it1 = file1;
		uint8_t* it2 = file2 + binlastblock2;						  //End of file2.
		for (size_t i = 0; i <= entry1->size; i++, it2++, it1++) //Copy part of first block of file1 to lst block of file2.
			*it2 = *it1;
		disk.write(lastfatfile2, file2);

		//Find the entry update its size and write to disk.
		std::string parentpath = path + filepath2;
		parentpath = parentpath.substr(0u, parentpath.find_last_of('/') + 1);
		auto parententry = get_entry(parentpath);
		disk.read(parententry->first_blk, file1);
		for (dir_entry* i = (dir_entry*)file1;; i++)
		{
			if (!std::string(entry2->file_name).compare(i->file_name))
			{
				i->size += entry1->size;
				break;
			}
		}

		disk.write(parententry->first_blk, file1);

		// if (updateSize(entry1->size, filepath2) == -1) //Update the sizes after the move.
		// {
		// 	std::cerr << "Error! Could not update the sizes." << std::endl;
		// 	return -1;
		// }
		return 0;
	}
	else
	{
		uint8_t* it1 = file1;					  // Copy block pointer (start)
		uint8_t* it2 = file2 + binlastblock2; // File Endpoint
		size_t file1blocktoread = entry1->first_blk;
		size_t byteswandered = 0, i = 0;
		size_t bytestocopy = lastblockfree;
		disk.read(lastfatfile2, file2);
		disk.read(entry1->first_blk, file1);
		while (byteswandered < entry1->size)
		{
			if (it1 == file1 + BLOCK_SIZE)
			{
				file1blocktoread = fat[file1blocktoread];
				disk.read(file1blocktoread, file1);
				it1 = file1;
			}
			if (file1blocktoread == (size_t)EOF)
				bytestocopy = entry1->size - byteswandered;
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

	// if (updateSize(entry1->size, filepath2) == -1) //Update the sizes after the append.
	// {
	// 	std::cerr << "Error! Could not update the sizes." << std::endl;
	// 	return -1;
	// }
	return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
	std::cout << "FS::mkdir(" << dirpath << ")\n";

	dir_entry* currentDir = get_entry(dirpath);
	if (currentDir)
	{
		std::cerr << "Error! That name already exists!" << std::endl;
		return -1;
	}

	auto pathvec = split_path(dirpath);
	auto cpathvec = split_path(path);
	if (pathvec[0] != "/")
		pathvec.insert(pathvec.begin(), cpathvec.begin(), cpathvec.end());

	//Get the name of the new dir.
	std::string temppath = pathvec.back();
	currentDir = get_entry(path);
	//New directory
	dir_entry newDir;
	temppath.copy(newDir.file_name, 56);
	int empty = find_empty();
	newDir.first_blk = empty;
	newDir.size = BLOCK_SIZE;
	newDir.type = TYPE_DIR;
	newDir.access_rights = READ | WRITE | EXECUTE;

	//Return directory
	dir_entry returnDir;
	returnDir.file_name[0] = '.';
	returnDir.file_name[1] = '.';
	returnDir.first_blk = currentDir->first_blk; // kanske fel.
	returnDir.size = 0;
	returnDir.type = TYPE_DIR;
	returnDir.access_rights = READ | WRITE | EXECUTE;

	//Read current block.
	uint8_t buff[BLOCK_SIZE] = { 0 };
	dir_entry* currentblock = (dir_entry*)(buff);
	disk.read(currentDir->first_blk, (uint8_t*)currentblock);

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
	disk.write(currentDir->first_blk, (uint8_t*)currentblock);

	//Read new block, that is for the new directory entry.
	uint8_t buff2[BLOCK_SIZE] = { 0 };
	dir_entry* newblock = (dir_entry*)buff2;
	newblock[0] = returnDir;

	//Write the return dir.
	disk.write(empty, (uint8_t*)newblock);

	//Update fat.
	fat[empty] = FAT_EOF;
	disk.write(FAT_BLOCK, (uint8_t*)fat);

	return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
	///TODO: Rerwrite this whole finction since we do not have a folder named root anymore. And there is a simpler way of doing this.
	std::cout << "FS::cd(" << dirpath << ")\n";

	if (!get_entry(dirpath)) //Checks if path is valid.
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
	std::cout << "FS::pwd()\n";
	std::cout << path << std::endl;
	return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
	std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
	dir_entry* valid = get_entry(filepath);
	if (!valid)
		return 1;

	uint8_t block[BLOCK_SIZE];

	if (filepath[0] != '/')
		filepath = path + filepath;

	//Remove last folder/file from filepath.
	size_t lastslash = filepath.find_last_of("/");
	filepath.erase(lastslash + 1, filepath.size() - lastslash);

	//Retrive the dir etntry of the dir/file to be changed.
	dir_entry* dirtoload = get_entry(filepath);

	disk.read(dirtoload->first_blk, block);

	//Look for valid in block.
	dir_entry* it;
	for (it = (dir_entry*)block; std::strcmp(it->file_name, valid->file_name); it++);

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

	disk.write(dirtoload->first_blk, block);

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
	auto check = get_entry(updateFrom);
	if (check->type == TYPE_FILE)
	{
		dir_entry* file = get_entry(updateFrom);
		file->size += size;

		while (updateFrom.back() != '/' && updateFrom != "")
			updateFrom.pop_back();

		if (updateFrom.back() == '/' && updateFrom.size() > 1)
			updateFrom.pop_back();

		dir_entry* homeFolder = get_entry(updateFrom);
		uint8_t block[BLOCK_SIZE] = { 0 };
		disk.read(homeFolder->first_blk, block);
		dir_entry* entry = (dir_entry*)block;
		int i = 0;
		while (std::strcmp(file->file_name, entry->file_name) && i < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
		{
			i++;
			entry++;
		}
		*entry = *file;

		disk.write(homeFolder->first_blk, block);
	}

	//if it is a relative path, make it an absolute path.
	if (updateFrom[0] != '/')
		updateFrom = path + updateFrom;

	dir_entry* currentEntry;
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
			currentEntry = get_entry(updateFrom);
			currentEntry->size += size;

			//Read this folder's block
			dir_entry* dirblock = (dir_entry*)block;
			disk.read(currentEntry->first_blk, (uint8_t*)dirblock);

			//Save the index of the block where the directory lies.
			int block_number = dirblock->first_blk;
			//Then read the block where the directory was.
			disk.read(dirblock->first_blk, (uint8_t*)dirblock);

			//Find the spot where the dir is.
			int j = 0;
			while (std::strcmp(currentEntry->file_name, dirblock->file_name) && j < std::floor(BLOCK_SIZE / sizeof(dir_entry)))
			{
				j++;
				dirblock++;
			}
			//Replace it with the entry that has an updated size.
			*dirblock = *currentEntry;

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
	currentEntry = get_entry(updateFrom);
	currentEntry->size += size;

	//Read the root block.
	dir_entry* dirblock = (dir_entry*)block;
	disk.read(ROOT_BLOCK, block);

	//Put back the updated root directory.
	dirblock[0] = *currentEntry;

	disk.write(ROOT_BLOCK, block);
	return 0;
}

//Retrives the entry of a given filelpath. If the entry is not found, nullptr is returned.
dir_entry* FS::get_entry(std::string filepath)
{
	auto pathvec = split_path(filepath);
	auto absvec = split_path(path);
	if (pathvec[0] != "/")
		pathvec.insert(pathvec.begin(), absvec.begin(), absvec.end());

	//Search from root directory.
	uint8_t block[BLOCK_SIZE];
	disk.read(ROOT_BLOCK, block);
	dir_entry* it = (dir_entry*)block;
	for (auto&& s : pathvec)
	{
		for (; it->file_name[0] != '\0'; it++)
		{
			if (s == "/" && pathvec.size() != 1)
			{
				++it;
				break;
			}
			if (!s.compare(it->file_name))
			{
				if (!s.compare(pathvec.back()))
				{
					dir_entry* result = (dir_entry*)malloc(sizeof(dir_entry));
					*result = *it;
					return result;
				}
				else
				{
					disk.read(it->first_blk, block);
					it = (dir_entry*)block;
					break;
				}
			}
		}
	}
	return nullptr;
}

/*
Takes a path and returns a vector of strings where every string
represents a directory or a file. The strings are stored in the vector
with the same order as the the filepath.
*/
std::vector<std::string> FS::split_path(std::string filepath)
{
	std::vector<std::string> results;
	size_t cut;
	if (filepath.front() == '/')
		results.push_back("/");
	while ((cut = filepath.find_first_of('/')) != filepath.npos)
	{
		if (cut > 0)
			results.push_back(filepath.substr(0, cut));
		filepath = filepath.substr(cut + 1);
	}
	if (filepath.length() > 0)
		results.push_back(filepath);
	return results;
}