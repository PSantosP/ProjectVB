// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Character/VBCharacter.h"
#include "Animation/VBMontageTransition.h"
#include "TimerManager.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "VBCharacterMovementTuning.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "Animation/VBPlayerAttentionComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Player/VBPlayerState.h"
#include "Player/VBPlayerController.h"                     // 사망 연출(슬로모/게임오버) 위임 대상
#include "Components/CapsuleComponent.h"                   // 사망 시 캡슐 콜리전 해제
#include "Components/SkeletalMeshComponent.h"

#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBTargetLockConfig.h"
#include "Data/VBMovementConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"   // UAnimMontage 완전 타입 - 가드 몽타주 캐시(TObjectPtr) / Montage_* 호출
                                     // (섹션명 계약 검증은 UVBWeaponDefinition::IsDataValid)
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "VB/VB.h"

#include "MotionWarpingComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBGuardComponent.h"
#include "Character/VBContextEffectComponent.h"
#include "Character/VBFinisherComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Settings/VBOptionRules.h"
#include "Traversal/VBTraversalComponent.h"
#include "Character/VBCombatInputComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Curves/CurveFloat.h"
// PlayAnimation(UAnimationAsset*) 에 UAnimSequence* 를 전달하려면 완전 타입이 필요하다(사망 애니).
#include "Animation/AnimSequence.h"
#include "Animation/VBMotionWarpingNames.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

namespace
{
	// 사망 몽타주를 튼다. 몽타주로 재생해야 루트모션이 캡슐에 간다.
	void PlayLoopingDeathMontage(UAnimInstance& AnimInst, UAnimMontage& DeathMont, FName LoopSection)
	{
		const bool bPlaying = AnimInst.Montage_Play(&DeathMont) > 0.0f;
		if (!bPlaying)
		{
			VB_LOG(Warning, "[VBChar] 사망 몽타주 재생 실패(0 반환): %s", *DeathMont.GetName());
			return;
		}

		// 마지막 섹션을 자기 자신으로 이어 블렌드 아웃을 막는다(에셋의 next section 이 비면 시체가 일어선다).
		if (DeathMont.GetNumSections() > 0)
		{
			const FName FirstSection = DeathMont.GetSectionName(0);
			if (FirstSection != LoopSection)
			{
				AnimInst.Montage_SetNextSection(FirstSection, LoopSection, &DeathMont);
			}
		}

		AnimInst.Montage_SetNextSection(LoopSection, LoopSection, &DeathMont);
	}

	// 사망 표시가 선 뒤 회전 방식을 다시 정한다.
	void RefreshRotationModeForDeath(UCharacterMovementComponent& MoveComp, const AActor& Owner)
	{
		if (UVBSlideMovementComponent* SlideMove = Cast<UVBSlideMovementComponent>(&MoveComp))
		{
			SlideMove->RefreshRotationMode();
		}
		else
		{
			VB_LOG(Warning, "%s: 이동 컴포넌트가 UVBSlideMovementComponent 가 아니다 - 사망 순간 회전 방식을 다시 정하지 못한다", *Owner.GetName());
		}
	}

	// 게임오버 화면은 쓰러지는 첫 섹션이 끝난 뒤 뜬다.
	float ComputeDeathFallSeconds(const UAnimMontage* Montage, FName LoopSection)
	{
		if (!Montage)
		{
			return 0.0f;
		}

		const int32 LoopIdx = Montage->GetSectionIndex(LoopSection);
		return (LoopIdx != INDEX_NONE) ? Montage->GetSectionLength(0) : Montage->GetPlayLength();
	}
}

