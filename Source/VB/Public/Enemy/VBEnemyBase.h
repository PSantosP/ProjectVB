// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Character.h"
#include "Animation/VBAnimationTypes.h" // EVBGait - ApplyGait 인자를 값으로 받는다
#include "Character/VBTargetable.h"
#include "Types/VBTeamTypes.h"
#include "Character/VBDownedImpactReceiver.h"
#include "CharacterCommon/VBDeathSteps.h"
#include "GameplayTagContainer.h" // GetCurrentAttackInputTag 반환형
#include "AbilitySystem/VBAbilitySet.h" // FVBAbilitySetGrantedHandles - 값 멤버
#include "VBEnemyBase.generated.h"

class UVBAbilitySystemComponent;
class UVBHealthAttributeSet;
class UVBHealthComponent;
class UVBCombatAttributeSet;
class UGameplayEffect;
class UVBEnemyConfig;
class UVBHitFlashComponent;
class UVBDeathDissolveComponent;
class UVBRagdollComponent;
class UVBHitPhysicsComponent;
class UVBContextEffectComponent;
class UVBEnemyLookAtComponent;
class UVBEnemyCombatComponent;
class UVBEnemyDeathPresentationComponent;
class UVBEnemyRagdollComponent;
class UVBMeleeTraceComponent;
class UContextualAnimSceneActorComponent;
class UMotionWarpingComponent;

// 적 캐릭터 하나의 조립과 ASC 를 맡는다. 죽음 판정은 UVBHealthComponent, 죽음 재생은 UVBEnemyDeathPresentationComponent,
// 쓰러짐과 시체 물리는 UVBEnemyRagdollComponent 가 한다.
UCLASS(Abstract)
class VB_API AVBEnemyBase : public ACharacter, public IAbilitySystemInterface, public IVBTargetable,
	public IGenericTeamAgentInterface, public IVBDownedImpactReceiver, public IVBDeathSteps
{
	GENERATED_BODY()

public:
	AVBEnemyBase(const FObjectInitializer& ObjectInitializer);

	// 적 설정. 미할당이면 클래스 기본값이고 null 이 아니다. AI 컨트롤러가 시야 값을 읽는다.
	const UVBEnemyConfig* GetEnemyConfig() const;
	// 전투 조율 컴포넌트. 적 코드 안에서는 늘 있다.
	UVBEnemyCombatComponent* GetCombatComponent() const { return CombatComponent; }
	// 죽었으면 true 다.
	bool IsDead() const;

	// 걸음을 바꾼다. 설정의 Walk/RunSpeed 를 MaxWalkSpeed 에 쓰는 한 곳이고 바뀔 때만 로그를 남긴다.
	void ApplyGait(EVBGait NewGait);
	// 지금 걸음. 같은 값을 다시 쓰는지 가리려고 보관한다.
	EVBGait GetGait() const { return CurrentGait; }

	// 지금 쓸 기본 공격 GA 의 InputTag. 기본은 설정 값이고 보스는 페이즈별로 덮는다.
	virtual FGameplayTag GetCurrentAttackInputTag() const;
	// 토큰을 받아 공격을 계획하기 직전에 한 번 부른다(서버).
	virtual void ChooseNextAttack() {}
	// 계획이 비워질 때 지난 계획의 선택을 버린다.
	virtual void ForgetChosenAttack() {}
	// 공격이 실제로 시작될 때 부른다(서버). 고른 공격의 쿨다운은 여기서 시작한다.
	virtual void NotifyAttackStarted() {}

	// 가산 플린치 몽타주를 모든 컴퓨터에서 지금 동작 위에 얹는다. ASC 몽타주 복제를 못 타서 따로 보낸다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayAdditiveFlinch(UAnimMontage* Montage);

	// 저작 진입점. VBEnemyAuthoring 이 기존 dissolve 실행기로 전달한다.
	UFUNCTION(CallInEditor, Category="VB 사망 디졸브 시험")
	void PreviewDeathDissolve();

	// 시험으로 깎인 메시를 원래대로 돌린다. 길이 0 이 머티리얼의 꺼짐 게이트다.
	UFUNCTION(CallInEditor, Category="VB 사망 디졸브 시험")
	void ResetDeathDissolvePreview();

	//~ Begin IGenericTeamAgentInterface
	// 소속은 적 종류별 설정이 갖는다(같은 세력끼리는 서로 지각하지 않는다).
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin IVBTargetable
	virtual bool IsTargetable() const override;
	// 락온 카메라가 조준할 가슴 지점. 본이 없으면 actor+Z 이고 ZeroVector 는 돌려주지 않는다.
	virtual FVector GetTargetMarkerLocation() const override;
	// UVBEnemyConfig::EnemyTags. 처치 이벤트가 싣는 것과 같은 정체다.
	virtual FGameplayTagContainer GetTargetIdentityTags() const override;
	//~ End IVBTargetable

	//~ Begin IVBDownedImpactReceiver - 처형을 버텨내고 물리로 쓰러져 있는 캐릭터. 서버에서만 불린다. UVBEnemyRagdollComponent 에 위임한다.
	virtual bool IsBodyDownedInPhysics() const override;
	virtual float ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale) override;
	//~ End IVBDownedImpactReceiver

