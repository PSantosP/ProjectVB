// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBSwingCombatProfile.h" // FVBSwingCombatScales 를 값으로 반환
#include "Types/VBWeaponStateTypes.h" // EVBWeaponType 를 값으로 반환(GetGrantingWeaponType)
#include "AbilitySystem/Abilities/VBComboRuntime.h" // FVBComboRuntime - 콤보 상태를 값으로 든다
#include "AbilitySystem/Abilities/VBMeleeActivationMemory.h" // FVBMeleeActivationMemory - 공격 사이 기억을 값으로 든다
#include "AbilitySystem/Abilities/VBSwingHitContext.h" // FVBSwingHitContext - 열린 판정 창이 값으로 든다
#include "AbilitySystem/Abilities/VBSwingTargetBinding.h" // FVBSwingTargetBinding - 한 타가 잡은 대상의 구독을 값으로 든다
#include "AbilitySystem/Abilities/VBApproachWarpDriver.h" // FVBApproachWarpDriver - 접근 워프를 값으로 든다
#include "VBGA_MeleeAttackBase.generated.h"

class UAbilityTask_WaitGameplayEvent;
class UVBWeaponDefinition;
class UVBSlideMovementComponent;
class UVBSwingImpactProfile;
class UVBMeleeTraceComponent;
class UMotionWarpingComponent;
struct FVBAttackActivationData;

/** 근접 공격 한 번의 순서(준비, 설정 해석, 재생), 판정 창 연결, 종료 콜백, 서버 피해 적용의 단일 경로를 맡는 기초 능력이다.
 *  단계별 도우미: FVBMeleeActivationMemory, FVBSwingTargetBinding, FVBApproachWarpDriver, FVBComboRuntime, VBMeleeHitApplication.
 *  ServerInitiated - 소유 클라도 몽타주를 로컬 재생한다. */
UCLASS(Abstract)
class VB_API UVBGA_MeleeAttackBase : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_MeleeAttackBase();

	// 무기 정의에서 이 공격이 쓰는 잎. 기본은 nullptr(적, 무기 무관 공격은 AttackConfig)
	virtual UVBAttackConfig* SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const { return nullptr; }

	// 한 타의 번호. 활성화와 타 전환마다 1 늘어난다
	int32 GetSwingSerial() const { return SwingSerial; }

	// 이 활성화가 쓸 공격 설정을 미리 본다. 잎이 없으면 AttackConfig. 적 전투 컴포넌트가 토큰 부여 때 읽으므로 public
	const UVBAttackConfig* PeekAttackConfig() const;