AVBCharacter::AVBCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVBSlideMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Tick 은 이동 모드 감지와 회전율 설정만 한다. 나머지는 사건으로 돈다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 네트워크
	bReplicates = true;
	SetReplicatingMovement(true);

	// CharacterMovement 기본 설정
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// 물리 기본값은 BeginPlay 의 VBCharacterMovementTuning::ApplyConfig, 걷기 속도, 회전 방식은 이동 컴포넌트가 매 틱 정한다
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	}


	GameplayCameraComponent =CreateDefaultSubobject<UGameplayCameraComponent>(TEXT("GameplayCamera"));
	// GASP 리그는 메시 원점(발) 위에 자기 서 있는 높이를 더한다.
	GameplayCameraComponent->SetupAttachment(GetMesh());
	GameplayCameraComponent->SetAutoActivate(false);
	GameplayCameraComponent->DefaultPlayer = EAutoReceiveInput::Disabled;
	RagdollComponent = CreateDefaultSubobject<UVBRagdollComponent>(TEXT("RagdollComponent"));
	PlayerRagdollComponent = CreateDefaultSubobject<UVBPlayerRagdollComponent>(TEXT("PlayerRagdollComponent"));
	ContextEffectComponent = CreateDefaultSubobject<UVBContextEffectComponent>(TEXT("ContextEffectComponent"));
	LookAtPOI = CreateDefaultSubobject<UVBLookAtPOIComponent>(TEXT("LookAtPOI"));
	PlayerAttention = CreateDefaultSubobject<UVBPlayerAttentionComponent>(TEXT("PlayerAttention"));

	// 회전 방식은 이동 컴포넌트가 정한다 - 여기 값은 시작값.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 플레이어만 쓰는 컴포넌트는 BP_VBCharacter 가 붙인다(목록은 헤더의 클래스 주석) - 이 클래스를 물려받는 NPC 에는 필요 없다.

	// LockComponent 생성
	TargetLockComponent = CreateDefaultSubobject<UVBTargetLockComponent>(TEXT("TargetLockComponent"));

	// MotionWarping 생성
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));
	
	// WeaponState 생성
	WeaponStateComponent = CreateDefaultSubobject<UVBWeaponStateComponent>(TEXT("WeaponStateComponent"));
	GuardComponent = CreateDefaultSubobject<UVBGuardComponent>(TEXT("GuardComponent"));

	// 트래버설 - 점프 의도가 먼저 시도하고, 자산이 없으면 일반 점프로 떨어진다.
	TraversalComponent = CreateDefaultSubobject<UVBTraversalComponent>(TEXT("TraversalComponent"));

	PawnExtensionComponent = CreateDefaultSubobject<UVBPawnExtensionComponent>(TEXT("PawnExtensionComponent"));
	HealthComponent = CreateDefaultSubobject<UVBHealthComponent>(TEXT("HealthComponent"));
	HeroComponent = CreateDefaultSubobject<UVBHeroComponent>(TEXT("HeroComponent"));
	LocomotionIntentComponent = CreateDefaultSubobject<UVBLocomotionIntentComponent>(TEXT("LocomotionIntentComponent"));

	InteractorComponent = CreateDefaultSubobject<UVBInteractorComponent>(TEXT("InteractorComponent"));
	SmartObjectInteraction = CreateDefaultSubobject<UVBGASPSmartObjectComponent>(TEXT("SmartObjectInteraction"));
}

void AVBCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController); // 겉모습의 장비 출처는 그 컴포넌트가 이 안의 컨트롤러 바뀜 통지로 스스로 잇는다
	// 플레이어가 조종할 때만 준비 상태 컴포넌트를 붙인다(로딩 화면이 조회한다). 캐릭터는 포인터를 보관하지 않는다 - 찾는 쪽은 타입으로 찾는다.
	if (IsValid(NewController) && NewController->IsPlayerController() && !FindComponentByClass<UVBPlayerReadinessComponent>())
	{
		UVBPlayerReadinessComponent* Readiness = NewObject<UVBPlayerReadinessComponent>(this, NAME_None, RF_Transient);
		AddInstanceComponent(Readiness);
		Readiness->RegisterComponent();
	}

	// 서버: PlayerState 의 ASC 에 이 캐릭터를 아바타로 연결한다. 시작 부여와 체력 구독 순서는 PawnExtension 이 관리한다.
	if (AVBPlayerState* PS = GetPlayerState<AVBPlayerState>())
	{
		PawnExtensionComponent->InitializeAbilitySystem(PS->GetVBAbilitySystemComponent(), PS);
	}
}

