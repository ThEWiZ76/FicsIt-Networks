#include "FINComputerRCO.h"

#include "FINComputerCase.h"
#include "FINComputerEEPROMDesc.h"
#include "FicsItReflection.h"
#include "FIRAnyValue.h"
#include "FicsItKernel/Processor/FINItemStateEEPROMText.h"
#include "FINNetworkComponent.h"
#include "ComputerModules/FINComputerDriveHolder.h"
#include "ComputerModules/PCI/FINComputerGPUT1.h"
#include "ComputerModules/PCI/FINComputerGPUT2.h"
#include "FicsItKernel/Processor/FINStateEEPROM_Legacy.h"
#include "Net/UnrealNetwork.h"
#include "Reflection/FIRClass.h"
#include "Reflection/FIRSignal.h"

namespace {
	void TriggerGPUT2Signal(AFINComputerGPUT2* GPU, const FString& SignalName, const TArray<FFIRAnyValue>& Parameters) {
		if (!IsValid(GPU)) return;
		UFIRClass* ReflectedClass = FFicsItReflectionModule::Get().FindClass(GPU->GetClass());
		if (!ReflectedClass) return;
		UFIRSignal* Signal = ReflectedClass->FindFIRSignal(SignalName);
		if (!Signal) return;
		Signal->Trigger(GPU, Parameters);
	}
}

void UFINComputerRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UFINComputerRCO, bDummy);
}

void UFINComputerRCO::SetCaseLastTab_Implementation(AFINComputerCase* Case, int LastTab) {
	Case->LastTabIndex = LastTab;
	Case->ForceNetUpdate();
}

void UFINComputerRCO::SetDriveHolderLocked_Implementation(AFINComputerDriveHolder* Holder, bool bLocked) {
	Holder->SetLocked(bLocked);
}

void UFINComputerRCO::ToggleCase_Implementation(AFINComputerCase* Case) {
	Case->Toggle();
}

void UFINComputerRCO::SetNick_Implementation(UObject* Component, const FString& Nick) {
	if (IsValid(Component) && Component->Implements<UFINNetworkComponent>()) IFINNetworkComponent::Execute_SetNick(Component, Nick);
}

void UFINComputerRCO::GPUMouseEvent_Implementation(AFINComputerGPUT1* GPU, int type, int x, int y, int btn) {
	switch (type) {
	case 0:
		GPU->netSig_OnMouseDown(x, y, btn);
		break;
	case 1:
		GPU->netSig_OnMouseUp(x, y, btn);
		break;
	case 2:
		GPU->netSig_OnMouseMove(x, y, btn);
		break;
	default: break;
	}
}

void UFINComputerRCO::GPUKeyEvent_Implementation(AFINComputerGPUT1* GPU, int type, int64 c, int64 code, int btn) {
	switch (type) {
	case 0:
		GPU->netSig_OnKeyDown(c, code, btn);
		break;
	case 1:
		GPU->netSig_OnKeyUp(c, code, btn);
		break;
	default: break;
	}
}

void UFINComputerRCO::GPUKeyCharEvent_Implementation(AFINComputerGPUT1* GPU, const FString& c, int btn) {
	GPU->netSig_OnKeyChar(c, btn);
}

void UFINComputerRCO::CreateEEPROMState_Implementation(UFGInventoryComponent* Inv, int SlotIdx) {
	FInventoryStack stack;
	if (!IsValid(Inv) || !Inv->GetStackFromIndex(SlotIdx, stack) || !IsValid(stack.Item.GetItemClass()) || stack.Item.HasState()) return;
	UFINComputerEEPROMDesc* desc = Cast<UFINComputerEEPROMDesc>(stack.Item.GetItemClass()->GetDefaultObject());
	if (!IsValid(desc)) return;
	FFGDynamicStruct state = desc->CreateEEPROMState();
	Inv->SetStateOnIndex(SlotIdx, state);
}

void UFINComputerRCO::CopyDataItem_Implementation(UFGInventoryComponent* InProviderInc, int InProviderIdx, UFGInventoryComponent* InFromInv, int InFromIdx, UFGInventoryComponent* InToInv, int InToIdx) {
	FInventoryStack Provider;
	if (!InProviderInc->GetStackFromIndex(InProviderIdx, Provider)) return;
	FInventoryStack From;
	if (!InFromInv->GetStackFromIndex(InFromIdx, From)) return;
	if (!InToInv->IsIndexEmpty(InToIdx)) return;
	if (!Provider.Item.GetItemClass() || !From.Item.GetItemClass()) return;
	UObject* Descriptor = const_cast<UObject*>(GetDefault<UObject>(Provider.Item.GetItemClass()));
	if (!Descriptor->Implements<UFINCopyableItemInterface>()) return;
	bool bDone = IFINCopyableItemInterface::Execute_CopyData(Descriptor, InProviderInc, Provider.Item, From.Item, From.Item);
	if (bDone) {
		InFromInv->RemoveAllFromIndex(InFromIdx);
		InToInv->AddStackToIndex(InToIdx, From);
	}
}

