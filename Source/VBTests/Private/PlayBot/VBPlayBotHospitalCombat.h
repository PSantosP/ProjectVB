// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotHospitalOpening.h"
class UAbilitySystemComponent;
class AVBEnemyBase;
class APlayerState;
struct FGameplayEventData;
struct FVBPlayBotRunConfig;

struct FVBPlayBotCombatTargetRef
{
	FString ActorPath;
	FString ClassPath;
	FGameplayTag DefeatedFact;
};
struct FVBPlayBotCombatPolicy
{
	FName AttackAction;
	double AttackRangeCm = 0.0;
	double AttackHeightCm = 0.0;
	double AttackIntervalSeconds = 0.0;
	double MinStaminaFraction = -1.0;
	double NoProgressSeconds = 0.0;
	int32 MaxAttacks = 0;
	bool IsValid(FString& Error) const;
};
struct FVBPlayBotHospitalFightSpec
{
	TArray<FVBPlayBotCombatTargetRef> Targets;
	FVBPlayBotCombatPolicy Policy;
	static bool ParseTargets(const TArray<FString>& Rows, TArray<FVBPlayBotCombatTargetRef>& Out, FString& Error);
	bool Load(const FVBPlayBotRunConfig& Config, FName Group, FString& Error);
};
struct FVBPlayBotCombatTargetView
{
	FName Id;
	FVector Position = FVector::ZeroVector;
	double Health = 0.0;
	double MaxHealth = 0.0;
	bool bDead = false;
	bool bDefeatedFact = false;
	bool bVisible = false;
	int32 PlayerDamageHits = 0;
};
struct FVBPlayBotHospitalCombatView
{
	FVBPlayBotOpeningView Movement;
	TArray<FVBPlayBotCombatTargetView> Targets;
	double PlayerHealth = 0.0;
	double PlayerMaxHealth = 0.0;
	double Stamina = 0.0;
	double MaxStamina = 0.0;
	bool bPlayerDead = false;
};

// 실제 적과 피격 알림을 관측하고 구독 수명을 직접 닫는다.
class FVBPlayBotHospitalCombatObservation
{
public:
	FVBPlayBotHospitalCombatObservation() = default;
	~FVBPlayBotHospitalCombatObservation();
	FVBPlayBotHospitalCombatObservation(const FVBPlayBotHospitalCombatObservation&) = delete;
	FVBPlayBotHospitalCombatObservation& operator=(const FVBPlayBotHospitalCombatObservation&) = delete;
	bool Bind(UWorld* World, const FVBPlayBotHospitalFightSpec& Spec, FString& Error);
	bool Read(UWorld* World, FVBPlayBotHospitalCombatView& Out, FString& Error);
	void Unbind();
private:
	struct FTarget
	{
		FVBPlayBotCombatTargetRef Ref;
		TWeakObjectPtr<AVBEnemyBase> Actor;
		TWeakObjectPtr<UAbilitySystemComponent> ASC;
		FDelegateHandle HitHandle;
		FVector LastPosition = FVector::ZeroVector;
		double MaxHealth = 0.0;
		int32 PlayerDamageHits = 0;
	};
	void OnHit(const FGameplayEventData* Payload, int32 Index);
	FVBPlayBotHospitalObservation PlayerObservation;
	TWeakObjectPtr<ACharacter> Player;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<APlayerState> PlayerState;
	TArray<FTarget> Targets;
};

// 경공격과 이동만 하는 기본 전투 정책이다. 게임 상태는 바꿀 수 없다.
class FVBPlayBotHospitalCombatActions
{
public:
	bool Configure(const FVBPlayBotCombatPolicy& Policy, FString& Error);
	void NotifyInjected(int32 Id, double Seconds);
	EVBPlayBotOpeningResult Tick(const FVBPlayBotHospitalCombatView& View, double Seconds,
		FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error);
private:
	FVBPlayBotCombatPolicy Policy;
	FVBPlayBotNativeSteering Steering;
	int32 PressId = INDEX_NONE;
	int32 InjectedAttacks = 0;
	int32 LastDamageHits = 0;
	double NextAttackSeconds = 0.0;
	double ProgressSeconds = -1.0;
	double CompleteSeconds = -1.0;
};
