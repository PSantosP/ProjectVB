// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "VBUISlotTestWidgets.generated.h"

class UVBUISlotSubsystem;

// UI 슬롯 시험용 항목 위젯. 구성, 해체를 세고, 구성 때 한 번 등록소를 건드릴 수 있다.
UENUM()
enum class EVBUISlotTestAction : uint8
{
	None,
	RegisterOther, // 같은 슬롯에 UVBUISlotTestOther 를 등록한다
	UnregisterId, // 정해 둔 항목 Id 를 푼다(자기 자신)
};

UCLASS(NotBlueprintable)
class UVBUISlotTestEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	int32 Constructs = 0;
	int32 Destructs = 0;

	// 다음에 구성되는 항목 하나가 한 번 할 일(시험이 정한다). 쓰고 나면 None 으로 돌아간다.
	static EVBUISlotTestAction PendingAction;
	static TWeakObjectPtr<UVBUISlotSubsystem> PendingRegistry;
	static FGameplayTag PendingSlot;
	static int32 PendingId;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
};

// 재진입 등록이 만드는 두 번째 항목 - 고정 크기(원하는 크기 시험에도 쓴다).
UCLASS(NotBlueprintable)
class UVBUISlotTestOther : public UVBUISlotTestEntry
{
	GENERATED_BODY()
};
