// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"

#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"
#include "Animation/VBAnimationTypes.h"
#include "Character/VBGuardable.h"     // IVBGuardable
#include "Types/VBTeamTypes.h"     // IGenericTeamAgentInterface + EVBTeam
#include "Character/VBJumpCrouchInput.h"
#include "Character/VBRecoveryCancelTrace.h"
#include "CharacterCommon/VBDeathSteps.h"
#include "VBCharacter.generated.h"



class UGameplayCameraComponent;
class UVBAbilitySystemComponent;
class UVBTargetLockComponent;
class UMotionWarpingComponent;
class UVBWeaponStateComponent;
class UVBGuardComponent;
class UVBTraversalComponent;
class UVBPawnExtensionComponent;
class UVBHealthComponent;
class UVBHeroComponent;
class UVBLocomotionIntentComponent;
class UVBInteractorComponent;

class UAnimMontage;
class UAnimSequence;
class UVBMovementConfig;
class UVBParryConfig;
class UVBPlayerRagdollComponent;
class UVBRagdollComponent;
class UVBContextEffectComponent;
class UVBLookAtPOIComponent;
class UVBPlayerAttentionComponent;
class UVBGASPSmartObjectComponent;
// 수집 함수의 반환형만 쓰므로 앞쪽 선언(정의는 CharacterCommon/VBBodyControlPolicy.h).
namespace VBBodyControlPolicy
{
	struct FCapsuleRotationFacts;
	struct FBodyControlSources;
}

