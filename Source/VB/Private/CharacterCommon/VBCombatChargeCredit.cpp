#include "CharacterCommon/VBCombatChargeCredit.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBTargetable.h"
#include "Engine/World.h"
#include "GenericTeamAgentInterface.h"
#include "Messages/VBGameplayMessageSubsystem.h"

void VBCombatCharge::Publish(const UObject* WorldContext, const FVBCombatChargeCredit& Credit)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client || !Credit.RecipientASC
		|| !Credit.RecipientASC->IsOwnerActorAuthoritative())
	{
		return;
	}

	if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(WorldContext))
	{
		Bus->BroadcastMessage(VBGameplayTags::Message_Combat_ChargeCredit, Credit);
	}
}

FVBCombatChargeSnapshot FVBCombatChargeSnapshot::Capture(UAbilitySystemComponent* Victim, const FGameplayEffectSpec& Spec)
{
	FVBCombatChargeSnapshot Snapshot;
	Snapshot.Id = FGuid::NewGuid();
	Snapshot.VictimASC = Victim;
	Snapshot.SourceASC = Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
	Snapshot.SourceAvatar = Spec.GetContext().GetEffectCauser();
	Snapshot.VictimAvatar = Victim ? Victim->GetAvatarActor() : nullptr;
	Snapshot.bSourceEligible = IVBTargetable::IsActorTargetable(Snapshot.SourceAvatar.Get())
		&& !UVBAbilitySystemStatics::IsActorDead(Snapshot.SourceAvatar.Get());
	Snapshot.bVictimEligible = Victim && Victim->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) > 0.0f
		&& IVBTargetable::IsActorTargetable(Snapshot.VictimAvatar.Get());
	Snapshot.bHostile = Snapshot.SourceAvatar.IsValid() && Snapshot.VictimAvatar.IsValid()
		&& Snapshot.SourceAvatar != Snapshot.VictimAvatar
		&& FGenericTeamId::GetAttitude(Snapshot.SourceAvatar.Get(), Snapshot.VictimAvatar.Get()) == ETeamAttitude::Hostile;
	Snapshot.bFinisher = UVBAbilitySystemStatics::IsFinisherDamage(Spec);
	if (const FVBGameplayEffectContext* Context = FVBGameplayEffectContext::Get(Spec.GetContext()))
	{
		Snapshot.SourceKind = Context->GetChargeSource();
	}

	return Snapshot;
}

void FVBCombatChargeSnapshot::PublishResolved(const UObject* WorldContext, bool bGuarded, bool bPerfectParry, float AppliedDamage) const
{
	if (!bHostile || !bVictimEligible)
	{
		return;
	}

	FVBCombatChargeCredit Credit;
	Credit.Id = Id;
	Credit.AppliedDamage = AppliedDamage;
	if (bPerfectParry && bSourceEligible)
	{
		Credit.RecipientASC = VictimASC.Get();
		Credit.RecipientAvatar = VictimAvatar.Get();
		Credit.Opponent = SourceAvatar.Get();
		Credit.Reason = EVBCombatChargeReason::Parry;
	}
	else if (!bGuarded && FMath::IsFinite(AppliedDamage) && AppliedDamage > 0.0f
		&& (bFinisher || SourceKind == EVBCombatChargeSource::OrdinaryAttack))
	{
		Credit.RecipientASC = SourceASC.Get();
		Credit.RecipientAvatar = SourceAvatar.Get();
		Credit.Opponent = VictimAvatar.Get();
		Credit.Reason = bFinisher ? EVBCombatChargeReason::Finisher : EVBCombatChargeReason::Hit;
	}
	else
	{
		return;
	}

	VBCombatCharge::Publish(WorldContext, Credit);
}
