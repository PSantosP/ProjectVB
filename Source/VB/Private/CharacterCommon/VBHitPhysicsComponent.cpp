// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBHitPhysicsComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h" // GetNameSafe on the mesh's physics asset in the missing-body warning
#include "VB/VB.h"

#if !UE_BUILD_SHIPPING
// 개발용 A/B 스위치 - 몽타주 흔들림과 물리 충격이 같은 프레임이라 어느 쪽이 떨리는지 가르려고 둔다. 0 이면 새 피격 물리만 건너뛰고 몽타주는 그대로 재생한다.
static TAutoConsoleVariable<int32> CVarHitPhysicsEnabled(
	TEXT("vb.Combat.HitPhysics"), 1,
	TEXT("Dev test: 0 = enemies skip hit physics (upper-body physical push); the hit react montage still plays. 1 = on (default)."),
	ECVF_Cheat);
#endif

UVBHitPhysicsComponent::UVBHitPhysicsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UVBHitPhysicsComponent::SetConfig(UVBHitPhysicsConfig* InConfig)
{
	Config = InConfig;
	USkeletalMeshComponent* Mesh = ResolveMesh();
	if (!Config || !Mesh || PhysicalAnimation || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	PhysicalAnimation = NewObject<UPhysicalAnimationComponent>(GetOwner(), TEXT("HitPhysicalAnimation"));
	PhysicalAnimation->RegisterComponent();
	// 엔진 컴포넌트는 매 프레임 틱한다(PhysicalAnimationComponent.cpp 생성자). 출렁이는 동안만 켠다 - 적 수만큼 늘 도는 비용을 없앤다.
	PhysicalAnimation->SetComponentTickEnabled(false);
}

USkeletalMeshComponent* UVBHitPhysicsComponent::ResolveMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

FName UVBHitPhysicsComponent::ResolveHitBone(const USkeletalMeshComponent& Mesh, FName HitBone) const
{
	const FName Root = Config->SimulateRootBone;
	const bool bInRange = !HitBone.IsNone() && Mesh.GetBodyInstance(HitBone) &&
		(HitBone == Root || Mesh.BoneIsChildOf(HitBone, Root));
	return bInRange ? HitBone : Config->FallbackHitBone;
}

bool UVBHitPhysicsComponent::IsBlocked() const
{
	if (Config->BlockedTags.IsEmpty())
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	return ASC && ASC->HasAnyMatchingGameplayTags(Config->BlockedTags);
}

void UVBHitPhysicsComponent::ApplyHit(FName HitBone, const FVector& PushDirection, EVBHitImpact Impact)
{
#if !UE_BUILD_SHIPPING
	if (CVarHitPhysicsEnabled.GetValueOnGameThread() == 0)
	{
		VB_LOG(Log, "%s: 피격 물리 건너뜀 - vb.Combat.HitPhysics 0", *GetNameSafe(GetOwner()));
		return;
	}
#endif
	if (!Config || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// 넘어짐, 띄움은 전신을 몽타주가 점유한다 - 앞 타의 출렁임을 끄는 신호로만 쓴다.
	if (Impact >= EVBHitImpact::Knockdown)
	{
		StopAll();
		return;
	}

	if (PushDirection.IsNearlyZero())
	{
		return;
	}

	const FVBHitPhysicsTier* Tier = Config->FindActiveTier(Impact);
	USkeletalMeshComponent* Mesh = ResolveMesh();
	const FName Root = Config->SimulateRootBone;
	if (Tier && Mesh && !Mesh->GetBodyInstance(Root) && !bWarnedMissingRootBody)
	{
		// SimulateRootBone 이 body 없는 뼈를 가리키면 피격 물리가 꺼진다 - 원인이 데이터라 컴포넌트당 한 번만 경고한다.
		bWarnedMissingRootBody = true;
		VB_LOG(Warning, "%s: 피격 물리 꺼짐 - SimulateRootBone %s 에 물리 바디가 없다 (물리 자산 %s). 설정 %s 의 뼈를 바디가 있는 뼈로 바꾼다",
			*GetNameSafe(GetOwner()), *Root.ToString(), *GetNameSafe(Mesh->GetPhysicsAsset()), *GetNameSafe(Config));
	}

	if (!Tier || !Mesh || !PhysicalAnimation || !Mesh->GetBodyInstance(Root) || IsBlocked())
	{
		return;
	}
	// 다른 소유자(래그돌)가 이미 이 메시를 시뮬레이션 중이면 손대지 않는다 - 끝날 때 그쪽의 시뮬레이션까지 끄게 된다.
	if (!bActive && Mesh->IsSimulatingPhysics(Root))
	{
		return;
	}

	if (!bActive)
	{
		const ECollisionEnabled::Type Collision = Mesh->GetCollisionEnabled();
		if (!CollisionEnabledHasPhysics(Collision))
		{
			RestoreCollision = Collision;
			Mesh->SetCollisionEnabled(
				Collision == ECollisionEnabled::NoCollision ?ECollisionEnabled::PhysicsOnly : ECollisionEnabled::QueryAndPhysics);
		}

		PhysicalAnimation->SetSkeletalMeshComponent(Mesh);
		PhysicalAnimation->SetComponentTickEnabled(true);
		PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Root, Config->PhysicalAnimation, /*bIncludeSelf=*/true);
		Mesh->SetAllBodiesBelowSimulatePhysics(Root, true, /*bIncludeSelf=*/true);
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(Root, 0.0f, /*bSkipCustomPhysicsType=*/false, /*bIncludeSelf=*/true);
		bActive = true;
		SetComponentTickEnabled(true);
	}

	ActiveTier = *Tier;
	Elapsed = 0.0f;
	// 성공 경로도 남겨 "돌았지만 약하다" 와 "안 돌았다" 를 구별한다.
	VB_LOG(Log, "%s: 피격 물리 - 등급 %d, 충격 %.0f, 최대 섞기 %.2f (오름 %.2f초, 내림 %.2f초), 맞은 뼈 %s", *GetNameSafe(GetOwner()),
		static_cast<int32>(Impact), Tier->ImpulseSpeed, Tier->MaxBlendWeight, Tier->RiseSeconds, Tier->FallSeconds,
		*ResolveHitBone(*Mesh, HitBone).ToString());
	Mesh->AddImpulse(PushDirection.GetSafeNormal() * Tier->ImpulseSpeed, ResolveHitBone(*Mesh, HitBone), /*bVelChange=*/true);
}

void UVBHitPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USkeletalMeshComponent* Mesh = ResolveMesh();
	if (!bActive || !Mesh || !Config)
	{
		StopAll();
		return;
	}

	Elapsed += DeltaTime;
	if (Elapsed >= ActiveTier.TotalSeconds() || IsBlocked())
	{
		StopAll();
		return;
	}

	Mesh->SetAllBodiesBelowPhysicsBlendWeight(Config->SimulateRootBone, ActiveTier.EvaluateBlend(Elapsed), false, true);
}

void UVBHitPhysicsComponent::StopAll()
{
	SetComponentTickEnabled(false);
	if (!bActive)
	{
		return;
	}

	bActive = false;
	if (PhysicalAnimation)
	{
		// 피격 모터를 즉시 해제해 다음 래그돌의 관절을 붙잡지 않게 한다.
		PhysicalAnimation->SetSkeletalMeshComponent(nullptr);
		PhysicalAnimation->SetComponentTickEnabled(false);
	}

	USkeletalMeshComponent* Mesh = ResolveMesh();
	if (!Mesh || !Config)
	{
		RestoreCollision.Reset();
		return;
	}

	const FName Root = Config->SimulateRootBone;
	Mesh->SetAllBodiesBelowSimulatePhysics(Root, false, true);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(Root, 0.0f, false, true);
	if (RestoreCollision.IsSet())
	{
		Mesh->SetCollisionEnabled(RestoreCollision.GetValue());
		RestoreCollision.Reset();
	}
}

void UVBHitPhysicsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAll();
	Super::EndPlay(EndPlayReason);
}
