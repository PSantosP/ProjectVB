// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "AbilitySystem/VBHitImpact.h"
#include "Data/VBHitPhysicsConfig.h"
#include "VBHitPhysicsComponent.generated.h"

class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

/**
 * 서 있는 캐릭터의 물리 피격 레이어 - 맞은 뼈 근처 상체가 잠깐 출렁이고 몽타주로 돌아온다. 연출 전용, 전용 서버에서는 동작하지 않는다.
 * 전신 래그돌과 물리 제어가 겹치지 않게 래그돌 시작 때 StopAll 을 부르고, 다른 기능이 시뮬 중인 메시는 건드리지 않는다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBHitPhysicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBHitPhysicsComponent();

	// 소유 액터가 BeginPlay 에서 설정을 전달한다(null 이면 꺼짐). 피격 순간 비용을 피하려 물리 애니 컴포넌트를 여기서 만든다.
	void SetConfig(UVBHitPhysicsConfig* InConfig);

	// 맞은 뼈(모르면 None)와 미는 방향, 실효 등급. 진행 중이면 새 충격으로 덮고 시간을 처음부터 잰다.
	void ApplyHit(FName HitBone, const FVector& PushDirection, EVBHitImpact Impact);

	// 시뮬레이션, 가중치, 충돌을 원래대로 돌린다. 래그돌, 사망이 메시 제어를 가져가기 전에 부른다. 꺼져 있으면 아무것도 안 한다.
	void StopAll();

	bool IsActive() const { return bActive; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	USkeletalMeshComponent* ResolveMesh() const;
	FName ResolveHitBone(const USkeletalMeshComponent& Mesh, FName HitBone) const;
	bool IsBlocked() const;

	UPROPERTY(Transient)
	TObjectPtr<UVBHitPhysicsConfig> Config;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> PhysicalAnimation;

	// 지금 도는 충격의 등급 값 사본. 설정 자산이 바뀌어도 진행 중인 출렁임은 시작한 값으로 끝난다.
	FVBHitPhysicsTier ActiveTier;

	// 루트 바디 없음 경고를 이 컴포넌트에서 이미 냈는가.
	bool bWarnedMissingRootBody = false;
	float Elapsed = 0.0f;
	bool bActive = false;

	// 시뮬레이션하려고 메시 충돌을 물리 포함으로 바꿨으면 원래 값. 끝날 때 돌려놓는다.
	TOptional<ECollisionEnabled::Type> RestoreCollision;
};
