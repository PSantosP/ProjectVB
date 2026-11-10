// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "VBUISlotSubsystem.generated.h"

class APlayerController;
class UUserWidget;
class UVBUISlotSubsystem;
class UVBUISlotWidget;

// 등록 하나의 손잡이. 사라져도 저절로 풀리지 않으니 Unregister 로 푼다.
struct VB_API FVBUISlotHandle
{
	void Unregister();
	// 자리에 알리지 않고 등록만 지운다. GC 중(BeginDestroy)에는 자리를 건드리면 안 되어서 쓴다.
	void ForgetWithoutNotify();
	bool IsValid() const { return Owner.IsValid() && Id != INDEX_NONE; }
	int32 GetId() const { return Id; }

private:
	friend class UVBUISlotSubsystem;

	TWeakObjectPtr<UVBUISlotSubsystem> Owner;
	int32 Id = INDEX_NONE;
};

// 슬롯에 위젯 하나를 띄워 달라는 요청.
struct FVBUISlotRequest
{
	FGameplayTag Slot;
	TSubclassOf<UUserWidget> WidgetClass;
	// 같은 슬롯 안 그리기 순서. 큰 값이 위, 같으면 먼저 등록한 것이 아래.
	int32 Order = 0;
	// 새로 만든 항목에 붙인 직후 한 번만 적용한다.
	TOptional<ESlateVisibility> InitialVisibility;
	// 비면 모든 슬롯이 받고, 있으면 그 소유자가 만든 슬롯만 받는다.
	const UObject* Scope = nullptr;
	// 등록한 쪽. 못 푼 채 사라지면 항목을 걷는 보조 장치다(명시적 해제를 대신하지 않는다).
	const UObject* Registrant = nullptr;
};

// 등록소 안의 항목. 리플렉션 복사가 칸을 빠뜨리지 않게 전부 UPROPERTY 다.
USTRUCT()
struct FVBUISlotEntry
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = INDEX_NONE;

	UPROPERTY()
	FGameplayTag Slot;

	UPROPERTY()
	TSubclassOf<UUserWidget> WidgetClass;

	UPROPERTY()
	int32 Order = 0;

	UPROPERTY()
	bool bHasInitialVisibility = false;

	UPROPERTY()
	ESlateVisibility InitialVisibility = ESlateVisibility::Visible;

	UPROPERTY()
	TWeakObjectPtr<UObject> Scope;

	UPROPERTY()
	bool bScoped = false;

	UPROPERTY()
	TWeakObjectPtr<UObject> Registrant;

	UPROPERTY()
	bool bHasRegistrant = false;
};

/**
 * UI 슬롯 등록소(로컬 플레이어마다 하나, Lyra UIExtension 과 같은 구조).
 * 슬롯 태그에 위젯 클래스를 등록하면 그 태그의 자리(UVBUISlotWidget)가 만들어 붙인다.
 */
UCLASS()
class VB_API UVBUISlotSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

	friend class FVBUISlotTestAccess;

public:
	// 로컬 플레이어가 없는 컨트롤러면 null(정상 경로).
	static UVBUISlotSubsystem* Get(const APlayerController* PC);

	// 빈 값, 중복 등록은 무효 손잡이. 돌아오기 전에 자리가 위젯을 만들 수 있다.
	FVBUISlotHandle RegisterWidget(const FVBUISlotRequest& Request);
	void Unregister(int32 Id);
	// 자리에 알리지 않고 지운다(FVBUISlotHandle::ForgetWithoutNotify).
	void Forget(int32 Id);

	// 자리 위젯이 구성, 해체 때 부른다.
	void AddSlot(UVBUISlotWidget& SlotWidget);
	void RemoveSlot(UVBUISlotWidget& SlotWidget);
	bool HasSlot(const UVBUISlotWidget& SlotWidget) const;

	// 이 자리가 받을 항목. Order, 같으면 등록 순으로 정렬해 돌려준다.
	TArray<FVBUISlotEntry> GetEntriesFor(const UVBUISlotWidget& SlotWidget) const;

private:
	bool Accepts(const FVBUISlotEntry& Entry, const UVBUISlotWidget& SlotWidget) const;
	// 등록자, 범위가 사라진 항목을 걷고, 걷힌 태그의 자리에 알린다.
	void PruneDeadRegistrants();
	void NotifySlots(const FGameplayTag& Slot);

	UPROPERTY(Transient)
	TArray<FVBUISlotEntry> Entries;

	TArray<TWeakObjectPtr<UVBUISlotWidget>> Slots;
	int32 NextId = 1;
};