// 플레이어 캐릭터(Lyra ALyraCharacter 역할, AVBGASPNPC 도 상속): 두 클래스가 함께 쓰는 컴포넌트, 팀, 입력 의도, 미끄럼 상태, 사망 자세.
// 기능 상태는 각 컴포넌트가 갖고 FindFor 로 찾는다. 플레이어 전용 컴포넌트는 BP_VBCharacter 가 붙인다(VB.Unit.Character.Assembly).
// 맡지 않음: 이동 의도(UVBLocomotionIntentComponent), 입력, 카메라(UVBHeroComponent), ASC 연결(UVBPawnExtensionComponent), 점프, 앉기 판단(FVBJumpCrouchInput).
UCLASS()
class VB_API AVBCharacter : public ACharacter, public IAbilitySystemInterface, public IVBGuardable,
	public IGenericTeamAgentInterface, public IVBDeathSteps
{
	GENERATED_BODY()

public:
	AVBCharacter(const FObjectInitializer& ObjectInitializer);

	// 지각의 적대 판정이 읽는다. 폰에 두면 엔진이 컨트롤러보다 먼저 찾아 AI, 플레이어가 같은 답을 받는다.
	virtual FGenericTeamId GetGenericTeamId() const override { return VBTeam::ToGenericId(Team); }
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// 입력 의도: 영웅(UVBHeroComponent)이 키 이벤트를 전달하고, 판단은 FVBJumpCrouchInput 이 한다.
	void JumpInputPressed();
	void JumpInputReleased();
	void CrouchInputPressed();

	// 미끄럼 이동 컴포넌트가 미끄럼을 켜고 끌 때 부른다 - 복제되는 bIsSliding 을 쓴다.
	void OnSlideMovementStateChanged(bool bNewSliding);
	bool GetIsSliding() const { return bIsSliding; }
	FORCEINLINE UGameplayCameraComponent* GetGameplayCamera() const { return GameplayCameraComponent; }
	// 엔진 진입점 - 영웅의 카메라 갱신으로 위임하고 아날로그 걷기를 푼다.
	virtual void PawnClientRestart() override;
	virtual void NotifyControllerChanged() override;

	// 캐릭터를 지금 어느 기능이 담당하는지의 사실을 모은다(판정은 VBBodyControlPolicy). 게임 스레드, 캐시 없음, 컴포넌트가 없으면 false.
	VBBodyControlPolicy::FCapsuleRotationFacts GatherCapsuleRotationFacts() const;
	// 캡슐 회전을 기능이 점유했나 - 이동 컴포넌트가 회전 칸 셋을 정할 때 묻는다. 새 기능은 VBBodyControlPolicy::IsCapsuleRotationOwned 에 더한다.
	bool IsCapsuleRotationOwnedByFeature() const;
	// 회전율 정책 - 이동 컴포넌트의 PhysicsRotation 이 회전 직전에만 부른다. 이동, 포즈 소유 경계를 지금 상태로 확인한다.
	void UpdateRotationRateForMovementMode();
	// 이동 설정. 비면 클래스 기본값.
	const UVBMovementConfig* GetMovementConfig() const;

	// IVBGuardable - 데미지 판정이 묻는 가드 질문. 값은 가드 컴포넌트가 갖고 있고, 아래 함수는 그것을 부르기만 한다.
	virtual bool IsGuarding() const override;
	virtual float GetGuardElapsed() const override;
	virtual void PlayGuardAccept(EVBHitDirection Side) override;
	virtual const UVBParryConfig* GetParryConfig() const override;
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) override;
	virtual void ApplyGuardStaminaDelta(float Delta) override;

	// 사망 여부 - ASC 의 State.Dead 보유 판정. AVBEnemyBase::IsDead 와 동일 계약.
	bool IsDead() const;

	// 회복 캔슬 직후 이동 상태를 찍는 진단을 시작한다(vb.Combat.RecoveryCancelTrace 1).
	void BeginRecoveryCancelTrace();

	// 콤보 섹션 전환을 시뮬 프록시에 보낸다(서버, 소유 클라는 이미 넘겼으므로 건너뛴다).
	//   몽타주 위치 복제가 연속 섹션 점프를 놓친다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastJumpToMontageSection(UAnimMontage* Montage, FName SectionName, float BlendSeconds = 0.0f);

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// 공격 워프가 캡슐 회전을 담당하나(AttackTarget 이 있다). 그 수명은 공격 GA 가 정한다.
	bool IsWarpOwningRotation() const;

	// 죽음 단계 중 캐릭터가 맡는 부분(서버, 순서는 UVBHealthComponent).
	// 태그 뒤, 표시 전: 누름 기록, 대기 공격, 락온을 비운다(락온이 남으면 사망 카메라가 캐릭터를 못 비춘다).
	virtual void AfterDeathTag(const FVBDeathFacts& Facts) override;
	// 전투 이탈 타이머 정지(게임오버 화면이 멈추지 않아 만료되면 시체가 수납 연출을 낸다) -> 사망 표시 멀티캐스트.
	virtual void PresentDeath(const FVBDeathFacts& Facts) override;

	// 사망 자세, 입력 차단을 모든 기계에 같은 프레임에 건다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHandleDeathCosmetic();

	// 위 멀티캐스트의 실체. 각 머신에서 로컬로만 작동(복제 없음).
	void HandleDeathCosmetic();

	// 현재 무기에 맞는 사망 몽타주를 고른다. 없으면 DeathMontage, 그것도 없으면 nullptr.
	UAnimMontage* ResolveDeathMontage() const;

	// 소속 세력 - 다른 팀이면 AI 가 적으로 본다. None 이면 AI 에게 보이지 않는다(BeginPlay 가 경고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Team")
	EVBTeam Team = EVBTeam::Player;

	UPROPERTY(BlueprintReadOnly, Replicated, Category="VB|Locomotion")
	bool bIsSliding = false;

	// 3인칭 카메라
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Camera")
	TObjectPtr<UGameplayCameraComponent> GameplayCameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ragdoll")
	TObjectPtr<UVBRagdollComponent> RagdollComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Ragdoll")
	TObjectPtr<UVBPlayerRagdollComponent> PlayerRagdollComponent;
	// 애님 순간 이펙트(발소리, 착지)의 유일한 길.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TObjectPtr<UVBContextEffectComponent> ContextEffectComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UVBLookAtPOIComponent> LookAtPOI;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UVBPlayerAttentionComponent> PlayerAttention;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBGASPSmartObjectComponent> SmartObjectInteraction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|MotionWarping")
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Weapon")
	TObjectPtr<UVBWeaponStateComponent> WeaponStateComponent;

	// 가드 상태, 반격 창, 패링 설정. IVBGuardable 함수가 이것을 부른다.
	UPROPERTY(VisibleAnywhere, Category="VB|Combat")
	TObjectPtr<UVBGuardComponent> GuardComponent;

	// 맨틀, 볼트, 허들 - JumpInputPressed 가 먼저 시도한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Traversal")
	TObjectPtr<UVBTraversalComponent> TraversalComponent;

	// PlayerState ASC 와의 연결 세션 하나(연결, 시작 부여, 해제). 캐릭터의 ASC 는 여기서 읽는다(GetAbilitySystemComponent).
	UPROPERTY(VisibleAnywhere, Category="VB|GAS")
	TObjectPtr<UVBPawnExtensionComponent> PawnExtensionComponent;

	// 이 캐릭터의 죽음을 담당한다(적과 같은 컴포넌트). 캐릭터는 IVBDeathSteps 로 자기 몫 단계만 한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Health")
	TObjectPtr<UVBHealthComponent> HealthComponent;

	// 로컬 입력과 플레이어 카메라 선택을 담당한다(C2, C3). 캐릭터의 입력 바인딩, 카메라 진입점이 이것을 부른다.
	UPROPERTY(VisibleAnywhere, Category="VB|Input")
	TObjectPtr<UVBHeroComponent> HeroComponent;

	// 이동 의도(걸음, 질주, 조준, 스트레이프)를 담당한다.
	UPROPERTY(VisibleAnywhere, Category="VB|Locomotion")
	TObjectPtr<UVBLocomotionIntentComponent> LocomotionIntentComponent;

	// 환경 상호작용 - 가리키는 대상 고르기와 홀드 표시. 판정은 UVBGA_Interact 가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBInteractorComponent> InteractorComponent;

	UPROPERTY(VisibleAnywhere, Category="VB|Lock")
	TObjectPtr<UVBTargetLockComponent> TargetLockComponent;

	// 무기별 몽타주가 없을 때의 사망 몽타주 [Die]+[Loop].
	//   몽타주여야 루트모션이 캡슐에 간다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	TObjectPtr<UAnimMontage> DeathMontage;

	// 무기별 사망 몽타주 - 없으면 DeathMontage(사망은 어떤 무기든 쓰러져야 한다).
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> WeaponDeathMontages;

	// 사망 몽타주의 정착 섹션 이름. 이 섹션을 자기 자신으로 링크해 시체 자세를 영구 유지한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	FName DeathLoopSection = FName("Loop");

