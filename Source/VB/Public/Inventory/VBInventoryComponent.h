// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "SaveSystem/VBSaveGameTypes.h"   // FVBSavedItem - 못 찾은 저장 줄을 값으로 지킨다
#include "Inventory/VBInventoryTypes.h"
#include "Inventory/VBItemAmount.h"
#include "VBInventoryComponent.generated.h"

class UVBItemDefinition;
class APlayerState;
struct FInstancedStruct;
struct FVBRuleContext;

DECLARE_MULTICAST_DELEGATE(FVBOnInventoryChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnItemAcquired, const UVBItemDefinition*, int32);

/**
 * 가진 아이템 수량의 서버 정본. 폰이 다시 나도 남도록 AVBPlayerState 에 붙고, 넣고 빼는 길은 이 API 하나다.
 * 교환 계산은 VBInventoryExchangeRules, 저장 해석은 VBInventorySaveRules, 사용 시작과 버린 몫 떨구기는 VBItemActions 가 맡는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBInventoryComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBInventoryComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 인벤토리. 없으면 nullptr.
	static UVBInventoryComponent* FindFor(const AActor* Actor);

	int32 GetCount(const UVBItemDefinition* Item) const;
	const TArray<FVBInventoryEntry>& GetEntries() const { return List.Entries; }

	// 서버에서만. 상한에서 자르고 실제로 넣은 수를 돌려준다. 넣었으면 Event.Item.Acquired 방송 + 소유 클라이언트에 획득 알림.
	int32 AddItem(const UVBItemDefinition* Item, int32 Count);

	// 서버에서만. 모자라면 아무것도 안 빼고 false - 반쪽 거래를 막는다.
	bool RemoveItem(const UVBItemDefinition* Item, int32 Count);

	// 서버에서만. 내는 것(Costs)과 받는 것(Gains)을 한 번에 바꾼다(제작, 강화). 하나라도 안 되면 아무것도 안 바꾸고 false.
	//   알림은 끝난 뒤 OnInventoryChanged 한 번으로 모은다.
	bool TryExchange(TConstArrayView<FVBItemAmount> Costs, TConstArrayView<FVBItemAmount> Gains);

	// 표시가 부른다. 판단은 서버가 다시 한다.
	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestUseItem(const UVBItemDefinition* Item);

	// 지금 이 아이템을 쓸 수 있어 보이나(미리 보기). 결정은 ConfirmUse 가 다시 한다.
	bool CanUseItem(const UVBItemDefinition* Item) const;

	// 표시가 부른다(버리기). 판단은 서버가 다시 한다. 버린 몫은 발밑에 떨어진다.
	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestDiscard(const UVBItemDefinition* Item, int32 Count);

	// 버릴 수 있어 보이나(표시용 - 버튼을 켜고 끄는 데만). bCanDiscard 이고 가진 것이 있으면.
	bool CanDiscard(const UVBItemDefinition* Item) const;

	// 서버에서만. 땅의 아이템을 먼저 만들고 성공했을 때만 뺀다(조용히 사라지지 않게). 버렸으면 true.
	bool Discard(const UVBItemDefinition* Item, int32 Count);

	// 서버에서만. 사용 확정 - 다시 검사한 뒤 소모 -> 효과 -> Event.Item.Used. 확정했으면 true.
	//   바로 쓰면 VBItemActions::BeginUse 가, 몽타주가 있으면 UVBGA_UseItem 이 표식 시각에 부른다.
	bool ConfirmUse(const UVBItemDefinition* Item);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnInventoryChanged OnInventoryChanged;

	// ---- 퀵슬롯. 칸은 가진 수가 0 이 돼도 비우지 않는다 - 다시 주우면 돌아온다.

	const TArray<TObjectPtr<const UVBItemDefinition>>& GetQuickSlots() const { return QuickSlots; }
	const UVBItemDefinition* GetQuickSlotItem(int32 SlotIndex) const
	{
		return QuickSlots.IsValidIndex(SlotIndex) ? QuickSlots[SlotIndex].Get() : nullptr;
	}

	// 표시가 부른다(인벤토리 화면의 칸 버튼). 판단은 서버가 한다. Item = nullptr 이면 칸을 비운다.
	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item);

	// 퀵슬롯 키가 부른다. 서버가 칸의 아이템을 사용 경로로 전달한다(빈 칸이면 무시).
	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestUseQuickSlot(int32 SlotIndex);

	// 칸이 바뀌었을 때. 가진 수 변화는 OnInventoryChanged 가 알린다.
	FVBOnInventoryChanged OnQuickSlotsChanged;

	// 이 플레이어의 화면에서만(획득 알림). 서버가 소유 클라이언트로 보낸다.
	FVBOnItemAcquired OnItemAcquired;

	//~ IVBSaveParticipant - 인벤토리 구역(UVBSaveGame::Inventory). 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	// 복제 콜백(FVBInventoryEntry)이 부른다.
	void HandleReplicatedChange() { OnInventoryChanged.Broadcast(); }

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 퀵슬롯 칸 수. 늘리면 AVBPlayerController::QuickSlotActions 에 입력 액션도 더한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Item", meta=(ClampMin="0", ClampMax="10"))
	int32 QuickSlotCount = 4;

private:
	bool CanApplyAcquireEffects(const UVBItemDefinition* Item, const TArray<FInstancedStruct>& Effects,
		const FVBRuleContext& Context) const;
	void BroadcastAcquired(const UVBItemDefinition* Item, int32 Added);
	// 획득 사건, 소유 클라이언트 알림(목록 변경 알림은 빼고). 교환은 변경 알림을 한 번만 낸다.
	void NotifyAcquired(const UVBItemDefinition* Item, int32 Added);
	// 알림 없이 한 줄의 수를 정한다(0 이하 = 줄 없음). 교환 전용 - 확인은 부르는 쪽이 끝냈다.
	void WriteCountSilently(const UVBItemDefinition* Item, int32 NewCount);

	// 교환의 한 줄. 정의는 cpp 에 둔다.
	struct FExchangeRow;
	using FExchangeRows = TArray<FExchangeRow, TInlineAllocator<8>>;
	// 모든 줄을 확인하고 획득 효과와 문맥을 채운다. 하나라도 안 되면 false.
	bool CheckExchangeRows(FExchangeRows& Rows) const;
	// 확인이 끝난 줄의 수를 알림 없이 모두 바꾼다.
	void WriteExchangeRows(const FExchangeRows& Rows);
	// 교환 뒤의 획득 효과, 변경 알림, 획득 사건을 차례로 낸다.
	void AnnounceExchange(const FExchangeRows& Rows);

	UPROPERTY(Replicated)
	FVBInventoryList List;

	// 칸마다 아이템 정의(빈 칸 = nullptr). 그 플레이어만 안다(가진 것과 같다).
	UPROPERTY(ReplicatedUsing=OnRep_QuickSlots)
	TArray<TObjectPtr<const UVBItemDefinition>> QuickSlots;

	UFUNCTION()
	void OnRep_QuickSlots();

	UFUNCTION(Server, Reliable)
	void ServerAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item);

	UFUNCTION(Server, Reliable)
	void ServerUseQuickSlot(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(const UVBItemDefinition* Item);

	UFUNCTION(Server, Reliable)
	void ServerDiscard(const UVBItemDefinition* Item, int32 Count);

	UFUNCTION(Client, Unreliable)
	void ClientItemAcquired(const UVBItemDefinition* Item, int32 Count);

	APlayerState* GetOwningPlayerState() const;
	bool HasWriteAuthority(const TCHAR* What) const;
	FVBInventoryEntry* FindEntry(const UVBItemDefinition* Item);
	const FVBInventoryEntry* FindEntry(const UVBItemDefinition* Item) const;

	// 불러왔지만 넣지 못한 저장 줄. 다음 저장에 그대로 다시 쓴다. 서버 전용, 복제 안 함.
	TArray<FVBSavedItem> KeptSavedItems;
};