protected:
	// 활성 공격이 받은 입력을 자신의 콤보 사건으로 전달한다.
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;

	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UVBAttackConfig> AttackConfig;

	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 슈퍼아머 태그를 지우는 한 곳. 몽타주 종료 세 길이 모두 여기로 온다
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// 현재 공격이 겨누는 대상(하드락 우선, 없으면 소프트 런지). 워프와 판정이 같은 대상을 본다
	const USceneComponent* ResolveAimTargetComponent() const;
	// 어느 대상을 언제 잡나만 정한다. 묶은 내용은 SwingTarget 이 기억한다
	void CaptureSwingTarget();
	void OnSwingTargetDeath(const FGameplayTag Tag, int32 NewCount);
	UFUNCTION()
	void OnSwingTargetDestroyed(AActor* DestroyedActor);
	FVBSwingTargetBinding SwingTarget;

	// 정면 구 스윕 한 번(서버). 한 프레임 노티, 몽타주 없음, 판정 창을 못 열었을 때 쓴다
	void PerformAttackTrace();

	// 이번 스윙의 임팩트 쉐이크 프로필(노티, 없으면 공격 잎). 둘 다 비면 nullptr
	const UVBSwingImpactProfile* ResolveSwingImpactProfile() const;

	// 이번 스윙의 전투 수치 배율(노티 프로필, 없으면 전부 1.0)
	FVBSwingCombatScales ResolveSwingCombatScales() const;

	// 적중 시 추가 효과(ActiveConfig->OnHitExtraEffects). 파생은 Super 뒤에 더한다
	virtual void ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit);
	// 피해 적용 전에 능력별 타격 사실을 구성한다
	virtual void ConfigureHitContext(struct FVBGameplayEffectContext& HitFacts, const AActor* Target) const;

	// 즉시 trace(몽타주 없음, 한 프레임 notify, 못 연 창) 허용 여부. 피해가 자기 창에서만 나야 하면 끈다(UVBGA_CinematicSkill)
	virtual bool AllowsImmediateTraceFallback() const { return true; }
	bool IsFinalComboStrike() const;
	// 한 trace 창의 적중을 readout, 피해보다 먼저 거른다(서버). 기본은 모두 남긴다
	virtual void FilterWindowHits(int32 WindowKey, TArray<FHitResult>& InOutHits) {}

	// 스택형 추가효과가 한도에 닿았을 때의 폭발(AttackConfig.StackFullEffects). 엔진 OverflowEffects 는 쓰지 않는다
	void ApplyStackFullEffects(UAbilitySystemComponent* TargetASC, TSubclassOf<UGameplayEffect> StackingEffect);

	// 공격 판정 노티 이벤트 -> 판정
	UFUNCTION()
	void OnAttackTraceEvent(FGameplayEventData PayLoad);

	// 판정 창 닫힘(서버). 페이로드의 모양 태그로 어느 창인지 가른다
	UFUNCTION()
	void OnAttackTraceEndEvent(FGameplayEventData PayLoad);

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageBlendOut();

	// 다른 몽타주에 의해 중단됨
	UFUNCTION()
	void OnMontageInterrupted();

	// GA 취소로 중단됨
	UFUNCTION()
	void OnMontageCancelled();

	// bCapture = 이 타의 대상을 새로 잡는다. false = 같은 대상으로 워프만 다시 세운다
	void SetWarpTargetLockedEnemy(bool bCapture = true);
	void ClearWarpTarget();
	// 대상 때문에 워프를 끊는 한 길. 이유를 로그에 남긴다
	void DropWarpTarget(EVBWarpDropReason Reason);
	FVBApproachWarpDriver ApproachWarp;

	// 락온 중 방향 공격 전환(서버, 타 시작에 한 번). 입력 방향이 다른 적을 분명히 가리키면 락온을 바꾼다
	void ApplyDirectionalSwitch(const FVector2D& WorldIntent, float StepMaxDistance);

	// 방향 전환을 받는 공격인가. 반격은 끈다(패링이 이미 공격자에게 락을 넘겼다)
	bool bAllowDirectionalLockSwitch = true;

	// 워프 창마다 목표를 다시 세운다(엔진 OnPreUpdate). 한 타에 워프 창이 둘인 몽타주의 둘째 창용
	UFUNCTION()
	void OnWarpPreUpdate(UMotionWarpingComponent* MotionWarpingComp);
	void BindWarpRearm();
	void UnbindWarpRearm();
	void RearmWarpAtWindowStart();
	// 멈추는 거리에 닿은 목표를 붙잡고 되돌리는 것은 드라이버, 전환 로그와 끊기는 여기가 한다
	void HoldSpentApproachSync();
	float GetWarpSyncMinLead() const;

	// 워프 목적지를 쓰는 한 곳(락, 소프트 런지 공용). 오프셋 clamp 후 대상 컴포넌트를 따라간다
	void ApplyApproachWarpToTarget(const USceneComponent* TargetComponent);

	// 오프셋을 밖에서 정해 세우는 판(적 GA). bFollowTarget=false 면 지금 한 점에 고정한다
	void ApplyApproachWarpWithOffset(const USceneComponent* TargetComponent, float ApproachOffset, bool bFollowTarget = true);
	// 거리 밖에서 고른 첫 타: 표적을 보되 클립의 자연 전진만큼만 간다
	void ApplyNaturalReachWarp(const USceneComponent* TargetComponent);

	// 접근 워프의 멈추는 거리(cm). 변형 선택도 쓴다. ActiveConfig 해석 뒤에만 뜻이 있다
	float ComputeApproachOffset(const USceneComponent* TargetComponent) const;

	// 같은 clamp 사슬을 주어진 기본 멈춤에서 시작한다(적 GA 가 변형을 고르기 전에 쓴다)
	float ComputeApproachOffset(const USceneComponent* TargetComponent, float BaseOffset) const;

	// 이번 스윙의 변형 번호(기본은 순환 커서). 적 GA 는 거리에 맞는 변형으로 덮는다(서버 전용)
	virtual int32 SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount);

	const UAnimMontage* GetActiveMontage() const { return ActiveMontage; }

	// 락온 대상, 없으면 소프트 런지 대상(상태를 바꾸지 않는다)
	AActor* FindSwingTargetCandidate() const;
	// 시작 콤보를 거리 자격 안에서 골랐나(준비 없는 활성화는 true). 첫 타 접근 워프가 읽는다
	bool bStartTargetInReach = true;

	// 이 능력을 준 무기 정의의 잎. 무기가 주지 않았으면 nullptr, 줬는데 잎이 없으면 Error 와 nullptr
	UVBAttackConfig* ResolveWeaponLeaf() const;
	const UVBWeaponDefinition* GetGrantingWeapon() const;

	// 슈퍼아머 태그를 한 번만 건다(따로 걸면 카운트가 2 가 된다). 제거는 EndAbility 한 곳
	void GrantSuperArmorOnce(const UVBAttackConfig* Config);

