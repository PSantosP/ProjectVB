// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Enhancement/VBEnhancementRules.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBEnhancementComponent.generated.h"

class UVBEnhancementConfig;
class UVBItemDefinition;

USTRUCT()
struct FVBItemEnhanceLevel
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY()
	int32 Level = 0;
};

USTRUCT()
struct FVBWeaponEnhanceLevel
{
	GENERATED_BODY()

	UPROPERTY()
	EVBWeaponType Weapon = EVBWeaponType::None;

	UPROPERTY()
	int32 Level = 0;
};

// 서버는 단계를 바꾼 직후, 클라는 복제가 도착했을 때. 클라는 무엇이 바뀌었는지 모르므로 빈 대상(= 전부)으로 알린다.
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnEnhancementChanged, const FVBEnhanceTarget& /*Target*/);

/**
 * 강화 단계를 관리한다(AVBPlayerState 에 붙음). 아이템 정의, 무기 종류마다 단계 하나. 쓰기는 서버만, 복제는 그 플레이어에게만.
 * 저장의 모르는 이름은 버리지 않고 다시 쓴다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBEnhancementComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

	friend class FVBEnhancementTestAccess;

public:
	UVBEnhancementComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 강화. 없으면 nullptr.
	static UVBEnhancementComponent* FindFor(const AActor* Actor);

	int32 GetLevel(const FVBEnhanceTarget& Target) const;
	int32 GetMaxLevel() const;
	// 이 컴포넌트가 읽은 수치 자산(없으면 nullptr). 제작 컴포넌트도 이것을 읽는다.
	const UVBEnhancementConfig* GetConfig() const { return Config; }
	float GetItemStatScale(const UVBItemDefinition* Item) const;
	float GetWeaponDamageScale(EVBWeaponType Weapon) const;

	// 서버에서만. 갈래가 없는 대상이면 false. [0, MaxLevel] 로 자르고, 바뀌었으면 true 를 돌려주고 알린다.
	bool SetLevel(const FVBEnhanceTarget& Target, int32 Level);

	FVBOnEnhancementChanged OnLevelChanged;

	//~ IVBSaveParticipant - 강화 구역(UVBSaveGame::Enhancement). 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_Levels)
	TArray<FVBItemEnhanceLevel> ItemLevels;

	UPROPERTY(ReplicatedUsing=OnRep_Levels)
	TArray<FVBWeaponEnhanceLevel> WeaponLevels;

	UFUNCTION()
	void OnRep_Levels();

	// 읽었지만 지금 모르는 이름의 줄(아이템이 빠졌거나 enum 이름이 바뀜). 적용하지 않고 저장 때 그대로 다시 쓴다.
	TArray<FVBSavedItemLevel> DormantItems;
	TArray<FVBSavedWeaponLevel> DormantWeapons;

	// 수치 자산. InitializeComponent 에서 한 번 설정에서 읽는다(시험이 바꿔 끼운다).
	UPROPERTY(Transient)
	TObjectPtr<const UVBEnhancementConfig> Config;

	const FVBEnhanceTrack* FindTrack(EVBEnhanceCategory Category) const;
	bool HasWriteAuthority(const TCHAR* What) const;
};
