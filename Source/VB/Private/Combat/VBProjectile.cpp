// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Combat/VBProjectile.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueFunctionLibrary.h"
#include "GameplayEffect.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Combat/VBProjectileConfig.h"
#include "VB/VB.h"

AVBProjectile::AVBProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(20.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetGenerateOverlapEvents(true);
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->InitialSpeed = 0.0f;
	Movement->MaxSpeed = 0.0f;
	// 발사 전에는 떨어지지 않는다 - 중력은 설정이 Launch 에서 준다.
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->bSweepCollision = true;
	// 속도는 월드 기준으로 받는다(엔진 기본은 로컬 기준이라 스폰 회전이 한 번 더 곱해진다).
	Movement->bInitialVelocityInLocalSpace = false;
}

void AVBProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AVBProjectile::HandleBeginOverlap);
	Movement->OnProjectileStop.AddDynamic(this, &AVBProjectile::HandleWorldStop);
}

void AVBProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBProjectile, Config);
}

void AVBProjectile::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	Super::PostNetReceiveVelocity(NewVelocity);
	Movement->Velocity = NewVelocity;
}

void AVBProjectile::Launch(const UVBProjectileConfig& InConfig, AActor& InThrower, const FGameplayEffectSpecHandle& InSpec,
	const FVBHitSpecValues& InValues, const FVector& Velocity)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Config)
	{
		VB_LOG(Warning, "투사체 %s: Launch 가 두 번 왔다 - 두 번째(%s)를 무시한다", *GetName(), *InConfig.GetName());
		return;
	}

	if (!InSpec.IsValid())
	{
		VB_LOG(Error, "투사체 %s: %s 가 빈 스펙으로 던졌다 - 피해를 낼 수 없어 없앤다", *GetName(), *InThrower.GetName());
		Destroy();
		return;
	}

	Config = &InConfig;
	Thrower = &InThrower;
	Spec = InSpec;
	HitValues = InValues;
	ApplyConfig();

	Movement->Velocity = Velocity;
	Movement->UpdateComponentVelocity();
	// BeginPlay 가 뒤에 와도 같은 수명이 다시 걸리게 둘 다 쓴다.
	InitialLifeSpan = InConfig.LifetimeSeconds;
	SetLifeSpan(InConfig.LifetimeSeconds);

	// 발사 자리에서 이미 겹친 폰은 BeginOverlap 이 다시 오지 않는다 - 지금 한 번 본다.
	TArray<UPrimitiveComponent*> AlreadyOverlapping;
	Collision->GetOverlappingComponents(AlreadyOverlapping);
	for (UPrimitiveComponent* Other : AlreadyOverlapping)
	{
		if (bSpent)
		{
			break;
		}

		TryHitPawn(Other ? Other->GetOwner() : nullptr, Other, false, FHitResult());
	}
}

void AVBProjectile::ApplyConfig()
{
	if (!Config)
	{
		return;
	}
	// 반경을 바꾸면 엔진이 겹침을 다시 잰다(SetSphereRadius 기본) - 아래 이미 겹친 폰 검사가 설정 반경을 본다.
	Collision->SetSphereRadius(Config->CollisionRadius);
	Mesh->SetStaticMesh(Config->Mesh);
	Movement->ProjectileGravityScale = Config->GravityScale;

	// 궤적은 이 기기에서만 낸다 - ASC 가 없는 대상이라 복제되지 않고, 서버, 클라가 각자 한 번씩 부른다.
	if (Config->TrailCueTag.IsValid() && GetNetMode() != NM_DedicatedServer)
	{
		FGameplayCueParameters TrailParams;
		TrailParams.Location = GetActorLocation();
		TrailParams.EffectCauser = this;
		TrailParams.SourceObject = Config.Get();
		UGameplayCueFunctionLibrary::AddGameplayCueOnActor(this, Config->TrailCueTag, TrailParams);
		bTrailActive = true;
	}
}

void AVBProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bTrailActive && Config)
	{
		FGameplayCueParameters TrailParams;
		TrailParams.EffectCauser = this;
		UGameplayCueFunctionLibrary::RemoveGameplayCueOnActor(this, Config->TrailCueTag, TrailParams);
		bTrailActive = false;
	}

	Super::EndPlay(EndPlayReason);
}