#if WITH_EDITOR
	// 애님 클래스와 설정의 화면 밖 정책을 짝으로 검증한다.
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 시작 능력, 초기 스탯 효과. BeginPlay 에 서버가 한 번 준다(SourceObject = 이 적). 같은 목록을 가진 적 BP 는 세트 하나를 같이 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|GAS")
	TObjectPtr<const UVBAbilitySet> AbilitySet;

	// 락온 카메라가 조준할 마커 본/소켓(가슴 높이). 스켈레톤별 자동 스케일 - UE 마네킹 기본 상체 본.
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn")
	FName MarkerBoneName = FName("spine_03");

	// 위 본/소켓 부재 시 폴백: 캡슐 중심(GetActorLocation) 기준 Z 상승(cm). 가슴 높이대(+40~60).
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn")
	float MarkerFallbackHeight = 50.0f;

protected:
	virtual void BeginPlay() override;
	// 적 목록(UVBEnemyRegistrySubsystem)에서 내린다. 올리는 곳은 BeginPlay.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ASC (Character 직접 소유)
	UPROPERTY(VisibleAnywhere, Category="VB|GAS")
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;

	// AttributeSet (ASC가 라이프타임 관리, GC-only - exposure 키워드 없이 UPROPERTY())
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthAttributeSet;

	UPROPERTY(VisibleAnywhere, Category="VB|Death")
	TObjectPtr<UVBHealthComponent> HealthComponent;

	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> CombatAttributeSet;

	// 피격 플래시 상태(큐가 찾아 위임한다). C++ 생성 - 적 BP 마다 배선이 빠질 수 있다.
	UPROPERTY(VisibleAnywhere, Category="VB|GameFeel")
	TObjectPtr<UVBHitFlashComponent> HitFlashComponent;

	// 시체가 사라지는 연출의 상태(같은 이유로 C++ 생성).
	UPROPERTY(VisibleAnywhere, Category="VB|GameFeel")
	TObjectPtr<UVBDeathDissolveComponent> DeathDissolveComponent;

	UPROPERTY(VisibleAnywhere, Category="VB|Animation")
	TObjectPtr<UVBEnemyLookAtComponent> LookAtComponent;

	UPROPERTY(VisibleAnywhere, Category="VB|Death")
	TObjectPtr<UVBRagdollComponent> RagdollComponent;

	// 쓰러짐, 시체 물리, 캡슐 따라가기(같은 이유로 C++ 생성). 뼈 물리 제어는 위 RagdollComponent 를 쓴다.
	UPROPERTY(VisibleAnywhere, Category="VB|Death")
	TObjectPtr<UVBEnemyRagdollComponent> EnemyRagdollComponent;

	// 죽음 복제 스냅샷과 재생(같은 이유로 C++ 생성).
	UPROPERTY(VisibleAnywhere, Category="VB|Death")
	TObjectPtr<UVBEnemyDeathPresentationComponent> DeathPresentationComponent;

	// 서 있을 때의 물리 피격 층. 래그돌이 메시 제어를 가져가는 자리마다 이것을 먼저 끈다(StopAll) - 한 메시의 물리 제어 주체는 하나다.
	UPROPERTY(VisibleAnywhere, Category="VB|GameFeel")
	TObjectPtr<UVBHitPhysicsComponent> HitPhysicsComponent;
	// 애니 순간 이펙트(발 딛음, 땅 내려치기, 쓰러져 바닥에 닿음). 적마다 표를 BP 에서 준다 - 트롤 전용 표를 공용 표 앞에 둔다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TObjectPtr<UVBContextEffectComponent> ContextEffectComponent;

	// 그룹 전투 조율(역할, 토큰, 슬롯). 같은 이유로 C++ 생성.
	UPROPERTY(VisibleAnywhere, Category="VB|AI")
	TObjectPtr<UVBEnemyCombatComponent> CombatComponent;

	// 무기 궤적 적중 판정(C++ 생성). 쓸 날은 BP 의 ShapeSet - 비면 공격 GA 가 옛 판정으로 대신하며 경고한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Combat")
	TObjectPtr<UVBMeleeTraceComponent> MeleeTraceComponent;

	// 처형 장면의 피해자 자리. 생성자에서 단다 - 처형 순간에 붙이면 원격 클라에서 도착 순서가 갈려 피해자만 장면에 안 든다.
	UPROPERTY(VisibleAnywhere, Category="VB|Finisher")
	TObjectPtr<UContextualAnimSceneActorComponent> FinisherSceneComponent;

	// 공격 몽타주의 SkewWarp 가 쓰는 자리(C++ 생성 - 빠지면 워프 구간이 생 루트모션으로 돈다).
	UPROPERTY(VisibleAnywhere, Category="VB|Animation")
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;

	// 일반 사망 시 명성 변동 GE(BP 에서 설정).
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	TSubclassOf<UGameplayEffect> DefaultDeathReputationEffect;

	// 이동/사망 튜닝값 DataAsset. 미할당 시 UVBEnemyConfig CDO 디폴트가 fallback (GetEnemyConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Config")
	TObjectPtr<UVBEnemyConfig> EnemyConfig;

	//~ Begin IVBDeathSteps
	virtual void BeforeDeathTag(const FVBDeathFacts& Facts) override;
	virtual void AfterDeathTag(const FVBDeathFacts& Facts) override;
	virtual void PresentDeath(const FVBDeathFacts& Facts) override;
	//~ End IVBDeathSteps

private:
	// 시작 세트가 준 것의 영수증. 액터 수명이라 거두지 않는다.
	FVBAbilitySetGrantedHandles AbilitySetGrant;

	// 마지막으로 적용한 걸음. BeginPlay 가 Walk 로 세운다.
	EVBGait CurrentGait = EVBGait::Walk;
};
