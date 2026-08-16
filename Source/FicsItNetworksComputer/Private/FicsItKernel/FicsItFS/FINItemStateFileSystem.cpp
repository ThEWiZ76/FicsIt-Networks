#include "FicsItKernel/FicsItFS/FINItemStateFileSystem.h"

#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "FicsItNetworksComputer.h"
#include "FINComputerSubsystem.h"
#include "FicsItKernel/FicsItFS/FINFileSystemSubsystem.h"
#include "FileSystemSerializationInfo.h"
#include "Logging/StructuredLog.h"

FIN_STRUCT_IMPLEMENT_INTERFACE(FFINItemStateFileSystem, FFINLabelContainerInterface)

int AskForDiskOrSave(FString Name) {
	if (FParse::Param(FCommandLine::Get(), TEXT("FINOverwriteFS"))) {
		return FIN_FS_AlwaysOverride;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("FINKeepFS"))) {
		return FIN_FS_AlwaysKeep;
	}
#if UE_SERVER
	UE_LOGFMT(LogFicsItNetworksComputer, Fatal,
		"A FileSystem Content difference between save file and host has been found for UUID: {uuid}! \n"
		"Please add the '-FINOverwriteFS' or '-FINKeepFS' command line options to define what should happen in such case.",
		Name);
#endif

	switch (AFINComputerSubsystem::GetFSAlways()) {
		case FIN_FS_AlwaysOverride:
			return FIFS_OVERRIDE_CHANGES;
		case FIN_FS_AlwaysKeep:
			return FIFS_KEEP_CHANGES;
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
			return FIFS_KEEP_CHANGES;
		case EAppReturnType::YesAll:
			AFINComputerSubsystem::SetFSAlways(FIN_FS_AlwaysKeep);
			return FIFS_KEEP_CHANGES;
		case EAppReturnType::NoAll:
			AFINComputerSubsystem::SetFSAlways(FIN_FS_AlwaysOverride);
			return FIFS_OVERRIDE_CHANGES;
		case EAppReturnType::No:
		default:
			return FIFS_OVERRIDE_CHANGES;
	}
}

bool FFINItemStateFileSystem::Serialize(FStructuredArchive::FSlot Slot) {
	StaticStruct()->SerializeBin(Slot, this);

	if (!Slot.GetUnderlyingArchive().IsSaveGame()) return true;

	int32 OldCapacity = Capacity;
	Capacity = 0;
	TSharedPtr<CodersFileSystem::Device> SerializeDevice = AFINFileSystemSubsystem::GetDevice(ID, false, true);
	Capacity = OldCapacity;

	auto Record = Slot.EnterRecord();
	FStructuredArchive::FSlot RootNode = Record.EnterField(TEXT("RootNode"));

	int KeepDisk = -1;
	CodersFileSystem::SerializePath(SerializeDevice.ToSharedRef(), RootNode.EnterRecord(), "/", ID.ToString(), KeepDisk, &AskForDiskOrSave);

	return true;
}
