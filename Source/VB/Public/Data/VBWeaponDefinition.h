// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/VBDodgeConfig.h"           // FVBDodgeMontageSet - 값 멤버
#include "Data/VBParryConfig.h"           // FVBGuardSet - 값 멤버
#include "Types/VBWeaponStateTypes.h"     // EVBWeaponType, FVBWeaponSummonData
#include "VBWeaponDefinition.generated.h"

class UVBAbilitySet;
class UVBAttackConfig;
class UVBAirComboConfig;
class UVBWeaponHitVisuals;

/**
 * 무기 하나의 데이터 - 무기별로 다른 것은 전부 여기 있다(Lyra ULyraEquipmentDefinition). 손에 들면 능력 세트를 준다.
 * 칸이 EditAnywhere 인 이유: 파이썬 저작이 EditDefaultsOnly 구조체 칸에 값을 못 넣는다.
 */
UCLASS(BlueprintType)
class VB_API UVBWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	EVBWeaponType GetWeaponType() const { return WeaponType; }
	const FText& GetDisplayName() const { return DisplayName; }
	// 이 무기를 꺼내는 숫자 키(Input.WeaponSlot.N). 0 이면 숫자 키가 없다(휠, Tab 으로만).
	int32 GetInputSlot() const { return InputSlot; }
	const FVBWeaponSummonData& GetSummonData() const { return SummonData; }
	const UVBAbilitySet* GetAbilitySet() const { return AbilitySet; }
	UVBAttackConfig* GetLightAttack() const { return LightAttack; }
	UVBAttackConfig* GetRiposte() const { return Riposte; }
	// 공중 콤보. nullptr = 이 무기는 띄운 적을 쫓지 않는다.
	const UVBAirComboConfig* GetAirCombo() const { return AirCombo; }
	// 이 무기로 맞힌 접점의 이펙트. nullptr = 타격 큐 자신의 모습.
	const UVBWeaponHitVisuals* GetHitVisuals() const { return HitVisuals; }
	// 가드 몽타주, 섹션. 가드할 수 있는가는 이 칸이 아니라 능력 세트에 가드 능력이 있는가다.
	const FVBGuardSet& GetGuardSet() const { return GuardSet; }
	// 전용 회피가 없는 무기는 nullptr - 회피 설정의 공용 세트를 쓴다.
	const FVBDodgeMontageSet* FindDodgeSet() const { return bHasDodgeSet ? &DodgeSet : nullptr; }
	// 질주 중 회피 = 구르기. 구르기 클립이 없는 무기는 nullptr - 질주 중에도 스텝.
	const FVBDodgeMontageSet* FindRollSet() const { return bHasRollSet ? &RollSet : nullptr; }
	const FVBDodgeToRun& GetDodgeToRun(bool bRoll) const { return bRoll ? RollToRun : DodgeToRun; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 검증의 판정부 - 문제 하나에 한 줄. 세트가 주는 공격마다 이 무기의 잎이 있어야 하고, 가드를 주면 가드 몽타주 섹션이 맞아야 한다.
	TArray<FText> FindProblems() const;

private:
	UPROPERTY(EditAnywhere, Category="VB|Weapon")
	EVBWeaponType WeaponType = EVBWeaponType::None;

	// 플레이어가 읽는 이름(무기 휠). 비면 저장 검사가 막는다.
	UPROPERTY(EditAnywhere, Category="VB|Weapon")
	FText DisplayName;

	// 숫자 키 칸. 한 칸을 나누는 무기는 함께 가지지 않는다(프롤로그 한손검 -> 카타나).
	UPROPERTY(EditAnywhere, Category="VB|Weapon", meta=(ClampMin="0", ClampMax="3"))
	int32 InputSlot = 0;

	// 꺼내기, 넣기, 바꾸기 연기, MM 풀, 처형, 장착 태그.
	UPROPERTY(EditAnywhere, Category="VB|Weapon")
	FVBWeaponSummonData SummonData;

	// 이 무기를 손에 들면 받는 능력과 효과. 무기 상태가 꺼낼 때 주고 넣을 때 거둔다(SourceObject = 이 정의).
	UPROPERTY(EditAnywhere, Category="VB|Weapon")
	TObjectPtr<const UVBAbilitySet> AbilitySet;

	// 약공격 콤보의 잎. 세트가 약공격을 주면 비어 있을 수 없다(검증).
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Combat")
	TObjectPtr<UVBAttackConfig> LightAttack;

	// 패링 뒤 반격의 잎.
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Combat")
	TObjectPtr<UVBAttackConfig> Riposte;

	UPROPERTY(EditAnywhere, Category="VB|Weapon|Combat")
	TObjectPtr<UVBAirComboConfig> AirCombo;

	UPROPERTY(EditAnywhere, Category="VB|Weapon|Combat")
	TObjectPtr<const UVBWeaponHitVisuals> HitVisuals;

	// 세트가 가드를 줄 때 쓰는 가드 콘텐츠. 몽타주가 비어 있으면 애니 없는 가드다(판정은 그대로).
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Guard")
	FVBGuardSet GuardSet;

	// 전용 회피가 있는가. 없으면 회피 설정의 공용 세트로, 있어도 빈 방향은 공용 세트로 내려간다.
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge")
	bool bHasDodgeSet = false;

	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge", meta=(EditCondition="bHasDodgeSet"))
	FVBDodgeMontageSet DodgeSet;

	// 구르기 세트가 있는가(Fighter). 빈 방향은 스텝으로 간다.
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge")
	bool bHasRollSet = false;

	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge", meta=(EditCondition="bHasRollSet"))
	FVBDodgeMontageSet RollSet;

	// 스텝, 구르기 끝에서 달리기로 이어 가는 클립. 비면 회피 클립이 끝까지 돈다.
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge")
	FVBDodgeToRun DodgeToRun;
	UPROPERTY(EditAnywhere, Category="VB|Weapon|Dodge")
	FVBDodgeToRun RollToRun;
};
