#pragma once

#include "CoreMinimal.h"
#include "VBCombatChargeCredit.generated.h"

class UAbilitySystemComponent;
class AActor;
struct FGameplayEffectSpec;

UENUM()
enum class EVBCombatChargeSource : uint8
{
	Unspecified,
	OrdinaryAttack,
	CinematicSkill
};

UENUM(BlueprintType)
enum class EVBCombatChargeReason : uint8
{
	None,
	Hit,
	Parry,
	Finisher
};

// FVBCombatChargeCredit - 서버가 판정해 플레이어 한 명의 자원 소유자에게 바로 전달하는 충전량
USTRUCT()
struct VB_API FVBCombatChargeCredit
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> RecipientASC;
	UPROPERTY()
	TObjectPtr<AActor> RecipientAvatar;
	UPROPERTY()
	TObjectPtr<AActor> Opponent;
	UPROPERTY()
	EVBCombatChargeReason Reason = EVBCombatChargeReason::None;
	UPROPERTY()
	float AppliedDamage = 0.0f;
};

namespace VBCombatCharge
{
	VB_API void Publish(const UObject* WorldContext, const FVBCombatChargeCredit& Credit);
}

// FVBCombatChargeSnapshot - 데미지 콜백이 두 캐릭터를 바꾸거나 제거하기 전에 자격을 기록해 둔다
struct VB_API FVBCombatChargeSnapshot
{
	static FVBCombatChargeSnapshot Capture(UAbilitySystemComponent* Victim, const FGameplayEffectSpec& Spec);
	void PublishResolved(const UObject* WorldContext, bool bGuarded, bool bPerfectParry, float AppliedDamage) const;

private:
	FGuid Id;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<UAbilitySystemComponent> VictimASC;
	TWeakObjectPtr<AActor> SourceAvatar;
	TWeakObjectPtr<AActor> VictimAvatar;
	EVBCombatChargeSource SourceKind = EVBCombatChargeSource::Unspecified;
	bool bVictimEligible = false;
	bool bSourceEligible = false;
	bool bHostile = false;
	bool bFinisher = false;
};
