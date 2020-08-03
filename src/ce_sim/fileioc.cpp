
#include "platform.h"
#include "debug.h"
#include "ce_sim.h"
#include "fileioc.h"

#include "fxcg/file.h"

// TI file format borrowed from https://github.com/calc84maniac/tiboyce/blob/master/tiboyce-romgen/romgen.c
enum VAR_TYPE {
	TYPE_APPVAR = 0x15
};

enum VAR_FLAG {
	VAR_UNARCHIVED = 0x00,
	VAR_ARCHIVED = 0x80
};

#pragma pack(push, 1)
struct tivar {
	uint16_t header_length;
	uint16_t data_length;
	uint8_t type;
	uint8_t name[8];
	uint8_t version;
	uint8_t flag;
	uint16_t data_length_2;
	uint16_t var_length;
};

struct tifile {
	uint8_t signature[11];
	uint8_t comment[42];
	uint16_t file_length;
	struct tivar data;

	tifile() {
		memset(this, '\0', sizeof(tifile));
		strcpy((char*) signature, "**TI83F*\x1A\x0A");
		file_length = 0;
		data.type = TI_APPVAR_TYPE;
	}

	void EndianSwap() {
		EndianSwap_Little(file_length);
		EndianSwap_Little(data.header_length);
		EndianSwap_Little(data.data_length);
		EndianSwap_Little(data.data_length_2);
		EndianSwap_Little(data.var_length);
	}
};
#pragma pack(pop)


struct CEFileSlot {
	tifile file;
	uint8_t* data;
	uint32 pos;

	CEFileSlot() : data(nullptr) {}

	void Close() {
		if (data) {
			//free(data);
			data = nullptr;
		}
	}
};

const int NumSlots = 64;

static bool bAnyOpen = false;
static int curSlot = 0;
static CEFileSlot AllFiles[NumSlots];

static int FindSlot() {
	for (int i = 0; i < NumSlots; i++) {
		if (AllFiles[i].data == nullptr) {
			return i;
		}
	}

	return -1;
}

void ti_CloseAll() {
	if (bAnyOpen) {
		for (int i = 0; i < NumSlots; i++) {
			AllFiles[i].Close();
		}
	}

	bAnyOpen = false;
}

static int OpenVar(const char* name, int mode, bool bHasExtension = false) {
	char fullName[64];
	strcpy(fullName, "\\\\fls0\\");
	strcat(fullName, name);
	if (!bHasExtension) {
		strcat(fullName, ".8xv");
	}

	uint16_t shortName[64];
	Bfile_StrToName_ncpy(shortName, fullName, 64);

	return Bfile_OpenFile_OS(shortName, mode, 0);
}

/**
 * Opens a file
 *
 * An AppVar is used as default file storage
 * @param name Name of file to open
 * @param mode
 * "r"  - Opens a file for reading. The file must exist. Keeps file in archive if in archive.                                   <br>
 * "w"  - Creates an empty file for writing. Overwrites file if already exists.                                                 <br>
 * "a"  - Appends to a file. Writing operations, append data at the end of the file. The file is created if it does not exist.  <br>
 * "r+" - Opens a file to update both reading and writing. The file must exist. Moves file from archive to RAM if in archive.   <br>
 * "w+" - Creates an empty file for both reading and writing. Overwrites file if already exists.                                <br>
 * "a+" - Opens a file for reading and appending. Moves file from archive to RAM if in archive. Created if it does not exist.
 * @returns Slot variable
 * @note If there isn't enough memory to create the variable, or a slot isn't open, zero (0) is returned
 */
