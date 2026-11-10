// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "Settings/VBOptionTypes.h"
#include "VBGC_MeleeImpact.generated.h"

class UMaterialInterface;
class UVBWeaponHitVisuals;
struct FVBStrikeVisualRow;
enum class EVBHitImpact : uint8;
struct FGameplayEffectContextHandle;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;
class USoundConcurrency;
struct FHitResult;

/**
 * 근접 타격 접점의 좌표틀. +X = 맞은 면의 법선(공격자 쪽), +Z = 날이 지나간 방향(법선에 직교화).
 * 이펙트와 tools/vfx-lab 이 같은 축을 쓰므로 이 약속을 바꾸면 둘이 갈린다.
 */
struct VB_API FVBMeleeImpactFrame
{
	FVector Location = FVector::ZeroVector;
	FVector Normal = FVector::ForwardVector;
	// 날의 이동 방향 원본(직교화 전). 이펙트가 들어온 방향으로 쓴다.
	FVector SwingDirection = FVector::UpVector;
	// 회전용 +Z. SwingDirection 을 법선에 직교화한 것.
	FVector SwingAxis = FVector::UpVector;
	// 문맥에 HitResult 가 있었나. 없으면 위치, 방향이 전부 대체값이다.
	bool bFromHitResult = false;

	FQuat ToQuat() const;
};

/**
 * 근접 타격 접점 이펙트 큐. 맞은 자리에서 맞은 면 쪽으로, 날이 지나간 방향을 알고 터진다.
 * 상태 없음(CDO 로 돈다). 튜너블과 큐 태그는 BP 자식이 든다(네이티브 생성자에 태그를 두면 등록이 빠진다).
 */
