// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBAirStagingLease.h"
#include "VBGA_MeleeAttackBase.h"
#include "VBGA_AirCombo.generated.h"

class ACharacter;
class UVBAirComboConfig;
class UAbilityTask_ApplyRootMotionMoveToActorForce;

// 쫓을 기회(AirOpening)가 닫히는 이유 - 닫는 길은 ClearOpening 하나다.
UENUM()
enum class EVBAirOpeningEnd : uint8
{
	Timeout,
	VictimLanded,
	VictimDied,
	ChaseStarted,
	WeaponRemoved,
	Replaced,
	VictimGone,
};

/** 띄운 적의 추격 기회와 공중 이동을 관리하며, 연타 판정은 근접 공격 부모에 위임한다. */
UCLASS(Abstract)
class VB_API UVBGA_AirCombo : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_AirCombo();
	virtual UVBAttackConfig* SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const override;

	// 쫓을 기회가 열려 있나와 그 대상(서버 기준). 시험과 읽기용.
	bool IsOpeningOpen() const { return bOpeningOpen; }
	const ACharacter* GetOpeningVictim() const { return OpeningVictim.Get(); }

	// 공중에서 손을 놓는 한 곳 - 이 능력이 Flying 으로 바꿨을 때만 Falling 으로 되돌린다.
	static void ReleaseAirMovement(ACharacter* Character, bool bWeSetFlying);
	// 이 누름으로 쫓을 수 없는 이유(순수). nullptr = 쫓는다. 거절된 누름은 쫓을 기회를 닫지 않는다.
	static const TCHAR* FindChaseRefusal(const UVBAirComboConfig* AirCombo, const AActor* Attacker, const AActor* Victim);

protected:
	virtual void ConfigureHitContext(struct FVBGameplayEffectContext& HitFacts, const AActor* Target) const override;
	virtual void ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit) override;
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	// 공격 버튼을 놓았다(서버) - 내려찍기 대기를 거둔다.
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

private:
	enum class EPhase : uint8 { Inactive, Chasing, String, FinalStrike, Slamming, Recovering };

	// 서버: 내가 띄운 적이 생겼다 - 쫓을 기회를 연다. 무기에 공중 콤보가 없으면 열지 않는다.
	void OpenOpening(ACharacter* Victim);
	// 쫓을 기회를 닫는 한 곳. 태그, 타이머, 구독을 같이 거두고 이유를 남긴다.
	void ClearOpening(EVBAirOpeningEnd Reason);

	// 서버가 쫓을 대상을 활성화 데이터(Target)에 실어 소유 클라도 같은 대상을 쫓게 한다.
	virtual bool PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const override;

	const UVBAirComboConfig* ResolveAirCombo() const;
	void HandleLaunchedTarget(const FGameplayEventData* Payload);
	UFUNCTION()
	void OnOpeningVictimLanded(const FHitResult& Hit);
	void OnOpeningVictimDeadTag(const FGameplayTag Tag, int32 NewCount);
	void OnChaseVictimDeadTag(const FGameplayTag Tag, int32 NewCount);
	UFUNCTION()
	void OnOpeningVictimEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	void OnOpeningTimeout();
	void UnbindOpeningVictim();

	UFUNCTION()
	void OnTakeoffEnded();
	UFUNCTION()
	void OnTakeoffInterrupted();
	void BeginChase(const UVBAirComboConfig& Config, ACharacter& Attacker, ACharacter& Victim);
	UAbilityTask_ApplyRootMotionMoveToActorForce* CreateChaseMovement(const UVBAirComboConfig& Config, ACharacter& Victim, bool bFollow);
	void StopFollowing();
	void BeginStaging(const UVBAirComboConfig& Config, ACharacter& Attacker);
	UFUNCTION()
	void OnChaseFinished(bool bDestinationReached, bool bTimedOut, FVector FinalTargetLocation);
	void BeginString();
	// 연타 중 누름 - 놓지 않고 HoldToSlamSeconds 가 지나면 내려찍는다.
	UFUNCTION()
	void OnStringPress(FGameplayEventData Payload);
	void OnSlamHoldElapsed();
	void RequestVictimSlam(const UVBAirComboConfig& Config);
	// 쫓는 대상에게 공중 제어 이벤트를 보낸다(서버). 내려찍기는 Magnitude 에 낙하 속도를 싣는다.
	void RequestVictimAirControl(FGameplayTag Event, const UVBAirComboConfig* Config, float Magnitude = 0.0f);
	void ConfirmFinalStrike();
	void OnFinalStrikeConfirmed();
	void BeginSlam();
	UFUNCTION()
	void OnSlamLanded(const FHitResult& Hit);
	void OnAttackerAirExpired();
	void StopAirCombo(const TCHAR* Reason);

	bool bOpeningOpen = false;
	TWeakObjectPtr<ACharacter> OpeningVictim;
	FTimerHandle OpeningTimer;
	FDelegateHandle OpeningDeadTagHandle;
	FDelegateHandle ChaseDeadTagHandle;
	FDelegateHandle LaunchedEventHandle;
	FDelegateHandle FinalStrikeEventHandle;

	// 이번 활성화가 쫓는 대상과 부모에게 넘길 활성화 데이터(도착 때 부모 실행에 쓴다).
	TWeakObjectPtr<ACharacter> ChaseVictim;
	FGameplayEventData DeferredTrigger;
	bool bWeSetFlying = false;
	FTimerHandle AttackerAirTimer;
	TWeakObjectPtr<UAbilityTask_ApplyRootMotionMoveToActorForce> StringFollowTask;
	FVBAirStagingLease StagingLease;

	EPhase Phase = EPhase::Inactive;
	bool bAttackHeld = false;
	FTimerHandle SlamHoldTimer;
};
