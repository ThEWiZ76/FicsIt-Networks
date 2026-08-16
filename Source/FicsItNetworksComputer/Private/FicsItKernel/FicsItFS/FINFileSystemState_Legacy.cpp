#include "FicsItKernel/FicsItFS/FINFileSystemState_Legacy.h"

#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "FGInventoryComponent.h"
#include "FINComputerSubsystem.h"
#include "FileSystemSerializationInfo.h"
#include "Net/UnrealNetwork.h"

#define KEEP_CHANGES 1
#define OVERRIDE_CHANGES 0

AFINFileSystemState_Legacy::AFINFileSystemState_Legacy() {
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

int AskForDiskOrSave_Legacy(FString Name) {
	switch (AFINComputerSubsystem::GetFSAlways()) {
	case FIN_FS_AlwaysOverride:
		return OVERRIDE_CHANGES;
	case FIN_FS_AlwaysKeep:
		return KEEP_CHANGES;
	default: ;
	}
	
	const FString Caption = FString::Printf(TEXT("FileSystem Content difference in FS '%s' found!"), *Name);
	const FString Message = FString::Printf(TEXT("The disk filesystem of a drive or floppy differs from the content in the save file.\n\n"
		"Choose Yes to keep the content on disk, or No to overwrite it with the content from the save.\n"
		"Yes to All and No to All apply the same choice to all remaining file systems.\n\n"
		"FileSystem UUID: '%s'"), *Name);
	const EAppReturnType::Type Result = FPlatformMisc::MessageBoxExt(EAppMsgType::YesNoYesAllNoAll, *Message, *Caption);

	switch (Result) {
		case EAppReturnType::Yes:
			return KEEP_CHANGES;
		case EAppReturnType::YesAll:
			AFINComputerSubsystem::SetFSAlways(FIN_FS_AlwaysKeep);
			return KEEP_CHANGES;
		case EAppReturnType::NoAll:
			AFINComputerSubsystem::SetFSAlways(FIN_FS_AlwaysOverride);
			return OVERRIDE_CHANGES;
		case EAppReturnType::No:
		default:
			return OVERRIDE_CHANGES;
	}
}

#define CheckKeepDisk(Condition) \
	if (bIsLoading && KeepDisk == -1) { \
		if (Condition) KeepDisk = AskForDiskOrSave_Legacy(Name); \
		if (KeepDisk == 1) return; \
	}

void AFINFileSystemState_Legacy::SerializePath(TSharedRef<CodersFileSystem::Device> SerializeDevice, FStructuredArchive::FRecord Record, CodersFileSystem::Path Path, FString Name, int& KeepDisk) {
	std::unordered_set<std::string> childs;
	std::unordered_set<std::string>::iterator childIterator;
	int ChildNodeNum;
	bool bIsSaving = Record.GetUnderlyingArchive().IsSaving();
	bool bIsLoading = Record.GetUnderlyingArchive().IsLoading();

	if (bIsSaving) {
		childs = SerializeDevice->children(Path);
		childIterator = childs.begin();
		ChildNodeNum = childs.size();
	}
	FStructuredArchive::FArray ChildNodes = Record.EnterArray(TEXT("ChildNodes"), ChildNodeNum);
	std::unordered_set<std::string> DiskChilds = SerializeDevice->children(Path);
	CheckKeepDisk(DiskChilds.size() != ChildNodeNum);
	if (KeepDisk == 0) {
		for (std::string DiskChild : DiskChilds) {
			SerializeDevice->remove(Path / DiskChild, true);
		}
		DiskChilds.clear();
	}

	for (int i = 0; i < ChildNodeNum; ++i) {
		FStructuredArchive::FRecord Child = ChildNodes.EnterElement().EnterRecord();
		FString UChildName;
		if (Record.GetUnderlyingArchive().IsSaving()) {
			UChildName = UTF8_TO_TCHAR((childIterator++)->c_str());
		}
		Child.EnterField(TEXT("Name")) << UChildName;
		std::string stdChildName = TCHAR_TO_UTF8(*UChildName);
		
		CheckKeepDisk(DiskChilds.find(stdChildName) == DiskChilds.end())
		if (DiskChilds.size() > 0 && KeepDisk == 0) {
			for (std::string DiskChild : DiskChilds) {
				SerializeDevice->remove(Path / DiskChild, true);
			}
			DiskChilds.clear();
		} else if (KeepDisk == -1) {
			DiskChilds.erase(stdChildName);
		}

		int Type = 0;
		if (bIsSaving) {
			TOptional<CodersFileSystem::FileType> ChildType = SerializeDevice->fileType(Path / stdChildName);
			if (ChildType.IsSet() && *ChildType == CodersFileSystem::File_Regular) Type = 1;
			if (ChildType.IsSet() && *ChildType == CodersFileSystem::File_Directory) Type = 2;
		}
		Child.EnterField(TEXT("Type")) << Type;
		TOptional<CodersFileSystem::FileType> existingType = SerializeDevice->fileType(Path / stdChildName);
		if (Type == 1) {
			CheckKeepDisk(!existingType.IsSet() || *existingType != CodersFileSystem::File_Regular)
			if (KeepDisk == 0) {
				SerializeDevice->remove(Path / stdChildName, true);
			}
			FStructuredArchive::FSlot Content = Child.EnterField(TEXT("FileContent"));
			if (Record.GetUnderlyingArchive().IsLoading()) {
				std::string diskData;
				if (KeepDisk == -1) diskData = CodersFileSystem::FileStream::readAll(SerializeDevice->open(Path / stdChildName, CodersFileSystem::INPUT | CodersFileSystem::BINARY).ToSharedRef());
				std::string stdData;
				if (bUsePreBinarySupportSerialization) {
					FString Data;
					Content << Data;
					FTCHARToUTF8 Conv(Data);
					stdData = std::string(Conv.Get(), Conv.Length());
				} else {
					TArray<uint8> Data;
					Content << Data;
					stdData = std::string((char*)Data.GetData(), Data.Num());
				}
				CheckKeepDisk(diskData != stdData)
				if (KeepDisk == 0) {
					TSharedPtr<CodersFileSystem::FileStream> Stream = SerializeDevice->open(Path / stdChildName, CodersFileSystem::OUTPUT | CodersFileSystem::TRUNC | CodersFileSystem::BINARY);
					Stream->write(stdData);
					Stream->close();
				}
            } else if (Record.GetUnderlyingArchive().IsSaving()) {
            	TSharedPtr<CodersFileSystem::FileStream> Stream = SerializeDevice->open(Path / stdChildName, CodersFileSystem::INPUT | CodersFileSystem::BINARY);
            	std::string RawData = CodersFileSystem::FileStream::readAll(Stream.ToSharedRef());
            	if (bUsePreBinarySupportSerialization) {
            		FUTF8ToTCHAR Conv(RawData.c_str(), RawData.length());
            		FString Data(Conv.Length(), Conv.Get());
            		Content << Data;
            	} else {
            		Stream->close();
            		TArray<uint8> Data((uint8*)RawData.c_str(), RawData.length());
            		Content << Data;
            	}
            }
		} else if (Type == 2) {
			CheckKeepDisk(!existingType.IsSet() || *existingType != CodersFileSystem::File_Directory)
			if (KeepDisk == 0) {
				SerializeDevice->remove(Path / stdChildName, true);
			}
			SerializeDevice->createDir(Path / stdChildName);
			SerializePath(SerializeDevice, Child, Path / stdChildName, Name, KeepDisk);
			if (KeepDisk == 1) return;
		}
	}
}

void AFINFileSystemState_Legacy::Serialize(FStructuredArchive::FRecord Record) {
	Super::Serialize(Record);

	if (!Record.GetUnderlyingArchive().IsSaveGame()) return;

	int32 OldCapacity = Capacity;
	Capacity = 0;
	TSharedPtr<CodersFileSystem::Device> SerializeDevice = GetDevice(false, true);
	Capacity = OldCapacity;

	if (Record.GetUnderlyingArchive().IsLoading()) {
		//SerializeDevice->remove("/", true);
	}
	
	FStructuredArchive::FSlot RootNode = Record.EnterField(TEXT("RootNode"));
	if (!bUseOldSerialization) {
		int KeepDisk = -1;
		SerializePath(SerializeDevice.ToSharedRef(), RootNode.EnterRecord(), "/", ID.ToString(), KeepDisk);
	} else {
		bUseOldSerialization = false;
	}
}

void AFINFileSystemState_Legacy::PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) {
	bUseOldSerialization = gameVersion < 150216;
	//bUsePreBinarySupportSerialization = gameVersion < 264901;
}