private:
	// 미끄럼 점프를 이어 줘도 되는 장면인가. 판정은 VBBodyControlPolicy::BlocksSlideJump - 여기는 원천을 전달하기만 한다.
	bool IsSlideJumpSceneAllowed() const;
	// 캐릭터 소유 사실의 원천(컴포넌트 넷, 질의 넷)을 호출하는 동안만 참조로 전달한다.
	VBBodyControlPolicy::FBodyControlSources MakeBodyControlSources() const;
	// 점프, 앉기 키 판단에 캐릭터의 구성 요소를 호출하는 동안만 참조로 전달한다.
	FVBJumpCrouchInput::FParts MakeJumpCrouchParts();

	// 이 캐릭터에 지금 연결된 ASC(PlayerState 소유). 폰 확장 하나가 관리한다 - 캐릭터는 따로 가지고 있지 않는다.
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// 폰 확장의 연결, 해제 통지 - 캐릭터 기능이 ASC 에 등록한 것을 연결하고 회수한다(기능 종료는 아바타 세션일 때만).
	void HandleAbilitySystemInitialized(UVBAbilitySystemComponent& ASC);
	void HandleAbilitySystemUninitialized(UVBAbilitySystemComponent& ASC, bool bAvatarSession);

	// 죽는 순간 누름 기록, 슬라이드, 상호작용, 래그돌, 조준을 끝낸다.
	void StopBodyActionsForDeath();

	// 이동 튜닝(CMC 물리, 회전율, 무기별 속도, 질주 램프). 비면 클래스 기본값.
	UPROPERTY(EditDefaultsOnly, Category="VB|Movement")
	TObjectPtr<UVBMovementConfig> MovementConfig;

	// 사망 연출이 시작됐나(모든 기계) - 켜지면 이동 방향 판정이 참이다.
	//   State.Dead 는 PlayerState 채널이라 클라에 늦게 온다.
	bool bDeathPresented = false;
	// 점프 키 유지와 미끄럼 점프 요청. Tick 이 사실을 전달한다.
	FVBJumpCrouchInput JumpCrouchInput;
	// 회복 캔슬 진단의 타이머와 남은 표본.
	FVBRecoveryCancelTrace RecoveryCancelTrace;
};
