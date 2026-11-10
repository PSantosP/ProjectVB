#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_CinematicSkill.h"
#include "Character/VBGuardable.h"
#include "Character/VBTargetable.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "GenericTeamAgentInterface.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBCinematicSkillGaugeTestTypes.generated.h"

class UVBHealthAttributeSet;
class UVBCombatAttributeSet;

UCLASS(NotBlueprintable)
class UVBGaugeTestWeapons : public UVBWeaponStateComponent
{
	GENERATED_BODY()
public:
	void SetFixtureOwned(bool bOwned)
	{
		OwnedWeapons = bOwned ? TArray<EVBWeaponType>{EVBWeaponType::Fighter} : TArray<EVBWeaponType>{};
		OnOwnedWeaponsChanged.Broadcast();
	}

	void SetFixtureSelected(EVBWeaponType Weapon)
	{
		CurrentWeaponType = Weapon;
		LocomotionState = Weapon == EVBWeaponType::None ? EVBLocomotionState::Unarmed : EVBLocomotionState::ArmedCombat;
		OnLocomotionStateChanged.Broadcast(LocomotionState, Weapon);
	}
};

UCLASS(NotBlueprintable)
class AVBGaugeTestPlayerState : public APlayerState, public IAbilitySystemInterface, public IVBSaveParticipant
{
	GENERATED_BODY()
public:
	AVBGaugeTestPlayerState();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override {}
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override { if (DuringSaveRead) DuringSaveRead(); }
	TFunction<void()> DuringSaveRead;
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> Health;
	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> Combat;
};

// fixture 가 가진 몸과 ASC 로 실제 health, guard 판정을 그대로 쓴다.
UCLASS(NotBlueprintable)
class AVBGaugeTestPawn : public APawn, public IAbilitySystemInterface, public IGenericTeamAgentInterface,
	public IVBTargetable, public IVBGuardable
{
	GENERATED_BODY()
public:
	AVBGaugeTestPawn();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(Team); }
	virtual bool IsTargetable() const override { return bTargetable; }
	virtual FVector GetTargetMarkerLocation() const override { return GetActorLocation(); }
	virtual bool IsGuarding() const override { return bGuarding; }
	virtual float GetGuardElapsed() const override { return GuardElapsed; }
	virtual void PlayGuardAccept(EVBHitDirection) override {}
	virtual const UVBParryConfig* GetParryConfig() const override;
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) override {}
	virtual void ApplyGuardStaminaDelta(float Delta) override {}
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;
	UPROPERTY()
	TObjectPtr<UVBGaugeTestWeapons> Weapons;
	// 패링 게이지(ParryPoiseDamage)를 끈 패링 설정 - 그 줄은 VB.Unit.Combat.PostureExecute 가 잰다.
	UPROPERTY()
	TObjectPtr<UVBParryConfig> ParryConfig;
	uint8 Team = 0;
	bool bTargetable = true;
	bool bGuarding = false;
	float GuardElapsed = 0.0f;
};

UCLASS(NotBlueprintable)
class UVBGaugeTestGain : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBGaugeTestGain();
};

UCLASS(NotBlueprintable)
class UVBGaugeTestCost : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBGaugeTestCost();
};

UCLASS(NotBlueprintable)
class UVBGaugeTestDamage : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBGaugeTestDamage();
};

UCLASS(NotBlueprintable)
class UVBGaugeTestReadyFeedback : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVBGaugeTestReadyFeedback();
};

// 실제 진입 판정(CanActivateAbility, 게이지 CheckCost, CommitAbility)은 두고 연출 검사만 떼어 낸다.
UCLASS(NotBlueprintable)
class UVBGaugeTestSkill : public UVBGA_CinematicSkill
{
	GENERATED_BODY()
public:
	UVBGaugeTestSkill();
protected:
	virtual bool CanStageShow(const ACharacter*) const override { return true; }
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayAbilityActivationInfo, const FGameplayEventData*) override {}
};

// OnGaugeStateChanged 방송 횟수를 센다 - dynamic delegate 라 UFUNCTION 대상이 필요하다.
UCLASS(NotBlueprintable)
class UVBGaugeTestStateListener : public UObject
{
	GENERATED_BODY()
public:
	int32 Count = 0;
	UFUNCTION()
	void Heard() { ++Count; }
};
