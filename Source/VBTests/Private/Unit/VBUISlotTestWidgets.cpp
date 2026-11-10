// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBUISlotTestWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "UI/Slots/VBUISlotSubsystem.h"

EVBUISlotTestAction UVBUISlotTestEntry::PendingAction = EVBUISlotTestAction::None;
TWeakObjectPtr<UVBUISlotSubsystem> UVBUISlotTestEntry::PendingRegistry;
FGameplayTag UVBUISlotTestEntry::PendingSlot;
int32 UVBUISlotTestEntry::PendingId = INDEX_NONE;

void UVBUISlotTestEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 원하는 크기를 가진 트리 - 자리의 원하는 크기 = 항목의 원하는 크기를 잴 수 있게.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Box"));
		Box->SetWidthOverride(200.0f);
		Box->SetHeightOverride(50.0f);
		WidgetTree->RootWidget = Box;
	}
}

void UVBUISlotTestEntry::NativeConstruct()
{
	Super::NativeConstruct();
	++Constructs;
	const EVBUISlotTestAction Action = PendingAction;
	PendingAction = EVBUISlotTestAction::None;
	UVBUISlotSubsystem* Registry = PendingRegistry.Get();
	if (!Registry)
	{
		return;
	}

	if (Action == EVBUISlotTestAction::RegisterOther)
	{
		FVBUISlotRequest Request;
		Request.Slot = PendingSlot;
		Request.WidgetClass = UVBUISlotTestOther::StaticClass();
		Request.Order = 1;
		Registry->RegisterWidget(Request);
	}
	else if (Action == EVBUISlotTestAction::UnregisterId)
	{
		Registry->Unregister(PendingId);
	}
}

void UVBUISlotTestEntry::NativeDestruct()
{
	++Destructs;
	Super::NativeDestruct();
}