void AVBCharacter::UnPossessed()
{
	// 엔진은 입력 컴포넌트를 지우지만 뗀 사건을 보낸다는 보장은 없다 - 손이 누른 이동값과 벽 위 이동 축을 먼저 0 으로.
	HeroComponent->EndPlayerInput();
	// 떠나는 컨트롤러의 아날로그 걷기는 서버가 직접 해제한다 - 원격 클라의 캐릭터면 위의 0 보고는 서버 사본에서 아무것도 보내지 않는다.
	LocomotionIntentComponent->ResetAnalogWalk();
	// Super 전: 락온 태그 해제는 PlayerState 를, 기능의 종료 경로는 아직 연결된 ASC 를 다시 찾는다. Super 가 PlayerState 를 비운다.
	PawnExtensionComponent->UninitializeAbilitySystem();
	Super::UnPossessed();
}

void AVBCharacter::HandleAbilitySystemInitialized(UVBAbilitySystemComponent& ASC)
{
	// PlayerState 복제가 기능 상태 복제보다 늦게 올 수 있다 - 이어진 뒤 다시 적용한다.
	if (SmartObjectInteraction)
	{
		SmartObjectInteraction->RefreshAbilityState();
	}

	if (PlayerRagdollComponent)
	{
		PlayerRagdollComponent->RefreshAbilityState();
	}
	// 손에 무기를 든 채 다시 연결된 캐릭터면 그 무기의 세트를 다시 부여한다(서버, 이미 부여했으면 그대로).
	if (WeaponStateComponent)
	{
		WeaponStateComponent->BeginAvatarSession();
	}

	// 같은 ASC 로 다시 와도 한 번만 듣는다(컴포넌트가 거른다). 죽음은 0 을 넘어서는 쓰기라 초기화 전의 Health 0 은 죽음이 아니다.
	HealthComponent->InitializeWithAbilitySystem(&ASC);
}

void AVBCharacter::HandleAbilitySystemUninitialized(UVBAbilitySystemComponent& ASC, bool bAvatarSession)
{
	// 서버만 돌려준다 - 클라의 정리는 서버 결과의 복제를 앞지른다.
	if (bAvatarSession && HasAuthority())
	{
		// 이 캐릭터가 ASC 에 건 것을 기능의 종료 경로로 해제한다 - 다른 Pawn 이 빙의되어도 막히지 않게.
		if (SmartObjectInteraction)
		{
			SmartObjectInteraction->AbortInteraction();
		}

		if (PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive())
		{
			PlayerRagdollComponent->CancelRagdoll();
		}

		if (TargetLockComponent)
		{
			TargetLockComponent->ClearLockOn();
		}

		if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(this))
		{
			CombatInput->ResetForAvatarSession(&ASC);
		}

		if (WeaponStateComponent)
		{
			WeaponStateComponent->EndAvatarSession(); // 무기 세트의 능력, 효과는 이 캐릭터의 것 - PlayerState ASC 에 남기지 않는다
		}
	}
	// 가드 세션(반격 창 몫 포함)은 연 ASC 에 돌려준다 - 늦은 정리여도 남의 몫을 걷지 않는다.
	GuardComponent->ReleaseSession();
	HealthComponent->UninitializeFromAbilitySystem();
}

UVBAbilitySystemComponent* AVBCharacter::GetVBAbilitySystemComponent() const
{
	return PawnExtensionComponent ? PawnExtensionComponent->GetAbilitySystemComponent() : nullptr;
}