ti_var_t ti_Open(const char *name, const char *mode) {
	int slot = FindSlot();
	if (slot == -1)
		return 0;

	if (!strcmp(mode, "r")) {
		int handle = OpenVar(name, READ);
		if (handle >= 0) {
			if (Bfile_ReadFile_OS(handle, &AllFiles[slot].file, sizeof(tifile), -1) != sizeof(tifile)) {
				Bfile_CloseFile_OS(handle);
				return 0;
			}

			const int var_length = AllFiles[slot].file.data.var_length;
			AllFiles[slot].data = (uint8_t*) malloc(var_length);
			if (Bfile_ReadFile_OS(handle, AllFiles[slot].data, var_length, -1) != var_length) {
				Bfile_CloseFile_OS(handle);
				AllFiles[slot].Close();
				return 0;
			}

			AllFiles[slot].pos = 0;
			bAnyOpen = true;
			return slot + 1;
		}

		return 0;
	}

	DebugAssert(0); // unsupported
	return 0;
}

void *ti_GetDataPtr(const ti_var_t slot) {
	return AllFiles[slot - 1].data;
}

int ti_GetC(const ti_var_t slot) {
	CEFileSlot& fileSlot = AllFiles[slot - 1];
	if (fileSlot.pos < fileSlot.file.data.var_length) {
		return fileSlot.data[fileSlot.pos++];
	} else {
		return EOF;
	}
}

int ti_Seek(int offset, unsigned int origin, const ti_var_t slot) {
	CEFileSlot& fileSlot = AllFiles[slot - 1];
	int newPos = offset;
	if (origin == SEEK_CUR) newPos += fileSlot.pos;
	if (origin == SEEK_END) newPos += fileSlot.file.data.var_length;
	if (newPos >= 0 && newPos <= fileSlot.file.data.var_length) {
		fileSlot.pos = newPos;
		return 0;
	}
	return EOF;
}

int ti_Rewind(const ti_var_t slot) {
	CEFileSlot& fileSlot = AllFiles[slot - 1];
	fileSlot.pos = 0;
	return 0;
}

struct foundFile {
	char path[48];
};

typedef struct {
	unsigned short id, type;
	unsigned long fsize, dsize;
	unsigned int property;
	unsigned long address;
} file_type_t;

static void FindFiles(const char* path, foundFile* toArray, int& numFound, int maxAllowed) {
	unsigned short filter[0x100], found[0x100];
	int ret, handle;
	file_type_t info; // See Bfile_FindFirst for the definition of this struct

	Bfile_StrToName_ncpy(filter, path, 0x50); // Overkill

	ret = Bfile_FindFirst((const char*)filter, &handle, (char*)found, &info);

	while (ret == 0 && numFound < maxAllowed) {
		Bfile_NameToStr_ncpy(toArray[numFound++].path, found, 48);
		ret = Bfile_FindNext(handle, (char*)found, (char*)&info);
	};

	Bfile_FindClose(handle);
}

char *ti_Detect(void **curr_search_posistion, const char *detection_string) {
	// todo : detection string
	static foundFile files[64];
	static int lastFound = 0;

	// only search once 
	if (!lastFound || *curr_search_posistion == nullptr) {
		int numFound = 0;
		FindFiles("\\\\fls0\\*.8xv", files, numFound, 64);

		// filter detection string
		int32 numCulled = 0;
		if (detection_string) {
			int detectLength = strlen(detection_string);
			DebugAssert(detectLength < 32);
			for (int32 i = 0; i < numFound; i++) {
				char readFile[32];
				int handle = OpenVar(files[i].path, READ, true);
				DebugAssert(handle != -1);
				
				if (Bfile_ReadFile_OS(handle, readFile, detectLength, sizeof(tifile)) != detectLength ||
					memcmp(readFile, detection_string, detectLength)) {

					files[i].path[0] = 0;
					numCulled++;
				}

				Bfile_CloseFile_OS(handle);
			}
		}

		lastFound = numFound;
		*curr_search_posistion = 0;
	}

	int* curFile = (int*) curr_search_posistion;
	while (lastFound > *curFile) {
		if (files[*curFile].path[0]) {
			char* ret = files[*curFile].path;
			*strrchr(ret, '.') = 0;
			curFile[0]++;
			return ret;
		} else {
			curFile[0]++;
		}
	}

	return nullptr;
}

uint8_t ti_RclVar(const uint8_t var_type, const char *var_name, void **data_struct) {
	// todo (translate Alpha_GetData?)
	return 1;
}