#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_Execute.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameplayEffect.h"
#include "VBTestAbilityTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "VBAbilityFailureTestAbilities.generated.h"

// 실제 duration GE 와 실제 CanActivate 경로를 쓴다 - 실패 조건을 바꿔 끼우거나 설정을 고치지 않는다
UCLASS(NotBlueprintable)
class UVBAbilityFailureCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBAbilityFailureCooldownEffect()
	{
		DurationPolicy = EGameplayEffectDurationType::HasDuration;
		DurationMagnitude = FScalableFloat(5.f);
		FInheritedTagContainer Tags;
		Tags.AddTag(VBGameplayTags::Cooldown_Execute);
		UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
		GEComponents.Add(TargetTags);
		TargetTags->SetAndApplyTargetTagChanges(Tags);
	}
};

UCLASS(NotBlueprintable)
class UVBAbilityFailureOptInExecute : public UVBGA_Execute
{
	GENERATED_BODY()
public:
	UVBAbilityFailureOptInExecute()
	{
		bShowFailureToPlayer = true;
		CooldownGameplayEffectClass = UVBAbilityFailureCooldownEffect::StaticClass();
	}
};

UCLASS(NotBlueprintable)
class UVBAbilityFailureOptOutExecute : public UVBAbilityFailureOptInExecute
{
	GENERATED_BODY()
public:
	UVBAbilityFailureOptOutExecute() { bShowFailureToPlayer = false; }
};

UCLASS(NotBlueprintable)
class UVBAbilityFailureSuccess : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBAbilityFailureSuccess()
	{
		InputTag = VBGameplayTags::Input_Execute;
		SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility));
	}

	int32 ExecuteCount = 0;
protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override
	{
		++ExecuteCount;
	}
};