// 사망: 단계 순서, 태그, 능력 취소는 UVBHealthComponent, 여기는 캐릭터가 맡는 단계(IVBDeathSteps). 시체는 지우지 않는다(뷰타겟).

bool AVBCharacter::IsDead() const
{
	// 판정은 UVBAbilitySystemStatics 가 소유한다 - 여기는 "어느 ASC 인가"만 정한다.
	return UVBAbilitySystemStatics::IsDead(GetVBAbilitySystemComponent());
}

void AVBCharacter::AfterDeathTag(const FVBDeathFacts& Facts)
{
	if (UVBAbilitySystemComponent* ASC = GetVBAbilitySystemComponent())
	{
		ASC->ClearInputPresses(); // 죽은 캐릭터의 누르는 중, 저장 누름이 부활 뒤 길게 누름으로 읽히지 않게
	}

	if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(this))
	{
		CombatInput->ResetForAvatarSession(GetVBAbilitySystemComponent());
	}

	if (TargetLockComponent)
	{
		TargetLockComponent->ClearLockOn(); // 서버라 즉시 풀린다(클라였다면 RPC)
	}
}

void AVBCharacter::PresentDeath(const FVBDeathFacts& Facts)
{
	if (WeaponStateComponent)
	{
		WeaponStateComponent->StopCombatTimer();
	}

	MulticastHandleDeathCosmetic();
}

void AVBCharacter::MulticastHandleDeathCosmetic_Implementation()
{
	// 원격 클라에 State.Dead 를 보완하지 않는다 - CountToOwner 태그는 모든 연결에 복제되고, 이 태그를 읽는 곳은 서버뿐이다.
	HandleDeathCosmetic();
}

UAnimMontage* AVBCharacter::ResolveDeathMontage() const
{
	// 무기별 몽타주, 없으면 기본 - 사망은 어떤 무기든 쓰러져야 한다.
	if (WeaponStateComponent)
	{
		const EVBWeaponType CurrentWeapon = WeaponStateComponent->GetCurrentWeaponType();
		if (const TObjectPtr<UAnimMontage>* Found = WeaponDeathMontages.Find(CurrentWeapon))
		{
			if (*Found)
			{
				return *Found;
			}
		}
	}

	return DeathMontage;
}

void AVBCharacter::HandleDeathCosmetic()
{
	StopBodyActionsForDeath();
	// 1. 폰 채널만 무시한다 - 적은 통과하고 캡슐은 시체를 바닥에 받친다(전부 끄면 바닥 아래로 빠진다).
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// 2. 사망 몽타주 - 루트모션이 캡슐에 가려면 몽타주여야 하고, Loop 섹션 자기 링크가 시체 자세를 유지한다.
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp)
	{
		UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
		UAnimMontage* DeathMont = ResolveDeathMontage();
		if (AnimInst && DeathMont)
		{
			PlayLoopingDeathMontage(*AnimInst, *DeathMont, DeathLoopSection);
		}
		else
		{
			// 미할당이면 선 채로 굳으니 오류를 남긴다.
			VB_LOG(Warning, "[VBChar] 사망 몽타주 미할당 — 사망 자세가 재생되지 않습니다: %s", *GetName());
		}
	}

	// 3. 속도만 지우고 이동 모드는 둔다 - 중력이 시체를 착지시킨다(DisableMovement 는 공중에 얼린다).
	//   몽타주 재생 뒤에 둔다: 끊긴 몽타주의 종료 콜백(트래버설 회복)이 남긴 속도를 마지막으로 지운다.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();

		// 사망 연출이 이동 방향을 정하므로 회전 방식을 지금 다시 정한다(시체가 컨트롤 회전 쪽으로 돌지 않게).
		bDeathPresented = true;
		RefreshRotationModeForDeath(*MoveComp, *this);
	}

	// 4. 로컬 조종 중이면 입력 차단과 컨트롤러 사망 연출(슬로모, HUD, 게임오버). 카메라는 그대로 둔다.
	if (IsLocallyControlled())
	{
		if (AVBPlayerController* VBPC = Cast<AVBPlayerController>(GetController()))
		{
			// 폰의 입력 묶음을 통째로 끈다 - 무기 칸, 락온, 이동은 능력을 거치지 않는다. 컨트롤러 쪽 입력은 OnPlayerDeath 가 끈다.
			DisableInput(VBPC);
			VBPC->OnPlayerDeath(ComputeDeathFallSeconds(ResolveDeathMontage(), DeathLoopSection));
		}
	}
}

