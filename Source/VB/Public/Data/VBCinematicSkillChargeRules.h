#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CharacterCommon/VBCombatChargeCredit.h"
#include "VBCinematicSkillChargeRules.generated.h"

class UGameplayEffect;
class UVBCinematicSkillDefinition;

// 충전량과 효과 - 현재 값과 상한은 CombatSet 이 관리한다
UCLASS(BlueprintType)
class VB_API UVBCinematicSkillChargeRules : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge")
	TObjectPtr<const UVBCinematicSkillDefinition> SkillDefinition;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge", meta=(ClampMin="0"))
	float NormalHitGain = 2.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge", meta=(ClampMin="0"))
	float ParryGain = 15.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge", meta=(ClampMin="0"))
	float FinisherGain = 30.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge", meta=(ClampMin="1"))
	int32 RecentCreditLimit = 128;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge")
	TSubclassOf<UGameplayEffect> GainEffect;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|CinematicSkill|Charge")
	TSubclassOf<UGameplayEffect> ReadyFeedbackEffect;

	float GetGain(EVBCombatChargeReason Reason) const;
	FString ValidateSetup() const;
	static bool IsFull(float Current, float Maximum);
	static FString ValidateCost(const UGameplayEffect* Effect, float Level = 1.0f);
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
