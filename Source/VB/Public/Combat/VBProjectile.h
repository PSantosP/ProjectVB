// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "AbilitySystem/VBHitSpec.h"
#include "VBProjectile.generated.h"

class UAbilitySystemComponent;
class UPrimitiveComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UVBProjectileConfig;

/**
 * 던진 쪽 능력이 만든 타격 스펙을 싣고 날아가 처음 맞은 적대 대상에 한 번 거는 투사체.
 * 피해, 큐는 서버만 낸다. 클라는 복제된 이동과 설정으로 모습만 따라간다.
 */
UCLASS()
class VB_API AVBProjectile : public AActor
{
	GENERATED_BODY()

public:
	AVBProjectile();

	// 서버에서 한 번: 설정, 던진 쪽, 스펙을 받고 Velocity 로 날린다. 클라 사본과 두 번째 호출은 무시한다
	void Launch(const UVBProjectileConfig& InConfig, AActor& InThrower, const FGameplayEffectSpecHandle& InSpec,
		const FVBHitSpecValues& InValues, const FVector& Velocity);

	// 서버에서 Launch 를 통과했는가
	bool IsLaunched() const { return Config != nullptr && Spec.IsValid(); }
	// 이미 무언가에 닿아 더는 피해를 내지 않는가
	bool IsSpent() const { return bSpent; }

	USphereComponent* GetCollision() const { return Collision; }
	UProjectileMovementComponent* GetMovement() const { return Movement; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 복제된 속도를 이동 컴포넌트에 반영한다. 없으면 클라 사본이 다음 위치 갱신까지 멈춰 보인다
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;

protected:
	// 겹침, 멈춤 알림을 묶는다. 스폰 중에 불려 Launch 보다 먼저다.
	virtual void PostInitializeComponents() override;
	// 궤적 큐를 같이 끝낸다. ASC 가 없는 액터라 아무도 대신 거두지 않는다
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 폰과 겹침. 서버에서만 피해로 이어진다.
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// 월드에 막혀 멈춤. 피해 없이 큐를 내고 사라진다.
	UFUNCTION()
	void HandleWorldStop(const FHitResult& ImpactResult);

	UFUNCTION()
	void OnRep_Config();

	// 폰과 겹쳤을 때의 판정과 적용(서버 전용)
	void TryHitPawn(AActor* OtherActor, UPrimitiveComponent* OtherComp, bool bFromSweep, const FHitResult& SweepResult);
	// 피해를 받을 대상의 ASC. 던진 쪽, 같은 편, 사망, ASC 없음이면 null.
	UAbilitySystemComponent* FindHostileTarget(AActor* OtherActor) const;
	// 스윕이 아닌 겹침에도 닿은 점, 법선이 있는 HitResult 를 만든다.
	FHitResult MakeImpactHit(AActor* OtherActor, UPrimitiveComponent* OtherComp, bool bFromSweep, const FHitResult& SweepResult) const;
	// 던진 쪽 ASC 로 닿은 자리 큐를 낸다(복제된다).
	void PlayImpactCue(const FHitResult& Hit);
	// 설정의 메시, 반지름, 중력을 적용하고 궤적 큐를 이 기기에서 낸다. 서버의 Launch 와 클라의 OnRep_Config 가 부른다.
	void ApplyConfig();

	// 충돌 구(루트). 폰과 겹치고 WorldStatic 에 막힌다
	UPROPERTY(VisibleAnywhere, Category="VB|Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category="VB|Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 스윕 비행. 막히면 HandleWorldStop 으로 알린다
	UPROPERTY(VisibleAnywhere, Category="VB|Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

	// 이 투사체의 설정. 클라가 같은 모습을 내도록 복제한다.
	UPROPERTY(ReplicatedUsing=OnRep_Config)
	TObjectPtr<const UVBProjectileConfig> Config;

	// 던진 쪽. 같은 편 판정과 큐의 출처다. 사라지면 더는 피해를 내지 않는다.
	TWeakObjectPtr<AActor> Thrower;

	// 던진 쪽 능력이 만든 타격 스펙. 맞은 순간 VBHitSpec::Fill 로 채워 한 번 건다.
	FGameplayEffectSpecHandle Spec;

	// 발사 때 정한 타격 값. 미는 방향만 맞은 순간의 비행 방향으로 바꾼다.
	FVBHitSpecValues HitValues;

	// 첫 접촉에서 선다. 한 프레임에 겹침이 여럿 와도 피해는 한 번이다.
	bool bSpent = false;
	// 이 기기에서 궤적 큐를 붙였나(붙인 쪽만 뗀다).
	bool bTrailActive = false;
};
