// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "Data/VBHitFlashConfig.h" // FVBHitFlashProfile 을 멤버(GlowProfile)로 든다 - 전방 선언으로는 부족하다
#include "VBHitFlashComponent.generated.h"

class UMaterialInstanceDynamic;
class UVBHitDistortionConfig;
class UMaterialInterface;
class USkeletalMeshComponent;
struct FGameplayCueParameters;
struct FVBHitDistortionProfile;

/**
 * 피격 순간 메시를 발광시키는 오버레이 플래시의 상태를 관리한다. GameplayCue 는 CDO 라 상태를 가질 수 없어 여기에 둔다.
 * 감쇠는 GPU 가 계산하므로 틱이 없다. 대상 메시의 OverlayMaterial 을 교체한다(렌더 상태 재생성).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBHitFlashComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBHitFlashComponent();

	// 플래시 시작과 재시작, 같은 자리의 공간 왜곡. 큐(UVBGC_HitFlash)가 유일한 호출자. 재트리거는 중첩이 아니라 교체다
	void TriggerFlash(const FGameplayCueParameters& Parameters);

	// 오버레이 즉시 해제와 타이머 정리. 적 죽음 연출(UVBEnemyDeathPresentationComponent)이 사망 연출을 시작할 때 호출한다
	void ClearFlash();

	// 지속 발광 시작(적 공격 예고 색). 호출자는 UVBGC_Telegraph. 같은 오버레이라 플래시가 끼어들면 플래시가 이기고 끝나면 발광이 되살아난다(OnFlashExpired)
	void BeginGlow(const FVBHitFlashProfile& Profile);

	// 지속 발광 끝. 오버레이 재생성을 피하려 강도만 0 으로 하고 진행 중인 플래시는 건드리지 않는다
	void EndGlow();

protected:
	// 이 캐릭터만 다르게 할 플래시 DataAsset. 비우면 프로젝트 기본(UVBHitFlashSettings::DefaultConfig)을 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<UVBHitFlashConfig> HitFlashConfig;

	// 쓸 설정. 명시값, 없으면 UVBHitFlashSettings::ResolveConfig. null = 프로젝트가 피격 플래시를 끈 것
	const UVBHitFlashConfig* GetHitFlashConfig();

	// 정해진 왜곡 하나를 낸다(Niagara, 풀링). 시험은 이것만 덮어 요청을 기록한다(nullrhi 에서는 Niagara 가 컴포넌트를 안 만든다)
	virtual void SpawnDistortionEffect(UWorld& World, const FVBHitDistortionProfile& Profile, const FVector& Location);

private:
	// 소유자를 ACharacter 로 보고 GetMesh() 를 돌려준다(구체 VB 클래스에 의존하지 않는다)
	USkeletalMeshComponent* ResolveTargetMesh() const;

	// 프로젝트 설정의 왜곡 데이터(UVBHitFlashSettings::DistortionConfig). 처음 물을 때 풀어 강하게 든다. null = 끈 것
	const UVBHitDistortionConfig* GetDistortionConfig();

	// 접촉점에 왜곡 이펙트 하나를 낸다(풀링, 상태 없음). 등급은 컨텍스트의 선언 등급이다
	void SpawnDistortion(UWorld& World, const FVector& ContactPoint, const FGameplayCueParameters& Parameters);

	// 플래시 창 만료 콜백. 실시간으로 재검증하고 아직이면 잔여만큼 재무장한다. 만료면 살아 있는 발광을 되살리거나 강도를 0 으로 한다
	void OnFlashExpired();

	// 프로필을 오버레이에 심는 유일한 쓰기 경로(MID 생성, 교체 포함). 심었으면 true. StartRealTime = 셰이더 감쇠 기준 시각, ContactPoint 가 null 이면 반경 0(메시 전체, 예고 발광 전용)
	bool ApplyProfile(const FVBHitFlashProfile& Profile, double StartRealTime, float Duration, float Intensity,
		const FVector* ContactPoint);

	bool IsGlowAlive(double NowRealTime) const { return GlowEndRealTime > NowRealTime; }
	bool IsFlashAlive(double NowRealTime) const { return FlashEndRealTime > NowRealTime; }

	// 프로필 머티리얼당 1개. 오버레이는 슬롯 무관하게 전 섹션을 덮는다
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FlashMID;

	// GetHitFlashConfig 가 푼 설정. 프로젝트 기본은 소프트 참조라 여기서 들지 않으면 GC 뒤 매 타격마다 다시 읽힌다
	UPROPERTY(Transient)
	TObjectPtr<const UVBHitFlashConfig> ResolvedConfig;

	UPROPERTY(Transient)
	TObjectPtr<const UVBHitDistortionConfig> ResolvedDistortionConfig;

	// FlashMID 를 만든 원본. 프로필이 바뀌면(태그 오버라이드) MID 를 다시 만들어야 해서 비교용으로 든다
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlashSourceMaterial;

	// 단일 타이머. 같은 핸들로 SetTimer 하면 엔진이 기존 것을 먼저 clear 해서 연타해도 중첩되지 않는다
	FTimerHandle FlashTimerHandle;

	// 플래시가 끝나야 하는 실시각(World->GetRealTimeSeconds 축). 만료 재검증의 기준이다
	double FlashEndRealTime = 0.0;

	// 지속 발광 상태(BeginGlow 에서 EndGlow 까지). 프로필이 OverlayMaterial 참조를 들어 GC 대상이라 UPROPERTY 다
	UPROPERTY()
	FVBHitFlashProfile GlowProfile;
	double GlowStartRealTime = 0.0;
	double GlowEndRealTime = 0.0;
};
