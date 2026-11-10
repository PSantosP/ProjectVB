// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "VBUISlotWidget.generated.h"

class UCanvasPanel;
class UVBUISlotSubsystem;
struct FVBUISlotEntry;

/**
 * UI 슬롯 자리 하나. 자기 태그로 UVBUISlotSubsystem 에 붙어 그 태그의 항목 위젯을 만들어 겹친다.
 * 트리는 C++ 로 짓는다(루트 CanvasPanel). 순서는 ZOrder 로만 바꾼다 - InsertChildAt 은 살아 있는 Slate 를 안 바꾼다.
 */
UCLASS()
class VB_API UVBUISlotWidget : public UUserWidget
{
	GENERATED_BODY()

	friend class FVBUISlotTestAccess;

public:
	// 구성 전에 한 번 정한다. 소유자 = 이 슬롯을 만든 쪽.
	void SetSlot(const FGameplayTag& InSlotTag, const UObject* InOwner);
	const FGameplayTag& GetSlotTag() const { return SlotTag; }
	const UObject* GetSlotOwner() const { return SlotOwner.Get(); }

	// 항목을 놓은 슬롯의 소유자. 슬롯 밖이면 null. 항목 Outer 는 PC 라 부모 패널의 Outer 를 따라간다.
	static const UObject* FindSlotOwnerOf(const UWidget& Entry);

	// 이 태그의 목록이 바뀌었다. 적용 중에 또 오면 끝난 뒤 다시 맞춘다.
	void RefreshEntries();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildTreeIfEmpty();
	void ApplyEntries(const TArray<FVBUISlotEntry>& Entries);

	FGameplayTag SlotTag;
	TWeakObjectPtr<const UObject> SlotOwner;
	// 붙은 등록소. 해체 때 여기서 뗀다.
	TWeakObjectPtr<UVBUISlotSubsystem> Registry;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> SlotCanvas;

	// 항목 Id -> 만든 위젯. 다시 구성돼도 지우지 않는다.
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UUserWidget>> EntryWidgets;

	// 만들기에 실패한 항목 Id(경고는 한 번).
	TSet<int32> FailedEntryIds;

	bool bApplying = false;
	bool bDirty = false;
};