private:
	// 아바타의 이동 컴포넌트(공격을 끊을 때 남은 이동을 누른다). 없으면 null
	UVBSlideMovementComponent* GetAvatarMovement() const;

	// false = 이 스윙은 피해를 낼 수 없다(아바타, 설정, 피해 GE, ASC 중 하나가 없음)
	bool BuildSwingHitContext(FVBSwingHitContext& OutContext) const;
	// 새로 맞은 표적에 피해, 반응, 큐를 적용하고, 첫 접촉이면 히트스톱, 쉐이크를 낸다
	void ApplySwingHits(const TArray<FHitResult>& Hits, FVBSwingHitContext& Context);

	// 표적 하나의 피해 스펙을 만든다. 무효 핸들이면 이 표적을 건너뛴다
	FGameplayEffectSpecHandle MakeHitSpec(const FHitResult& Hit, const FVBSwingHitContext& Context) const;

	// 활성화마다 지난 공격의 변형, 콤보, 프로필 값을 비우고 잎을 GA 기본값으로 되돌린다.
	void ResetActivationState();

	// 이번 공격의 잎과 변형을 정한다. 준비된 공격을 실행할 수 없으면 false 다.
	bool ResolveActiveAttack(const FVBAttackActivationData* Prepared);

	// 고른 잎과 변형을 활성 상태에 옮기고 해석 결과를 로그로 남긴다.
	void ApplyAttackLeaf(UVBAttackConfig* Leaf, int32 ChosenVariantIndex);

	// 몽타주가 없는 공격을 끝낸다. 허용되면 즉시 Trace 를 한 번 한다.
	void EndWithoutMontage(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bIsServer);

	// 회피가 끊은 콤보를 이을 시작 섹션을 돌려주고 재개 기록을 버린다. 이을 것이 없으면 NAME_None 이다.
	FName ResolveDodgeResumeSection(const FVBAttackActivationData* Prepared);

	// 첫 타의 판정 상태를 비우고 방향 전환, 워프 대상 잡기를 끝낸다.
	void PrepareFirstSwing(const FVector2D& PressIntent, bool bPrepared, bool bIsServer);

	// 첫 타의 방향 전환(서버). 대상을 잡기 전에 불러야 뒤의 잡기가 새 락온 대상을 잡는다.
	void ApplyFirstHitDirectionalSwitch(const FVector2D& PressIntent, bool bPrepared);

	// 활성 몽타주를 시작 섹션부터 틀고 끝남 델리게이트를 묶는다.
	void PlayAttackMontage(FName StartSection);

	// 자기 자신의 이벤트를 반복 수신하는 대기 Task 를 만든다.
	UAbilityTask_WaitGameplayEvent* WaitRepeatedEvent(const FGameplayTag& EventTag);

	// 판정과 콤보 창 이벤트 대기를 건다.
	void ListenForAttackEvents(bool bIsServer);

	UVBMeleeTraceComponent* GetMeleeTraceComponent() const;
	// 창을 연다(서버). 못 열면 경고하고 옛 구 스윕으로 대신한다.
	void BeginTraceWindow(const FGameplayEventData& Payload);
	void OnTraceWindowHits(const TArray<FHitResult>& Hits, int32 Key);
	// 같은 모양 묶음(Shapes)의 가장 먼저 연 창 하나를 닫는다.
	void EndTraceWindowByShapes(const FGameplayTagContainer& Shapes, bool bSweepFinalPose);
	// 이 GA 가 연 창을 전부 닫는다(섹션 전환, EndAbility).
	void EndAllTraceWindows();

	// 조기 전환 시도: 저장 입력, 창 열림, 닿음, 여운이 다 맞으면 넘어간다.
	UFUNCTION()
	void TryChainAfterHit();
	// 창 안 이동 끊기의 한 번 검사.
	void TickWindowMoveCut();
	// 저장된 입력이 있으면 다음 섹션으로 넘어간다. 반환 = 넘어갔나.
	bool TryChainToNextSection();
	// 다음 타 없이 창이 닫혔을 때: 워프를 거두고 몽타주를 짧게 멈춘 뒤 끝낸다(Reason 은 로그용).
	void CancelRecovery(const TCHAR* Reason = TEXT("창 닫힘"));
	// 이 능력을 준 무기(스펙의 SourceObject). 무기 세트가 주지 않았으면(적) None.
	EVBWeaponType GetGrantingWeaponType() const;

	// 활성화 전 준비(서버): 콤보 선택 정책이 있으면 새 콤보를 골라 이벤트 데이터(FVBAttackActivationData)에 싣는다.
	virtual bool PrepareActivation(const FVBActivationRequest& Request, FGameplayEventData& OutPayload) const override;

	// 콤보 창 열림 노티: Combo 에 알리고 전환과 이동 끊기 검사를 시도한다.
	UFUNCTION()
	void OnComboWindowOpen(FGameplayEventData Payload);

	// 콤보 창 닫힘 노티: 저장 입력이 있으면 전환하고, 없으면 설정에 따라 회복을 끊는다.
	UFUNCTION()
	void OnComboWindowClose(FGameplayEventData Payload);

	// 공격 입력: 이 누름의 방향과 시각을 Combo 에 기록한다.
	UFUNCTION()
	void OnComboInputReceived(FGameplayEventData Payload);

	// 현재 활성화의 무브셋(무기 타입으로 해석). 없으면 AttackConfig/AnimMontage
	UPROPERTY()
	TObjectPtr<UVBAttackConfig> ActiveConfig;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;

	// 직전 판정 노티가 실어 보낸 임팩트 쉐이크 프로필(노티 단위 수명). 이벤트 데이터가 const 로 줘서 const 다
	UPROPERTY()
	TObjectPtr<const UVBSwingImpactProfile> ActiveSwingProfile;

	// 직전 노티의 전투 수치 보정 프로필(노티 단위). 활성화 시작에서 반드시 리셋한다(빠뜨리면 다음 활성화로 샌다)
	UPROPERTY()
	TObjectPtr<const UVBSwingCombatProfile> ActiveSwingCombatProfile;

	FVBComboRuntime Combo;

	// GetSwingSerial 의 값. 활성화가 바뀌어도 리셋하지 않는다
	int32 SwingSerial = 0;

	// 공격 사이 기억(변형 커서, 시작 콤보, 회피 재개). 활성화를 비울 때 지우지 않는다
	FVBMeleeActivationMemory ActivationMemory;

	// 이번 변형의 재생배율(활성화마다 1.0)
	float ActiveVariantRate = 1.0f;

	// 이번 변형의 데미지 배율(활성화마다 1.0)
	float ActiveVariantDamage = 1.0f;

	// 이번 변형의 멈추는 거리. 음수 = 아직 해석 전
	float ActiveVariantApproachOffset = -1.0f;

	// 이번 변형의 콤보 타별 멈추는 거리 사본(비면 공통 값)
	TArray<float> ActiveVariantComboOffsets;

	// 이 GA 가 연 판정 창. 늦은 콜백은 Key 로 제 문맥을 못 찾아 버려진다
	struct FOpenTraceWindow
	{
		int32 Key = INDEX_NONE;
		int32 WindowId = INDEX_NONE; // UVBMeleeTraceComponent 쪽 번호
		FGameplayTagContainer Shapes;
		FVBSwingHitContext Context;
	};
	TArray<FOpenTraceWindow> OpenTraceWindows;
	int32 NextTraceWindowKey = 1;

	// 이 공격이 건 슈퍼아머 태그(없으면 무효). EndAbility 가 건 경우에만 지운다
	FGameplayTag GrantedSuperArmorTag;

	// 활성화마다 1 는다. 끊긴 스윙과 새 스윙을 가른다
	uint32 ActivationSerial = 0;
};
