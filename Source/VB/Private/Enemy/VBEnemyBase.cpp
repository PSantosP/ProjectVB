// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEnemyMovementRules.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Enemy/VBEnemySetup.h"
#include "Enemy/VBEnemyAuthoring.h"
#include "Enemy/VBEnemyDeathConsequences.h"
#include "Enemy/VBEnemyDeathPresentationComponent.h"
#include "Enemy/VBEnemyRagdollComponent.h"
#include "CharacterCommon/VBHealthComponent.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AI/VBEnemyCombatComponent.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "CharacterCommon/VBDeathDissolveComponent.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "CharacterCommon/VBHitPhysicsComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBContextEffectComponent.h"
#include "Animation/VBEnemyLookAtComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "ContextualAnimSceneActorComponent.h"
#include "MotionWarpingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h" // 가산 플린치 재생
#include "Animation/AnimMontage.h"
#include "Data/VBEnemyConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "VB/VB.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "CharacterCommon/VBCharacterSurface.h"
#include "Enemy/VBEnemyRegistrySubsystem.h"
#if !UE_BUILD_SHIPPING
// 개발용 - 설정 뒤 스폰된 적은 시작 체력이 Scale 배가 된다(1 = 끔). InitStats 가 서버 전용이라 서버가 적용한다
static TAutoConsoleVariable<float> CVarEnemyHealthScale(
	TEXT("vb.Combat.EnemyHealthScale"), 1.0f,
	TEXT("Dev test: multiply enemy initial MaxHealth/Health at spawn. 1 = off. Set before pressing Play for placed enemies."),
	ECVF_Cheat);
#endif


AVBEnemyBase::AVBEnemyBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// C++ 은 액터 틱을 켜지 않는다(캡슐 따라가기는 UVBEnemyRagdollComponent 의 틱). 값은 BP 기본값이 덮었을 수 있어 옛 그대로 둔다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// ASC 생성 - replication 은 UVBAbilitySystemComponent 생성자가 자체 설정(SetIsReplicatedByDefault)하므로 여기선 호출 불요
	AbilitySystemComponent = CreateDefaultSubobject<UVBAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// ASC 기본값은 Mixed(플레이어용)이나, NPC는 Minimal로 다시 재설정한다.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);


	// Attribute 생성
	HealthAttributeSet = CreateDefaultSubobject<UVBHealthAttributeSet>(TEXT("HealthAttributeSet"));
	CombatAttributeSet = CreateDefaultSubobject<UVBCombatAttributeSet>(TEXT("CombatAttributeSet"));
	HealthComponent = CreateDefaultSubobject<UVBHealthComponent>(TEXT("HealthComponent"));

	// 피격 플래시 컴포넌트 - 모든 적이 자동으로 갖는다. BP 배선 누락으로 특정 적만 안 빛나는 사고를 구조적으로 막는다.
	HitFlashComponent = CreateDefaultSubobject<UVBHitFlashComponent>(TEXT("HitFlashComponent"));

	// 사망 디졸브 컴포넌트 - 위와 같은 이유로 C++ 생성. 빠뜨리면 그 적만 시체로 남는다.
	DeathDissolveComponent = CreateDefaultSubobject<UVBDeathDissolveComponent>(TEXT("DeathDissolveComponent"));
	RagdollComponent = CreateDefaultSubobject<UVBRagdollComponent>(TEXT("RagdollComponent"));
	EnemyRagdollComponent = CreateDefaultSubobject<UVBEnemyRagdollComponent>(TEXT("EnemyRagdollComponent"));
	DeathPresentationComponent = CreateDefaultSubobject<UVBEnemyDeathPresentationComponent>(TEXT("DeathPresentationComponent"));
	HitPhysicsComponent = CreateDefaultSubobject<UVBHitPhysicsComponent>(TEXT("HitPhysicsComponent"));
	ContextEffectComponent = CreateDefaultSubobject<UVBContextEffectComponent>(TEXT("ContextEffectComponent"));
	LookAtComponent = CreateDefaultSubobject<UVBEnemyLookAtComponent>(TEXT("LookAtComponent"));

	// 전투 조율 컴포넌트 - 위와 같은 이유로 C++ 생성. 이 컴포넌트가 없으면 BTService 가 조용히 아무것도
	// 하지 않아 그 적만 그룹에 합류하지 않는다(로그 없는 결함).
	CombatComponent = CreateDefaultSubobject<UVBEnemyCombatComponent>(TEXT("CombatComponent"));

	// 무기 궤적 판정 - 위와 같은 이유로 C++ 생성. 모양 자산은 적 BP 마다 꽂는다.
	MeleeTraceComponent = CreateDefaultSubobject<UVBMeleeTraceComponent>(TEXT("MeleeTraceComponent"));

	// 처형 장면의 피해자 자리 - 위 둘과 같은 이유로 C++ 생성이다. 여기서는 'BP 배선 누락' 에
	// 「원격 클라에서만 도착이 늦는다」가 하나 더 얹힌다. 근거는 헤더 주석에 있다.
	FinisherSceneComponent =
		CreateDefaultSubobject<UContextualAnimSceneActorComponent>(TEXT("FinisherSceneComponent"));

	// 공격 워프의 소비자. 몽타주의 AnimNotifyState_MotionWarping 이 소유자에서 이 컴포넌트를 찾아 구간을 건다 -
	//   없으면 노티가 조용히 아무것도 안 해 공격이 생 루트모션으로 돈다.
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));

	// Movement 구조적 설정(튜닝값 아님). 속도/회전율은 BeginPlay 에서 EnemyConfig 적용
	// - 생성자에선 BP 가 할당한 config 가 아직 유효하지 않으므로 CDO 디폴트만 보임.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bUseControllerDesiredRotation = true; // AI 회전
	}
	
	// 몸은 데칼을 받지 않도록 설정
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		VBCharacterSurface::ApplyBodyRules(*Body);
	}

	// 네트워크
	bReplicates = true;
}

void AVBEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	// 근처 적 체력 바가 고르는 목록. 모든 기계에서 올린다(적은 복제된다) - 표시용이라 권한을 가리지 않는다.
	if (UVBEnemyRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBEnemyRegistrySubsystem>() : nullptr)
	{
		Registry->Register(this);
	}

	// 이동 튜닝 적용(EnemyConfig, 없으면 클래스 기본값). 걸음은 전투 서비스가 정하고 ApplyGait 가 쓴다.
	//   걷기로 시작한다 - 서비스가 안 돌면 고장이 눈에 띄게.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->RotationRate = GetEnemyConfig()->RotationRate;
	}

	ApplyGait(EVBGait::Walk);
	HitPhysicsComponent->SetConfig(GetEnemyConfig()->HitPhysics);
	VBEnemySetup::ApplyAnimTickPolicy(GetMesh(), *GetEnemyConfig());

	// 산 적의 캡슐, 메시가 카메라 채널을 무시한다 - 근접하면 스프링암 프로브가 적에 막혀 카메라가 당겨진다. BP 프로파일이 덮으므로 BeginPlay 에서 건다.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// ASC 초기화 (Owner = Avatar = Self)
	// NPC는 PlayerState 없음 -> 자기 자신이 Owner이자 Avatar
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		if (HasAuthority())
		{
			// 시작 능력, 초기 스탯. 세트 없는 적은 공격하지 않는다 - 아래 계약 검사가 그 공격 태그마다 경고한다.
			if (AbilitySet)
			{
				AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, AbilitySetGrant, this);
			}
			else
			{
				VB_LOG(Warning, "%s: 능력 세트(AbilitySet)가 비었다 - 능력도 초기 스탯도 받지 않는다", *GetName());
			}

			// 공격 어빌리티 계약을 여기서 한 번 검증한다(컴포넌트 BeginPlay 때는 세트 부여 전이라 거짓 경고가 된다).
			if (CombatComponent)
			{
				CombatComponent->ValidateAttackAbilityContract();
			}

#if !UE_BUILD_SHIPPING
			// 스폰 때 읽는다 - 배치된 적은 Play 전에 넣은 값을 받는다
			VBEnemySetup::ApplyDevHealthScale(AbilitySystemComponent, CVarEnemyHealthScale.GetValueOnGameThread());
#endif
		}

		HealthComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
	}

	// 복제 값이 BeginPlay 전에 올 수 있다 - ASC 초기화가 끝난 뒤에 재생을 허락한다.
	DeathPresentationComponent->MarkBodyReady();
	EnemyRagdollComponent->MarkBodyReady();
}

UAbilitySystemComponent* AVBEnemyBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

FGenericTeamId AVBEnemyBase::GetGenericTeamId() const
{
	return VBTeam::ToGenericId(GetEnemyConfig()->Team);
}

const UVBEnemyConfig* AVBEnemyBase::GetEnemyConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return EnemyConfig ? EnemyConfig : GetDefault<UVBEnemyConfig>();
}

void AVBEnemyBase::PreviewDeathDissolve()
{
	VBEnemyAuthoring::PreviewDeathDissolve(*this, *GetEnemyConfig(), DeathDissolveComponent);
}

void AVBEnemyBase::ResetDeathDissolvePreview()
{
	VBEnemyAuthoring::ResetDeathDissolvePreview(DeathDissolveComponent);
}

// 걸음을 바꾼다. 속도를 쓰는 유일한 지점이다.
void AVBEnemyBase::ApplyGait(EVBGait NewGait)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	const UVBEnemyConfig* Cfg = GetEnemyConfig();
	const float NewSpeed = VBEnemyMovementRules::CalculateGaitSpeed(NewGait, Cfg->WalkSpeed, Cfg->RunSpeed, VBStrideScale::Resolve(this));

	// 값이 실제로 바뀔 때만 로그. 매 주기 찍으면 그룹당 적 수만큼 쌓여 정작 전환이 묻힌다
	//   (코디네이터의 역할 전환 로그와 같은 규칙).
	VB_CLOG(NewGait != CurrentGait, Log, "걸음 전환: %s %s -> %s (속도 %.0f)",
		*GetName(),
		*UEnum::GetValueAsString(CurrentGait), *UEnum::GetValueAsString(NewGait), NewSpeed);

	CurrentGait = NewGait;
	MoveComp->MaxWalkSpeed = NewSpeed;
}

void AVBEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HealthComponent->UninitializeFromAbilitySystem();
	if (UVBEnemyRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBEnemyRegistrySubsystem>() : nullptr)
	{
		Registry->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
#include "Misc/DataValidation.h" // FDataValidationContext::AddError

EDataValidationResult AVBEnemyBase::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!VBEnemyAuthoring::ValidateAnimation(GetMesh(), *GetEnemyConfig(), Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#endif

FGameplayTag AVBEnemyBase::GetCurrentAttackInputTag() const
{
	// GetEnemyConfig() 는 미할당이면 CDO 를 돌려주므로 null 이 아니다. CDO 의 태그는 비어 있고, 그 부재는
	//   부팅 시 ValidateAttackAbilityContract 가 1회 경고한다(여기서 경고하면 0.5초마다 반복된다).
	return GetEnemyConfig()->AttackAbilityInputTag;
}


bool AVBEnemyBase::IsTargetable() const
{
	return !IsDead();
}

bool AVBEnemyBase::IsDead() const
{
	// 판정은 UVBAbilitySystemStatics 가 소유한다 - 여기는 "어느 ASC 인가"만 정한다.
	return DeathPresentationComponent->IsDeathReplicated() || UVBAbilitySystemStatics::IsDead(AbilitySystemComponent);
}

FGameplayTagContainer AVBEnemyBase::GetTargetIdentityTags() const
{
	return GetEnemyConfig()->EnemyTags;
}

FVector AVBEnemyBase::GetTargetMarkerLocation() const
{
	// 락온 카메라의 조준점(가슴 높이) - 캡슐 중심을 겨누면 근접 시 pitch 가 처박힌다. 본이 없으면 actor+Z, 0 벡터는 돌려주지 않는다.
	if (const USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// DoesSocketExist = 소켓+본 둘 다 검사. 없는 본에 GetSocketLocation 하면 엔진이 조용히
		// 컴포넌트(발) 위치로 폴백하므로, 반드시 존재 확인 후에만 소켓 위치를 신뢰한다.
		if (MeshComp->DoesSocketExist(MarkerBoneName))
		{
			return MeshComp->GetSocketLocation(MarkerBoneName); // 스켈레톤별 자동 스케일(가슴)
		}
	}
	// 폴백: 캡슐 중심 기준 Z 상승(가슴 높이대). 절대 ZeroVector 아님.
	return GetActorLocation() + FVector(0.0f, 0.0f, MarkerFallbackHeight);
}

void AVBEnemyBase::BeforeDeathTag(const FVBDeathFacts& Facts)
{
	DeathPresentationComponent->CaptureAirDeath(Facts);
	VBEnemyDeathConsequences::ApplyDefaultReputation(*this, Facts, *GetEnemyConfig(), DefaultDeathReputationEffect);
}

void AVBEnemyBase::AfterDeathTag(const FVBDeathFacts& Facts)
{
	VBEnemyDeathConsequences::AnnounceKill(*this, Facts, *GetEnemyConfig());
	if (CombatComponent)
	{
		CombatComponent->LeaveCombat();
	}
}

void AVBEnemyBase::PresentDeath(const FVBDeathFacts& Facts)
{
	DeathPresentationComponent->Present(Facts);
}

void AVBEnemyBase::MulticastPlayAdditiveFlinch_Implementation(UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || Montage->SlotAnimTracks.IsEmpty())
	{
		return;
	}
	// 같은 슬롯의 앞 가산 몽타주는 손으로 멈춘다 - 그룹 정지를 끄므로 엔진이 멈춰 주지 않고, 겹치면 가산이 두 번 더해진다.
	const FName Slot = Montage->SlotAnimTracks[0].SlotName;
	TArray<UAnimMontage*, TInlineAllocator<4>> Previous;
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->IsActive() && Instance->Montage->IsValidAdditive()
			&& Instance->Montage->IsValidSlot(Slot))
		{
			Previous.AddUnique(Instance->Montage);
		}
	}

	for (UAnimMontage* Old : Previous)
	{
		AnimInstance->Montage_Stop(Old->BlendOut.GetBlendTime(), Old);
	}
	// 그룹 정지 끔(bStopAllMontages=false): 켜 두면 같은 DefaultGroup 의 넘어짐, 기상 몽타주가 멈춘다
	//   (UAnimInstance::Montage_PlayInternal - 한 그룹에 몽타주 하나).
	AnimInstance->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, /*bStopAllMontages=*/false);
}

bool AVBEnemyBase::IsBodyDownedInPhysics() const
{
	return EnemyRagdollComponent->IsDownedInPhysics();
}

float AVBEnemyBase::ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale)
{
	return EnemyRagdollComponent->ReceiveDownedImpact(Hit, PushDir, Impact, KnockbackScale);
}
