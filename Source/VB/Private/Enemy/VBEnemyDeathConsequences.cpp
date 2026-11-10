// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEnemyDeathConsequences.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "Data/VBEnemyConfig.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VB/VB.h"

void VBEnemyDeathConsequences::ApplyDefaultReputation(AActor& Enemy, const FVBDeathFacts& Facts,
	const UVBEnemyConfig& Config, TSubclassOf<UGameplayEffect> ReputationEffect)
{
	if (Facts.bMedical)
	{
		VB_LOG(Log, "%s: medical death; the medical ability owns reputation", *Enemy.GetName());
		return;
	}

	AActor* Instigator = Facts.Instigator.Get();
	if (!Instigator)
	{
		VB_LOG(Warning, "%s: death has no instigator; reputation skipped", *Enemy.GetName());
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Instigator);
	if (!ASC)
	{
		VB_LOG(Warning, "%s: instigator %s has no ASC; reputation skipped", *Enemy.GetName(), *Instigator->GetName());
		return;
	}

	if (!ASC->GetSet<UVBReputationAttributeSet>())
	{
		VB_LOG(Log, "%s: instigator %s has no reputation set; reputation skipped", *Enemy.GetName(), *Instigator->GetName());
		return;
	}

	if (!ReputationEffect)
	{
		VB_LOG(Warning, "%s: default death reputation effect is unset", *Enemy.GetName());
		return;
	}

	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(ReputationEffect, 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(VBGameplayTags::Data_ReputationChange, Config.DeathReputationChange);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		VB_LOG(Log, "%s death -> %s reputation %.0f", *Enemy.GetName(), *Instigator->GetName(), Config.DeathReputationChange);
	}
}

void VBEnemyDeathConsequences::AnnounceKill(AActor& Enemy, const FVBDeathFacts& Facts, const UVBEnemyConfig& Config)
{
	APlayerState* Killer = Cast<APlayerState>(Facts.Instigator.Get());
	if (!Killer)
	{
		return;
	}

	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(&Enemy))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Enemy_Killed;
		Event.TargetTags = Config.EnemyTags;
		Event.Instigator = Killer;
		Events->Broadcast(Event);
	}

	if (!Config.OnKilledEffects.IsEmpty())
	{
		FVBRuleContext Context = FVBRuleContext::Make(&Enemy, Killer);
		Context.Counterpart = &Enemy;
		VBRules::ApplyAll(Config.OnKilledEffects, Context);
	}
}
