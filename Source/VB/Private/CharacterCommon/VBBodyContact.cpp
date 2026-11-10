// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBBodyContact.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/HitResult.h"
#include "GameFramework/Character.h"
#include "GameplayEffectTypes.h"
#include "PhysicsEngine/BodyInstance.h" // 맞은 부위 바디의 중심 - 깊이 박힌 날의 표면 찾기
#include "VB/VB.h"

FVector VBBodyContact::ComputeBladeSide(const FVector& BladeLocation, const FVector& BodyBase, float HalfHeight, const FVector& Fallback)
{
	const float Z = FMath::Clamp(BladeLocation.Z, BodyBase.Z - HalfHeight, BodyBase.Z + HalfHeight);
	FVector Side = BladeLocation - FVector(BodyBase.X, BodyBase.Y, Z);
	if (Side.SizeSquared() < 1.0f)
	{
		Side = Fallback; // 날이 축 위(캐릭터 한가운데) - 어느 쪽인지 날 위치로는 모른다
	}

	Side = Side.GetSafeNormal();
	return Side.IsNearlyZero() ? FVector::ForwardVector : Side;
}

USkeletalMeshComponent* VBBodyContact::FindBodyMesh(AActor* Victim, UPrimitiveComponent* HitComponent)
{
	// 캐릭터는 GetMesh() - MetaHuman 처럼 메시가 여럿이어도 몸통 뼈대를 가진 메시다.
	USkeletalMeshComponent* Mesh = nullptr;
	if (const ACharacter* Character = Cast<ACharacter>(Victim))
	{
		Mesh = Character->GetMesh();
	}

	if (!Mesh)
	{
		Mesh = Cast<USkeletalMeshComponent>(HitComponent);
	}

	if (!Mesh && Victim)
	{
		Mesh = Victim->FindComponentByClass<USkeletalMeshComponent>();
	}

	return (Mesh && Mesh->GetSkeletalMeshAsset()) ? Mesh : nullptr;
}