void AVBCharacter::StopBodyActionsForDeath()
{
	// 모든 기계에서 돈다. 소유 기계의 누르는 중 기록을 지운다 - 누른 채 죽어 놓음 콜백이 안 오면 누르는 중 상태가 남는다.
	UVBAbilitySystemComponent* ASC = GetVBAbilitySystemComponent();
	if (IsLocallyControlled() && ASC)
	{
		ASC->ClearInputPresses();
	}

	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(GetCharacterMovement()))
	{
		SlideCMC->CancelSlide();
	}

	if (SmartObjectInteraction)
	{
		SmartObjectInteraction->AbortInteraction();
	}

	if (PlayerRagdollComponent)
	{
		PlayerRagdollComponent->CancelRagdoll();
	}

	LocomotionIntentComponent->SetWantsToAim(false);
}

void AVBCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 팀이 None 이면 AI 에게 보이지 않는다 - 증상이 안 쫓아오는 것뿐이라 경고한다.
	if (Team == EVBTeam::None)
	{
		VB_LOG(Warning, "%s: Team 이 None - 적 AI 가 이 캐릭터를 감지하지 못한다. BP 의 Team 확인", *GetName());
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetControlRotation(GetActorRotation());
	}

	// CMC 물리 기본값을 설정 자산(없으면 클래스 기본값)에서 적용한다.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		VBCharacterMovementTuning::ApplyConfig(*CMC, *GetMovementConfig());
	}
}

const UVBMovementConfig* AVBCharacter::GetMovementConfig() const
{
	// 배선된 자산, 없으면 클래스 기본값.
	return MovementConfig ? MovementConfig : GetDefault<UVBMovementConfig>();
}

bool AVBCharacter::IsGuarding() const { return GuardComponent->IsGuarding(); }
float AVBCharacter::GetGuardElapsed() const { return GuardComponent->GetGuardElapsed(); }
void AVBCharacter::PlayGuardAccept(EVBHitDirection Side) { GuardComponent->PlayGuardAccept(Side); }
const UVBParryConfig* AVBCharacter::GetParryConfig() const { return GuardComponent->GetParryConfig(); }
void AVBCharacter::OpenRiposteWindow(float Duration, AActor* Attacker) { GuardComponent->OpenRiposteWindow(Duration, Attacker); }
void AVBCharacter::ApplyGuardStaminaDelta(float Delta) { GuardComponent->ApplyGuardStaminaDelta(Delta); }

void AVBCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 기존 Blueprint 템플릿이 옛 캡슐 부착을 아직 직렬화하고 있을 수 있다.
	if (GameplayCameraComponent && GetMesh() && GameplayCameraComponent->GetAttachParent() != GetMesh())
	{
		GameplayCameraComponent->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	// 첫 빙의(PossessedBy)보다 먼저 와야 한다 - 같은 수명의 컴포넌트라 해제 핸들은 들고 있지 않는다.
	PawnExtensionComponent->RegisterAndCallOnAbilitySystemInitialized(
		FVBAbilitySystemInitializedDelegate::FDelegate::CreateUObject(this, &AVBCharacter::HandleAbilitySystemInitialized));
	PawnExtensionComponent->RegisterOnAbilitySystemUninitialized(
		FVBAbilitySystemUninitializedDelegate::FDelegate::CreateUObject(this, &AVBCharacter::HandleAbilitySystemUninitialized));
}