bool AFINFileSystemState_Legacy::ShouldSave_Implementation() const {
	return true;
}

TSharedPtr<CodersFileSystem::Device> AFINFileSystemState_Legacy::GetDevice(bool bInForceUpdate, bool bInForceCreate) {
	if (!HasAuthority()) return nullptr;
	
	if (!IdCreated) {
		IdCreated = true;
		ID = FGuid::NewGuid();
	}

	TSharedPtr<CodersFileSystem::Device> NewDevice = Device;
	if (!Device.IsValid() || bInForceCreate || bInForceUpdate) {
		FString fsp;
		// TODO: Get UFGSaveSystem::GetSaveDirectoryPath() working
		if(fsp.IsEmpty()) {
			fsp = FPaths::Combine( FPlatformProcess::UserSettingsDir(), FApp::GetProjectName(), TEXT( "Saved/" ) TEXT( "SaveGames/" ) );
		}

		// get root fs path
		std::filesystem::path root = std::filesystem::absolute(*fsp);
		root = root / std::filesystem::path(std::string("Computers")) / std::filesystem::path(TCHAR_TO_UTF8(*ID.ToString()));

		std::filesystem::create_directories(root);

		NewDevice = MakeShared<CodersFileSystem::DiskDevice>(root, Capacity);

		if (!Device.IsValid() || bInForceUpdate) Device = NewDevice;
	}
	return NewDevice;
}