bool VBBodyContact::RefineOntoBody(FHitResult& Hit, const FVector& SourceLocation)
{
	AActor* Victim = Hit.GetActor();
	if (!Victim)
	{
		return false;
	}

	float HalfHeight = 90.0f;
	if (const ACharacter* Character = Cast<ACharacter>(Victim))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	const FVector Base = Victim->GetActorLocation();
	// 접촉 순간의 날 가운데. 처음부터 겹친 스윕은 Location 이 TraceStart 와 같다 - 그래도 날이 있던 자리다.
	const FVector Blade = FVector(Hit.Location).IsNearlyZero() ? FVector(Hit.TraceEnd) : FVector(Hit.Location);
	const FVector Side = ComputeBladeSide(Blade, Base, HalfHeight, SourceLocation - Base);
	const FVector AxisPoint(Base.X, Base.Y, FMath::Clamp(Blade.Z, Base.Z - HalfHeight, Base.Z + HalfHeight));
	Hit.ImpactNormal = Side; // 표면을 못 찾아도 날이 있는 쪽은 맞다

	// 축 바깥(날 쪽)에서 축을 향해 바디 메시의 물리 바디를 쏜다 - 첫 표면이 날이 닿은 쪽의 메시 표면이다.
	USkeletalMeshComponent* Mesh = FindBodyMesh(Victim, Hit.GetComponent());
	if (Mesh)
	{
		FHitResult OnBody;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(VBBodyContact), /*bTraceComplex=*/false);
		if (Mesh->LineTraceComponent(OnBody, AxisPoint + Side * ProbeDistance, AxisPoint, Params))
		{
			Hit.ImpactPoint = OnBody.ImpactPoint;
			if (!FVector(OnBody.ImpactNormal).IsNearlyZero())
			{
				Hit.ImpactNormal = FVector(OnBody.ImpactNormal).GetSafeNormal();
			}

			Hit.BoneName = OnBody.BoneName;
			Hit.Component = Mesh;
			return true;
		}
		// 축 탐침이 메시를 빗나갔다(칼끝으로 머리, 내민 팔을 쳤거나 캐릭터가 기운 자세). 아래 두 단계로 메시 표면을 다시 찾는다.
		// 1) 판정이 실제로 맞힌 신체 부위(스윕이 실은 뼈)가 있으면 그 부위에서, 없으면 메시 전체에서 날에 가장 가까운 표면.
		const FName SweptBone = (!Hit.BoneName.IsNone() && Mesh->GetBodyInstance(Hit.BoneName)) ? Hit.BoneName : NAME_None;
		FVector Nearest;
		const float Distance = Mesh->GetClosestPointOnCollision(Blade, Nearest, SweptBone); // 바디 안이면 0, 못 구하면 음수
		if (Distance > 0.0f && Distance <= ProbeDistance)
		{
			Hit.ImpactPoint = Nearest;
			Hit.ImpactNormal = (Blade - Nearest).GetSafeNormal(UE_SMALL_NUMBER, Side);
			Hit.BoneName = SweptBone.IsNone() ? Mesh->FindClosestBone(Nearest, nullptr, 0.0f, /*bRequirePhysicsAsset=*/true) : SweptBone;
			Hit.Component = Mesh;
			return true;
		}
		// 2) 날 자리가 그 부위 안이다(깊이 박힘 - 가장 가까운 점이 자기 자신). 그 부위의 중심을 향해 날이 온 쪽 바깥에서 쏜다.
		const FName InsideBone = !SweptBone.IsNone() ? SweptBone
			: Mesh->FindClosestBone(Blade, nullptr, 0.0f, /*bRequirePhysicsAsset=*/true);
		if (const FBodyInstance* Body = InsideBone.IsNone() ? nullptr : Mesh->GetBodyInstance(InsideBone))
		{
			const FVector Center = Body->GetUnrealWorldTransform().GetLocation();
			if (Mesh->LineTraceComponent(OnBody, Center + Side * ProbeDistance, Center, Params))
			{
				Hit.ImpactPoint = OnBody.ImpactPoint;
				Hit.ImpactNormal = FVector(OnBody.ImpactNormal).IsNearlyZero() ? Side : FVector(OnBody.ImpactNormal).GetSafeNormal();
				Hit.BoneName = OnBody.BoneName;
				Hit.Component = Mesh;
				return true;
			}
		}
		// 셋 다 못 찾았다. 화면에서는 이 타격만 상처가 없는 것으로만 보이므로 로그를 남긴다.
		VB_LOG(Log, "[BodyContact] %s 의 몸 표면을 못 찾았다(가장 가까운 거리 %.1fcm, 뼈 %s) - 날 위치를 접점으로 쓴다",
			*GetNameSafe(Victim), Distance, *InsideBone.ToString());
	}
	// 물리 바디가 없는 메시이거나 표면을 못 찾았다: 날 위치를 접점으로 쓴다(뼈 원점으로 채워진 겹침 접점보다 낫다).
	Hit.ImpactPoint = Blade;
	return false;
}

bool VBBodyContact::ResolveCuePoint(const FGameplayCueParameters& Parameters, FVector& OutPoint)
{
	// bBlockingHit 은 보지 않는다: 판정 창은 겹침 쓸기라 정상 접촉도 false 다.
	if (const FHitResult* ContextHit = Parameters.EffectContext.GetHitResult(); ContextHit
		&& !FVector(ContextHit->ImpactPoint).IsNearlyZero())
	{
		FHitResult Contact = *ContextHit;
		const AActor* Source = Parameters.GetInstigator();
		RefineOntoBody(Contact, Source ? Source->GetActorLocation() : FVector(Contact.TraceStart));
		// 날 위치도 비어 있던 접점(위치 없는 합성 HitResult)은 원래 접점을 쓴다.
		OutPoint = FVector(Contact.ImpactPoint).IsNearlyZero() ? FVector(ContextHit->ImpactPoint) : FVector(Contact.ImpactPoint);
		return true;
	}

	if (!FVector(Parameters.Location).IsNearlyZero())
	{
		OutPoint = FVector(Parameters.Location);
		return true;
	}

	return false;
}