void AVBProjectile::OnRep_Config()
{
	ApplyConfig();
}

void AVBProjectile::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryHitPawn(OtherActor, OtherComp, bFromSweep, SweepResult);
}

UAbilitySystemComponent* AVBProjectile::FindHostileTarget(AActor* OtherActor) const
{
	AActor* const ThrowerActor = Thrower.Get();
	// 던진 쪽이 사라졌으면 편을 물을 수 없다 - 모르는 편은 때리지 않는다.
	if (!OtherActor || !ThrowerActor || OtherActor == ThrowerActor || OtherActor == this)
	{
		return nullptr;
	}

	if (UVBAbilitySystemStatics::IsFriendlyFire(ThrowerActor, OtherActor))
	{
		return nullptr;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor);
	if (!TargetASC || UVBAbilitySystemStatics::IsDead(TargetASC))
	{
		return nullptr;
	}

	return TargetASC;
}

void AVBProjectile::TryHitPawn(AActor* OtherActor, UPrimitiveComponent* OtherComp, bool bFromSweep, const FHitResult& SweepResult)
{
	// 피해는 서버만 낸다. 발사 전(스폰 순간의 겹침)과 이미 닿은 뒤에는 아무것도 하지 않는다.
	if (!HasAuthority() || bSpent || !IsLaunched())
	{
		return;
	}

	UAbilitySystemComponent* const TargetASC = FindHostileTarget(OtherActor);
	if (!TargetASC)
	{
		return;
	}
	// 적용 전에 선다 - 효과가 부른 반응이 같은 프레임에 겹침을 또 보내도 두 번 걸리지 않는다.
	bSpent = true;

	const FHitResult ImpactHit = MakeImpactHit(OtherActor, OtherComp, bFromSweep, SweepResult);
	FVBHitSpecValues Values = HitValues;
	const FVector FlightDirection = FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0);
	if (!FlightDirection.IsNearlyZero())
	{
		Values.PushYaw = FlightDirection.Rotation().Yaw;
	}

	if (VBHitSpec::Fill(*Spec.Data.Get(), Values, ImpactHit))
	{
		TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		VB_LOG(Log, "투사체 %s: %s 가 던진 것이 %s 에 맞음 - 피해 %.1f", *GetName(), *GetNameSafe(Thrower.Get()),
			*OtherActor->GetName(), Values.Damage);
	}

	PlayImpactCue(ImpactHit);
	Destroy();
}

void AVBProjectile::HandleWorldStop(const FHitResult& ImpactResult)
{
	if (!HasAuthority() || bSpent)
	{
		return;
	}

	bSpent = true;
	PlayImpactCue(ImpactResult);
	Destroy();
}

FHitResult AVBProjectile::MakeImpactHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, bool bFromSweep,
	const FHitResult& SweepResult) const
{
	if (bFromSweep && !FVector(SweepResult.ImpactPoint).IsNearlyZero())
	{
		return SweepResult;
	}
	// 겹친 채 시작했거나 스윕이 점을 안 줬다 - 투사체 자리와 비행 반대 방향을 닿은 점, 법선으로 쓴다.
	const FVector Location = GetActorLocation();
	const FVector Normal = -Movement->Velocity.GetSafeNormal();
	FHitResult Hit(OtherActor, OtherComp, Location, Normal.IsNearlyZero() ? FVector::UpVector : Normal);
	Hit.TraceStart = Location;
	Hit.TraceEnd = Location;
	return Hit;
}

void AVBProjectile::PlayImpactCue(const FHitResult& Hit)
{
	if (!Config || !Config->ImpactCueTag.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* const ThrowerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Thrower.Get());
	if (!ThrowerASC)
	{
		VB_LOG(Log, "투사체 %s: 던진 쪽이 없어 닿은 자리 큐(%s)를 내지 않는다", *GetName(), *Config->ImpactCueTag.ToString());
		return;
	}

	FGameplayCueParameters CueParams = UGameplayCueFunctionLibrary::MakeGameplayCueParametersFromHitResult(Hit);
	CueParams.Instigator = Thrower.Get();
	CueParams.EffectCauser = this;
	CueParams.SourceObject = Config.Get();
	ThrowerASC->ExecuteGameplayCue(Config->ImpactCueTag, CueParams);
}