void UFINComputerRCO::GPUT2MouseEvent_Implementation(AFINComputerGPUT2* GPU, int Type, FVector2D Position, int Modifiers) {
	switch (Type) {
	case 0:
		TriggerGPUT2Signal(GPU, TEXT("OnMouseDown"), {FIRStruct(Position), static_cast<FIRInt>(Modifiers)});
		break;
	case 1:
		TriggerGPUT2Signal(GPU, TEXT("OnMouseUp"), {FIRStruct(Position), static_cast<FIRInt>(Modifiers)});
		break;
	case 2:
		TriggerGPUT2Signal(GPU, TEXT("OnMouseMove"), {FIRStruct(Position), static_cast<FIRInt>(Modifiers)});
		break;
	case 3:
		TriggerGPUT2Signal(GPU, TEXT("OnMouseEnter"), {FIRStruct(Position), static_cast<FIRInt>(Modifiers)});
		break;
	case 4:
		TriggerGPUT2Signal(GPU, TEXT("OnMouseLeave"), {FIRStruct(Position), static_cast<FIRInt>(Modifiers)});
		break;
	default: break;
	}
}

void UFINComputerRCO::GPUT2KeyEvent_Implementation(AFINComputerGPUT2* GPU, int Type, int64 C, int64 Code, int Modifiers) {
	switch (Type) {
	case 0:
		TriggerGPUT2Signal(GPU, TEXT("OnKeyDown"), {static_cast<FIRInt>(C), static_cast<FIRInt>(Code), static_cast<FIRInt>(Modifiers)});
		break;
	case 1:
		TriggerGPUT2Signal(GPU, TEXT("OnKeyUp"), {static_cast<FIRInt>(C), static_cast<FIRInt>(Code), static_cast<FIRInt>(Modifiers)});
		break;
	default: break;
	}
}

void UFINComputerRCO::GPUT2KeyCharEvent_Implementation(AFINComputerGPUT2* GPU, const FString& C, int Modifiers) {
	TriggerGPUT2Signal(GPU, TEXT("OnKeyChar"), {C, static_cast<FIRInt>(Modifiers)});
}

void UFINComputerRCO::GPUT2MouseWheelEvent_Implementation(AFINComputerGPUT2* GPU, FVector2D Position, float Delta, int Modifiers) {
	TriggerGPUT2Signal(GPU, TEXT("OnMouseWheel"), {FIRStruct(Position), static_cast<FIRFloat>(Delta), static_cast<FIRInt>(Modifiers)});
}

void UFINComputerRCO::SetLabel_Implementation(UFGInventoryComponent* Inventory, int32 Index, const FString& Label) {
	FInventoryStack stack;
	Inventory->GetStackFromIndex(Index, stack);
	FFGDynamicStruct state = stack.Item.GetItemState();
	if (auto labelContainer = FFINStructInterfaces::Get().GetInterface<FFINLabelContainerInterface>(state)) {
		labelContainer->SetLabel(Label);
		Inventory->SetStateOnIndex(Index, state);
	}
}

void UFINComputerRCO::SetTextEEPROMCode_Implementation(UFGInventoryComponent* Inventory, int32 Index, const FString& NewCode) {
	FInventoryStack stack;
	if (!Inventory->GetStackFromIndex(Index, stack)) return;
	UFINComputerEEPROMDesc::CreateEEPROMStateInItem(stack.Item);
	if (const FFINItemStateEEPROMText* luaState = stack.Item.GetItemState().GetValuePtr<FFINItemStateEEPROMText>()) {
		FFINItemStateEEPROMText state = *luaState;
		state.Code = NewCode;
		Inventory->SetStateOnIndex(Index, FFGDynamicStruct(state));
		Inventory->OnSlotUpdatedDelegate.Broadcast(Index);
	}
}

void UFINComputerRCO::Multicast_ItemStateUpdated_Implementation(class UFGInventoryComponent* Inventory, int32 Index, FFIRInstancedStruct state) {
	Inventory->OnSlotUpdatedDelegate.Broadcast(Index);
}

void UFINComputerRCO::GPUUpdateScreenSize_Implementation(AFINComputerGPU* GPU, FVector2D Size) {
	FScopeLock Lock(&GPU->MutexScreenSize);
	GPU->bScreenSizeUpdated = true;
	GPU->LastScreenSize = Size;
}