UCLASS()
class VB_API UVBGC_MeleeImpact : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	// 접점 좌표틀을 정한다(순수 함수). 날 이동 방향 = Hit.TraceEnd - Hit.TraceStart(원격 클라도 같은 값을 받는다).
	static FVBMeleeImpactFrame ResolveImpactFrame(const FHitResult* Hit, const FVector& TargetLocation,
		const FVector& SourceLocation, float SurfaceOffset);

	// 옵션 "피 표현" 을 큐 색 묶음에 겹친다(순수 함수). 모르는 값은 보통으로 떨어진다.
	static TMap<FName, FLinearColor> ResolveBloodColors(EVBBloodDisplay Mode, const TMap<FName, FLinearColor>& CueColors,
		const TMap<FName, FLinearColor>& AlternateColors);

	// 피 입자, 상처, 바닥 자국을 내는가. 끄기만 false.
	static bool ShowsBlood(EVBBloodDisplay Mode);

	// 이 타격을 낸 무기의 접점 이펙트(문맥의 SourceObject = 무기 정의).
	static const UVBWeaponHitVisuals* ResolveHitVisuals(const FGameplayEffectContextHandle& Context);

	// 이번 타격에 낼 층.
	struct FLayers
	{
		UNiagaraSystem* Ripple = nullptr;
		UNiagaraSystem* CutLine = nullptr;
		UNiagaraSystem* Blood = nullptr;
		// 때린 부위가 맞는 부위 이펙트 줄(무기 자산 안을 가리킨다).
		TArray<const FVBStrikeVisualRow*, TInlineAllocator<2>> Strikes;

		bool HasAny() const { return Ripple || CutLine || Blood || !Strikes.IsEmpty(); }
	};
	// 무기 층으로 낼 것이 있을 때만 무기 모습, 아니면 큐 자신의 모습.
	static bool UsesWeaponLayers(const UVBWeaponHitVisuals* Visuals, const FLayers& Layers) { return Visuals && Layers.HasAny(); }
	static FLayers PlanLayers(const UVBWeaponHitVisuals* Visuals, bool bShowBlood, bool bOnBody, const FGameplayTag& StrikeShape,
		EVBHitImpact Impact);

	// 접점에 스폰할 이펙트. 비우면 소리만 낸다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact")
	TObjectPtr<UNiagaraSystem> ImpactSystem;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact", meta = (ClampMin = "0.01"))
	float EffectScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact", meta = (ClampMin = "0", Units = "cm"))
	float SurfaceOffset = 5.0f;

	// Hit Velocity 로 전달하는 날 속도(cm/s).
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact", meta = (ClampMin = "0", Units = "CentimetersPerSecond"))
	float SwingSpeed = 600.0f;

	// 이펙트 User 파라미터 이름. 이름이 어긋나면 Niagara 는 조용히 기본값(0)을 쓴다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	FName HitNormalParameter = TEXT("User.Hit Normal");

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	FName HitDirectionParameter = TEXT("User.Hit Direction");

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	FName HitVelocityParameter = TEXT("User.Hit Velocity");

	// 스폰마다 넣는 상수 파라미터. 풀에서 돌아온 컴포넌트가 앞 값을 들고 있어 매번 다시 넣는다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	TMap<FName, float> FloatParameters;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	TMap<FName, FLinearColor> ColorParameters;

	// 피 입자 Spawn Probability 파라미터. 피 표현 "끄기" 면 0, 아니면 1.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Parameters")
	FName BloodSpawnParameter = TEXT("User.Blood Spawn");

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Sound")
	TObjectPtr<USoundBase> ImpactSound;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Sound")
	TObjectPtr<USoundAttenuation> SoundAttenuation;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Sound")
	TObjectPtr<USoundConcurrency> SoundConcurrency;

	// 맞은 부위의 상처. 접점에 가장 가까운 뼈에 붙여 스폰해 메시를 따라간다. 비우면 안 남긴다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Wound")
	TObjectPtr<UNiagaraSystem> WoundSystem;

	// 메시 표면에서 띄우는 거리(cm).
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Wound", meta = (ClampMin = "0", Units = "cm"))
	float WoundSurfaceOffset = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Wound", meta = (ClampMin = "0.01"))
	float WoundScale = 1.0f;

	// 바닥 핏자국 데칼 후보(무작위 하나, 무작위 회전). 비우면 안 남긴다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground")
	TArray<TObjectPtr<UMaterialInterface>> GroundDecalMaterials;

	// 데칼 반지름 범위(cm).
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground")
	FVector2D GroundDecalRadiusRange = FVector2D(35.0, 65.0);

	// 데칼 투영 깊이(cm).
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground", meta = (ClampMin = "1", Units = "cm"))
	float GroundDecalDepth = 30.0f;

	// 접점에서 아래로 바닥을 찾는 거리(cm). 더 멀면 남기지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground", meta = (ClampMin = "0", Units = "cm"))
	float GroundTraceDistance = 250.0f;

	// 남는 시간(초)과 그 끝의 사라지는 시간(초).
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground", meta = (ClampMin = "0", Units = "s"))
	float GroundDecalLifetime = 20.0f;

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground", meta = (ClampMin = "0", Units = "s"))
	float GroundDecalFadeDuration = 3.0f;

	// 월드에 동시에 남는 자국 상한(넘으면 오래된 것부터 지운다). 0 = 상한 없음.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground", meta = (ClampMin = "0"))
	int32 MaxGroundDecals = 40;

	// 피 표현 "다른 색" 바닥 자국의 색 파라미터. 텍스처가 붉게 구워져 있어 TintReplace 로 붉은 색을 버린다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground")
	FName DecalTintParameter = TEXT("Tint");

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Ground")
	FName DecalTintReplaceParameter = TEXT("TintReplace");

	UPROPERTY(EditDefaultsOnly, Category = "VB|Impact|Wound", meta = (ClampMin = "0"))
	int32 MaxWounds = 24;

	// 카메라 반응은 여기 두지 않는다(접촉마다 돈다). 화면 흔들림은 스윙당 한 번 UVBGC_SwingImpactShake 가 낸다.

private:
	// 무기 층이 있으면 그것을, 없으면 큐 자신의 이펙트를 낸다.
	void SpawnImpactEffect(UWorld& World, const FGameplayEffectContextHandle& Context, const FVBMeleeImpactFrame& Frame,
		const FHitResult* Hit, bool bOnBody, bool bShowBlood, const TMap<FName, FLinearColor>& Colors) const;

	// 큐 자신의 이펙트를 낸다. 피 표현 끄기인데 피 스위치가 없으면 내지 않고 경고한다.
	void SpawnCueEffect(UWorld& World, const FVBMeleeImpactFrame& Frame, bool bShowBlood,
		const TMap<FName, FLinearColor>& Colors) const;

	// 무기 접점 이펙트 층을 낸다(PlanLayers 가 고른 것만).
	void SpawnWeaponLayers(UWorld& World, const UVBWeaponHitVisuals& Visuals, const FLayers& Layers, const FVBMeleeImpactFrame& Frame,
		const FHitResult* Hit, const TMap<FName, FLinearColor>& Colors) const;

	void SpawnGroundDecal(UWorld& World, const FVBMeleeImpactFrame& Frame, const AActor* HitActor, const AActor* Source,
		EVBBloodDisplay BloodDisplay) const;
	void SpawnWound(const FVBMeleeImpactFrame& Frame, const FHitResult& Hit, const TMap<FName, FLinearColor>& Colors) const;
};
