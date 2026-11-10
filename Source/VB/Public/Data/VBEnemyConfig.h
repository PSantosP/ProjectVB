// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkinnedMeshComponent.h" // AnimTickOption - EVisibilityBasedAnimTickOption 을 값으로 보관
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h" // AttackAbilityInputTag - FGameplayTag 를 값으로 보관
#include "StructUtils/InstancedStruct.h" // OnKilledEffects
#include "Types/VBTeamTypes.h"
#include "VBEnemyConfig.generated.h"

class UAnimMontage;
class UVBEnemyAnimSet;
class UVBDeathDissolveConfig;
class UVBRagdollConfig;
class UVBHitPhysicsConfig;
class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * 적 아키타입 튜닝 DataAsset. 이동, 사망, AI 시야, 전투 조율 수치를 여러 적 BP 가 공유한다.
 * 미할당이면 이 클래스의 C++ 디폴트(CDO)가 fallback 이다.
 */
UCLASS(BlueprintType)
class VB_API UVBEnemyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 소속 세력
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Team")
	EVBTeam Team = EVBTeam::Infected;

	// 락온 대상 바와 보스 바에 보이는 이름. 비면 이름 없이 바만 뜬다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|UI")
	FText DisplayName;

	// 이 적의 종류(Enemy.Soldier ...). 처치 사건의 대상 태그로 실려 퀘스트 처치 목표가 질의한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest", meta=(Categories="Enemy"))
	FGameplayTagContainer EnemyTags;

	// 플레이어가 죽였을 때 서버에서 적용할 효과(드롭, 사실 켜기). 부른 쪽 = 죽인 플레이어, 상대 = 죽은 적.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Quest",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnKilledEffects;

	// 걸을 때 속도(cm/s). WalkSpeed < RunSpeed, RunSpeed 는 AnimSet 로코모션 Speed 축 최대 이하여야 한다(IsDataValid)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement", meta=(ClampMin="0.0"))
	float WalkSpeed = 150.0f;

	// 뛸 때 속도(cm/s)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement", meta=(ClampMin="0.0"))
	float RunSpeed = 300.0f;

	// 목표까지 남은 거리가 이 값을 넘으면 뛴다(cm). 진입/이탈 쌍이라 경계에서 걸음이 뒤집히지 않는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement", meta=(ClampMin="0.0"))
	float RunEnterDistance = 250.0f;

	// 이 거리 안으로 들어오면 걷는다(cm). RunEnterDistance 보다 작아야 한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement", meta=(ClampMin="0.0"))
	float RunExitDistance = 180.0f;

	// 시야 끝에서 사거리까지 다가오는 이상화된 시간(초). IsDataValid 와 테스트가 같은 식을 쓴다
	float GetNormalApproachSeconds() const;

	// 회전 속도 (CMC RotationRate, AI 회전 경로)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	FRotator RotationRate = FRotator(0.0f, 360.0f, 0.0f);

	// 로코모션 클립 묶음. ABP_Enemy_Base 를 쓰는 적은 필수(비면 참조 포즈로 선다), 그 외는 비어도 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UVBEnemyAnimSet> AnimSet;

	// 화면 밖 애니메이션 정책. OnlyTickMontagesAndRefreshBonesWhenPlayingMontages 는 연결 층이 있으면 UE 5.8 에서 터져 FVBMeshRefresh 가 내린다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation|Performance")
	EVisibilityBasedAnimTickOption AnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;

	// 애니메이션 갱신 빈도 줄이기(URO)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation|Performance")
	bool bAnimUpdateRateOptimizations = true;

	// 일반 사망 시 Instigator 명성 변동량 (음수 = 명성 하락)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	float DeathReputationChange = -5.0f;

	// 사망(래그돌) 후 액터 제거까지 대기 시간(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	float DeathCleanupDelay = 5.0f;

	// 사망 연출 몽타주 후보. 비면 죽는 순간 래그돌, 있으면 하나를 재생한 뒤 래그돌로 전환한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	TArray<TObjectPtr<UAnimMontage>> DeathMontages;

	// 엎드린 채 죽을 때의 연출. 비면 엎드린 자세 그대로 래그돌로 전환한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	TObjectPtr<UAnimMontage> DownedDeathMontage;

	// 이번 죽음에 틀 몽타주. 엎드렸으면 DownedDeathMontage, 아니면 DeathMontages 중 무작위 하나, 없으면 nullptr
	UAnimMontage* PickDeathMontage(bool bWasDowned) const;

	// 래그돌을 이 뼈와 그 아래에만 건다. None = 바디 전부. 골격에 없는 뼈면 경고 후 전부 건다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	FName RagdollRootBone;

	// 몰아지는 래그돌을 쓰는가(물리 바디가 앞장서고 캡슐이 스윕으로 쫓는다). 끄면 캡슐이 죽은 자리에 남는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	bool bUseDrivenRagdoll = true;

	// 골격과 물리 자산을 검증한 GASP powered ragdoll 설정. 비어 있으면 passive 물리만 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	TObjectPtr<UVBRagdollConfig> PoweredRagdoll;

	// 래그돌이 쓰러지던 운동량을 이어받는가. 끄면 진입 때 속도를 0 으로 지운다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	bool bPreserveRagdollMomentum = true;

	// 캡슐이 쫓아갈 뼈. 비면 RagdollRootBone, 그것도 비면 "pelvis".
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	FName DrivenRagdollFollowBone;

	// 몰아지는 래그돌을 끝낼 속도 문턱(cm/s). 모든 바디 최고 속도가 SettleFrames 만큼 연속으로 이 아래면 보통 래그돌로 전환한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death", meta=(ClampMin="0.0"))
	float DrivenRagdollSettleSpeed = 40.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death", meta=(ClampMin="1"))
	int32 DrivenRagdollSettleFrames = 10;

	// 몰아지는 래그돌의 상한(초). 경사에서 계속 구르는 시체도 끝나게 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death", meta=(ClampMin="0.1"))
	float DrivenRagdollMaxSeconds = 4.0f;

	// 캡슐 회전도 메시를 따라가는가. 캡슐 회전을 제어하는 쪽이 이미 여럿이라 기본은 끔.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	bool bDrivenRagdollFollowsRotation = false;

	// 시체 디졸브 연출 묶음. 비면 정리 지연이 끝날 때 그냥 없어진다. DeathCleanupDelay 안에 들어가야 한다(IsDataValid)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Death")
	TObjectPtr<UVBDeathDissolveConfig> DeathDissolve;

	// 서 있을 때 맞으면 상체가 잠깐 출렁이는 물리 피격 층(UVBHitPhysicsComponent). 비어 있으면 그 층은 꺼진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBHitPhysicsConfig> HitPhysics;

	// 이 적을 처형할 수 있나. false 면 빈사가 되어도 처형 대상에서 빠진다(UVBAbilitySystemStatics::FindFinisherTarget)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	bool bAllowFinisher = true;

	// 적을 인지하는 최대 거리(cm). AI 시야 값은 AVBAIController 가 OnPossess 에서 소비한다(생성자 시점엔 자산 값이 없다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Perception")
	float SightRadius = 2000.0f;

	// 이미 본 대상을 놓치는 거리(cm). SightRadius 보다 커야 한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Perception")
	float LoseSightRadius = 2500.0f;

	// 시야 반각(도). 60 이면 실제 시야는 120도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Perception")
	float PeripheralVisionAngleDegrees = 60.0f;

	// 시야에서 사라진 자극이 만료되기까지의 시간(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Perception")
	float SightMaxAge = 5.0f;

	// 공격 한 번에 소비하는 토큰 코스트. 대형몹에 2 이상을 준다(토큰 총량, 슬롯 수는 UVBCombatSettings 가 홈이다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="1"))
	int32 AttackTokenCost = 1;

	// 이 거리 안으로 들어오면 접근을 멈추고 공격을 시작한다(cm). 공격 자산의 AttackRange 보다 짧아야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0"))
	float AttackCommitDistance = 180.0f;

	// 이 거리 밖으로 벗어나야 공격 역할을 놓는다(cm). AttackCommitDistance 보다 커야 한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0"))
	float AttackReleaseDistance = 200.0f;

	// 토큰이 없을 때 물러나 대기하는 거리(cm). 대기 거리 = AttackCommitDistance + 이 값. Release - Commit 보다 커야 한다(IsDataValid)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0"))
	float WaitingStandoff = 70.0f;

	// 공격 GA 활성화 후 스윙까지의 예고 시간(초). 0 이면 즉시 스윙. 플레이어 회피 창이라 난이도의 1차 노브다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0"))
	float TelegraphDuration = 0.6f;

	// 이 적의 공격이 끝난 뒤 그룹의 다음 공격자가 나서기까지의 숨(초). 0 = 숨 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0", ClampMax="10.0"))
	float PostAttackPauseSeconds = 1.2f;

	// 숨의 무작위 폭(초, +-). 서버에서만 뽑는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.0", ClampMax="5.0"))
	float PostAttackPauseVariance = 0.4f;

	// 예고 중 재생할 몽타주(선택). 미할당이면 제자리 정지가 예고다. 예고 시간은 클립 길이를 따르지 않는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat")
	TObjectPtr<UAnimMontage> TelegraphMontage = nullptr;

	// 이 아키타입 공격 GA 의 InputTag. BT 노드의 AbilityTag 가 비어 있을 때 읽는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(Categories="Input"))
	FGameplayTag AttackAbilityInputTag;

	// 슈퍼아머 반격 GA 의 InputTag. 비면 반격 없음. 아머, 예고 등은 그 GA 의 공격 자산이 든다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(Categories="Input"))
	FGameplayTag ArmoredCounterAttackInputTag;

	// 창 안에서 이만큼 타격을 맞으면 다음 공격이 반격이 된다. 0 = 끔. 같은 타의 여러 판정은 하나로 세고 슈퍼아머 중 피격은 안 센다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0"))
	int32 ArmoredCounterAfterHits = 0;

	// 맞은 횟수를 세는 창(초). 공격이 시작되면 횟수를 비운다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.1", ClampMax="10.0"))
	float ArmoredCounterWindowSeconds = 2.0f;

	// 문턱을 넘은 반격을 다음 공격 시작까지 대기시키는 상한(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.1", ClampMax="10.0"))
	float ArmoredCounterPendingSeconds = 3.0f;

	// 토큰을 보유한 채 접근만 하는 시간의 상한(초). 초과하면 회수해 다른 멤버에게 준다. GetNormalApproachSeconds() 의 1.25배보다 커야 한다(IsDataValid)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="0.5"))
	float MaxApproachHoldSeconds = 8.0f;

	// 접근 워프가 클립 전진을 늘이거나 줄여도 되는 배수. 1.0 = 이동을 안 바꿈, 1 에서 멀수록 발이 미끄러진다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|AI|Combat", meta=(ClampMin="1.0", ClampMax="4.0"))
	float WarpLungeToleranceRatio = 1.5f;

#if WITH_EDITOR
	// 저장 시점에 자산 내부 계약을 검사한다. 위반해도 런타임 로그가 안 남는 부류다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

private:
	// 공격 태그, 애님 묶음, 공격과 대기 거리의 짝을 본다.
	void ValidateAttackRole(FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 걷기와 뛰기 속도, 걸음 전환 거리의 순서를 본다.
	void ValidateGaitSpeeds(FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 속도가 로코모션 블렌드스페이스의 축과 샘플에 맞는지 본다.
	void ValidateLocomotionClips(FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 접근 상한이 정상 접근 시간을 담는지 본다.
	void ValidateApproachBudget(FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 사망 몽타주와 디졸브가 정리 지연 안에 드는지 본다.
	void ValidateDeathTiming(FDataValidationContext& Context, EDataValidationResult& Result) const;
#endif
};