bool AVBCharacter::IsWarpOwningRotation() const
{
	// 워프 목표의 수명은 공격 GA 가 정한다 - 남아 있으면 워프가 회전을 맡는다.
	const UMotionWarpingComponent* MWC = MotionWarpingComponent.Get();
	return MWC && MWC->FindWarpTarget(VBMotionWarpingNames::AttackTarget) != nullptr;
}

bool AVBCharacter::IsCapsuleRotationOwnedByFeature() const
{
	// 누가 왜 회전을 점유하는지는 VBBodyControlPolicy 가 판단한다.
	return VBBodyControlPolicy::IsCapsuleRotationOwned(GatherCapsuleRotationFacts());
}

VBBodyControlPolicy::FBodyControlSources AVBCharacter::MakeBodyControlSources() const
{
	VBBodyControlPolicy::FBodyControlSources Sources;
	Sources.PlayerRagdoll = PlayerRagdollComponent;
	Sources.SmartObject = SmartObjectInteraction;
	Sources.Interactor = InteractorComponent;
	Sources.Traversal = TraversalComponent;
	Sources.bFinisherScene = UVBFinisherComponent::IsActorInScene(this);
	Sources.bAttackWarp = IsWarpOwningRotation();
	Sources.bFinishing = UVBAbilitySystemStatics::IsActorFinishing(this);
	Sources.bDead = IsDead();
	Sources.bDeathPresented = bDeathPresented;
	return Sources;
}

VBBodyControlPolicy::FCapsuleRotationFacts AVBCharacter::GatherCapsuleRotationFacts() const
{
	return VBBodyControlPolicy::GatherCapsuleRotationFacts(MakeBodyControlSources());
}

void AVBCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!JumpCrouchInput.IsSlideJumpIdle())
	{
		JumpCrouchInput.TickSlideJump(*this, IsSlideJumpSceneAllowed());
	}

	// JustLanded 는 애님 인스턴스가 자기 평가 뒤 지운다.

	// 공중 맨틀: 점프 키를 누른 채 떨어지는 동안 매 틱 앞 맨틀을 시도한다(Mantle, Climb 만, 소유 클라만).
	if (JumpCrouchInput.IsJumpHeld() && TraversalComponent)
	{
		if (const UCharacterMovementComponent* CMC = GetCharacterMovement(); CMC && CMC->IsFalling())
		{
			TraversalComponent->TryTraversalAction( /*bAirMantleOnly=*/true);
		}
	}
	// 질주 자동 진입: Shift + 앞 입력이면 지상, 공중 같은 판정을 주기로 부른다(판단은 TryAutoTraversal, 소유 클라만).
	if (TraversalComponent && IsLocallyControlled())
	{
		TraversalComponent->TryAutoTraversal();
	}
}

void AVBCharacter::UpdateRotationRateForMovementMode()
{
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		VBCharacterMovementTuning::RefreshRotationRate(*this, *CMC, WeaponStateComponent);
	}
}

void AVBCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 클라이언트: 서버의 PossessedBy, UnPossessed 가 여기로 온다. PlayerState 가 비거나 바뀌면 이전 연결을 해제한다.
	AVBPlayerState* PS = GetPlayerState<AVBPlayerState>();
	UVBAbilitySystemComponent* ASC = PS ? PS->GetVBAbilitySystemComponent() : nullptr;
	if (ASC)
	{
		PawnExtensionComponent->InitializeAbilitySystem(ASC, PS);
	}
	else
	{
		PawnExtensionComponent->UninitializeAbilitySystem();
	}
}

void AVBCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// 무엇을 어디에 묶을지는 영웅의 입력 설정 자산이 정한다.
	HeroComponent->InitializePlayerInput(PlayerInputComponent);
}

void AVBCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AVBCharacter, bIsSliding);
}

