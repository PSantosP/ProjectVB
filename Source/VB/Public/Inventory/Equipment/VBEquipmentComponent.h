// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBEquipmentComponent.generated.h"

class APlayerState;
class UAbilitySystemComponent;
class UGameplayEffect;
class UVBItemDefinition;
struct FVBEnhanceTarget;

DECLARE_MULTICAST_DELEGATE(FVBOnEquipmentChanged);

/**
 * 장착한 장비를 관리한다(AVBPlayerState 에 붙음). 장착한 것도 인벤토리 수에 남고, 가진 수가 0 이 되면 자동으로 해제한다.
 * 칸마다 EquipmentEffect 하나에 능력치를 SetByCaller 로 싣는다. 쓰기는 서버만, ASC 가 없으면 입히지 않는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBEquipmentComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBEquipmentComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 장비. 없으면 nullptr.
	static UVBEquipmentComponent* FindFor(const AActor* Actor);

	const TArray<FGameplayTag>& GetSlotLayout() const { return SlotLayout; }
	const UVBItemDefinition* GetEquipped(int32 SlotIndex) const
	{
		return Equipped.IsValidIndex(SlotIndex) ? Equipped[SlotIndex].Get() : nullptr;
	}

	int32 FindSlotOf(const UVBItemDefinition* Item) const;

	// 표시가 부른다. SlotIndex 가 음수면 서버가 칸을 고른다(맞는 빈 칸 먼저).
	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestEquip(const UVBItemDefinition* Item, int32 SlotIndex = -1);

	UFUNCTION(BlueprintCallable, Category="VB|Item")
	void RequestUnequip(int32 SlotIndex);

	// 서버에서만. 입었으면(또는 이미 그 칸이면) true.
	bool Equip(const UVBItemDefinition* Item, int32 SlotIndex);
	bool Unequip(int32 SlotIndex);

	// 서버에서만. Item 을 입은 칸(nullptr = 모두)의 효과를 지금 강화 단계로 다시 건다. 저장을 읽는 중이면 미룬다.
	void RefreshItem(const UVBItemDefinition* Item);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnEquipmentChanged OnEquipmentChanged;

	//~ IVBSaveParticipant - 장비 구역(UVBSaveGame::Equipment). 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 칸 배치(칸마다 종류). 순서를 바꿔도 저장은 같은 종류 같은 번째 칸으로 옮겨 읽는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Item", meta=(Categories="Equipment.Slot"))
	TArray<FGameplayTag> SlotLayout;

	// 칸마다 거는 무한 효과(SetByCaller Data.Stat.*). 비면 입히지 않고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Item")
	TSubclassOf<UGameplayEffect> EquipmentEffect;

private:
	// 칸마다 입은 아이템(빈 칸 = nullptr). 그 플레이어만 안다.
	UPROPERTY(ReplicatedUsing=OnRep_Equipped)
	TArray<TObjectPtr<const UVBItemDefinition>> Equipped;

	UFUNCTION()
	void OnRep_Equipped();

	UFUNCTION(Server, Reliable)
	void ServerEquip(const UVBItemDefinition* Item, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerUnequip(int32 SlotIndex);

	// 서버 전용. 칸마다 건 효과의 핸들.
	TArray<FActiveGameplayEffectHandle> EffectHandles;

	bool ApplyEffect(int32 SlotIndex);
	void RemoveEffect(int32 SlotIndex);
	void RemoveAllEffects();
	void HandleInventoryChanged();
	void HandleSaveApplied();
	void BroadcastEvent(const FGameplayTag& EventTag, const UVBItemDefinition* Item) const;
	bool HasWriteAuthority(const TCHAR* What) const;
	APlayerState* GetOwningPlayerState() const;
	UAbilitySystemComponent* GetAbilitySystem() const;

	FDelegateHandle InventoryChangedHandle;

	// 저장을 읽는 동안 소유 판정을 미뤘다가 OnSaveApplied 에서 한 번 한다.
	TWeakObjectPtr<class UVBSaveParticipantSubsystem> SaveRegistry;
	FDelegateHandle SaveAppliedHandle;

	// 강화 단계 변화 구독(같은 PlayerState 의 UVBEnhancementComponent).
	void HandleEnhancementChanged(const FVBEnhanceTarget& Target);
	TWeakObjectPtr<class UVBEnhancementComponent> EnhancementSource;
	FDelegateHandle EnhancementChangedHandle;
};