void AVBCharacter::OnSlideMovementStateChanged(bool bNewSliding)
{
	if (bIsSliding == bNewSliding)
	{
		return;
	}

	bIsSliding = bNewSliding;
}

void AVBCharacter::OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	Super::OnEndCrouch(HeightAdjust, ScaledHeightAdjust);
	// 웅크림 때문에 미뤄 둔 공격 입력은 공격 입력 컴포넌트가 보유한다 - 일어선 뒤 버퍼는 이 캐릭터의 이동 설정 값이다.
	if (UVBCombatInputComponent* CombatInput = UVBCombatInputComponent::FindFor(this))
	{
		CombatInput->HandleStoodUp(GetMovementConfig()->CrouchAttackStandBufferSeconds);
	}
}

void AVBCharacter::MulticastJumpToMontageSection_Implementation(UAnimMontage* Montage, FName SectionName, float BlendSeconds)
{
	// 시뮬 프록시만 - 서버와 소유 클라는 자기 능력에서 이미 넘겼다.
	if (HasAuthority())
	{
		return;
	}

	if (IsLocallyControlled())
	{
		return;
	}

	if (!Montage)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	// 같은 몽타주가 재생 중일 때만(늦게 온 멀티캐스트 방어).
	if (AnimInstance->Montage_IsPlaying(Montage))
	{
		if (!VBMontageTransition::RequestSectionBlend(AnimInstance, Montage, BlendSeconds))
		{
			VB_LOG(Warning, "Combo section blend rejected on proxy: %.3f", BlendSeconds);
			return;
		}
		AnimInstance->Montage_JumpToSection(SectionName, Montage);
	}
}

bool AVBCharacter::IsSlideJumpSceneAllowed() const
{
	return !VBBodyControlPolicy::BlocksSlideJump(VBBodyControlPolicy::GatherSlideJumpSceneFacts(MakeBodyControlSources()));
}

bool AVBCharacter::CanJumpInternal_Implementation() const
{
	// 들고 있는 요청이 없으면 장면을 묻지 않는다(옛 줄과 같은 단락 평가).
	return (JumpCrouchInput.IsSlideJumpIdle()
		|| !JumpCrouchInput.BlocksSlideJump(IsSlideJumpSceneAllowed())) && Super::CanJumpInternal_Implementation();
}

FVBJumpCrouchInput::FParts AVBCharacter::MakeJumpCrouchParts()
{
	FVBJumpCrouchInput::FParts Parts;
	Parts.Body = this;
	Parts.AbilitySystem = GetVBAbilitySystemComponent();
	Parts.Interactor = InteractorComponent;
	Parts.SmartObject = SmartObjectInteraction;
	Parts.PlayerRagdoll = PlayerRagdollComponent;
	Parts.Traversal = TraversalComponent;
	Parts.LocomotionIntent = LocomotionIntentComponent;
	Parts.Hero = HeroComponent;
	return Parts;
}

void AVBCharacter::JumpInputPressed()
{
	JumpCrouchInput.JumpPressed(MakeJumpCrouchParts());
}

void AVBCharacter::JumpInputReleased()
{
	JumpCrouchInput.JumpReleased(*this);
}

void AVBCharacter::CrouchInputPressed()
{
	JumpCrouchInput.CrouchPressed(MakeJumpCrouchParts());
}

void AVBCharacter::BeginRecoveryCancelTrace()
{
	RecoveryCancelTrace.Begin(*this);
}

UAbilitySystemComponent* AVBCharacter::GetAbilitySystemComponent() const
{
	return GetVBAbilitySystemComponent();
}

void AVBCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	HeroComponent->RefreshActiveCamera();
	// 새로 빙의한 Pawn 은 아날로그 걷기 없이 시작한다 - 다음 이동 입력이 현재 기울기를 다시 알린다.
	LocomotionIntentComponent->ResetAnalogWalk();
}

void AVBCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	HeroComponent->RefreshActiveCamera();
}
