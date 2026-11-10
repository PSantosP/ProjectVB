// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "CharacterCommon/VBHitFlashComponent.h"

#include "AbilitySystem/VBGameplayEffectContext.h"
#include "CharacterCommon/VBBodyContact.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBHitDistortionConfig.h"
#include "Data/VBHitFlashConfig.h"
#include "Data/VBHitFlashSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffectTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "TimerManager.h"
#include "VB/VB.h"


namespace
{
	// 머티리얼 파라미터 이름. 없는 파라미터 설정은 엔진이 조용히 무시하므로 상수로 한 곳에 둔다.
	const FName GParamFlashStartTime(TEXT("FlashStartTime"));
	const FName GParamFlashDuration(TEXT("FlashDuration"));
	const FName GParamFlashFalloffExp(TEXT("FlashFalloffExp"));
	const FName GParamFlashIntensity(TEXT("FlashIntensity"));
	const FName GParamFlashColor(TEXT("FlashColor"));
	// 예약(림 라이트). 지금 머티리얼은 읽지 않지만 push 해 두면 머티리얼만 고쳐 켤 수 있다.
	const FName GParamFlashFresnelStrength(TEXT("FlashFresnelStrength"));
	const FName GParamFlashFresnelExp(TEXT("FlashFresnelExp"));
	// 국소 플래시(M_VB_HitFlashLocal 만 읽는다. 흰 전신 머티리얼에는 무동작).
	const FName GParamFlashHitLocal(TEXT("FlashHitLocal"));
	const FName GParamFlashLocalRadius(TEXT("FlashLocalRadius"));
	const FName GParamFlashCoreSeconds(TEXT("FlashCoreSeconds"));

	// 플래시 최소 길이(초). 타이머 rate 가 0 이하면 엔진이 SetTimer 를 clear 로 처리해 만료 콜백이 영영 오지 않는다.
	constexpr float GMinFlashDuration = 0.01f;

	// 재무장 하한(실초). 남은 시간이 이보다 짧으면 만료로 본다 - 마이크로초 단위 재무장이 반복되는 것을 막는다.
	constexpr double GRearmEpsilon = 0.001;
}


UVBHitFlashComponent::UVBHitFlashComponent()
{
	// 감쇠는 GPU 가 한다. CPU 는 피격 순간에만 일하므로 tick 이 필요 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

const UVBHitFlashConfig* UVBHitFlashComponent::GetHitFlashConfig()
{
	if (!ResolvedConfig)
	{
		ResolvedConfig = UVBHitFlashSettings::ResolveConfig(HitFlashConfig);
	}

	return ResolvedConfig;
}

const UVBHitDistortionConfig* UVBHitFlashComponent::GetDistortionConfig()
{
	if (!ResolvedDistortionConfig)
	{
		ResolvedDistortionConfig = GetDefault<UVBHitFlashSettings>()->DistortionConfig.LoadSynchronous();
	}

	return ResolvedDistortionConfig;
}

void UVBHitFlashComponent::SpawnDistortion(UWorld& World, const FVector& ContactPoint, const FGameplayCueParameters& Parameters)
{
	if (World.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UVBHitDistortionConfig* Config = GetDistortionConfig();
	if (!Config)
	{
		// 비운 설정 = 왜곡을 끈 것. 끈 줄 모르고 쫓지 않게 월드마다 한 번만 경고한다.
		static TWeakObjectPtr<const UWorld> ReportedWorld;
		if (ReportedWorld.Get() != &World)
		{
			ReportedWorld = &World;
			VB_LOG(Warning, "[HitDistortion] Project Settings > VB Hit Flash > DistortionConfig 가 비었다 - 타격 왜곡 없음");
		}

		return;
	}
	// 등급은 공격이 선언한 값이다(컨텍스트가 복제한다). 저항으로 깎인 반응 등급과 상관없이 같은 칼질은 같은 왜곡이다.
	const FVBGameplayEffectContext* HitFacts = FVBGameplayEffectContext::Get(Parameters.EffectContext);
	const FVBHitDistortionProfile& Profile = Config->ResolveProfile(HitFacts ? HitFacts->GetDeclaredHitImpact() : EVBHitImpact::Light);
	if (!Profile.System)
	{
		return; // 이 등급은 끈 것
	}

	SpawnDistortionEffect(World, Profile, ContactPoint);
}

void UVBHitFlashComponent::SpawnDistortionEffect(UWorld& World, const FVBHitDistortionProfile& Profile, const FVector& Location)
{
	// 값은 켜기 전에 넣는다 - 즉시 스폰 스크립트는 켜는 순간 돈다. 풀에서 돌아온 컴포넌트는 앞 사용자의 값을 들고 있어 매번 넣는다.
	UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		&World, Profile.System, Location, FRotator::ZeroRotator, FVector::OneVector,
		/*bAutoDestroy=*/true, /*bAutoActivate=*/false, ENCPoolMethod::AutoRelease, /*bPreCullCheck=*/true);
	if (!Effect)
	{
		return; // 거리, 가시성 사전 컬링, 또는 그릴 수 없는 프로세스(nullrhi)의 정상 결과
	}

	Effect->SetVariableFloat(UVBHitDistortionConfig::StrengthParameter, Profile.Strength);
	Effect->SetVariableFloat(UVBHitDistortionConfig::RadiusParameter, Profile.RadiusCm);
	Effect->SetVariableFloat(UVBHitDistortionConfig::LifetimeParameter, Profile.Lifetime);
	Effect->SetVariableFloat(UVBHitDistortionConfig::PeakTimeParameter, Profile.PeakTime);
	Effect->SetVariableFloat(UVBHitDistortionConfig::DirectionParameter, Profile.Direction);
	Effect->Activate(true);
}

USkeletalMeshComponent* UVBHitFlashComponent::ResolveTargetMesh() const
{
	// 소유자를 ACharacter 로만 본다 - 플레이어와 적은 공통 VB 베이스가 없다.
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	return OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
}

void UVBHitFlashComponent::TriggerFlash(const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh();
	if (!World || !TargetMesh)
	{
		return;
	}

	// 플래시는 타격 접촉점(VBBodyContact)에 그린다. 접촉점이 없는 피해(처형 처치, 스폰 피해)는 그리지 않는다. 왜곡도 같은 점을 쓴다.
	FVector ContactPoint;
	if (!VBBodyContact::ResolveCuePoint(Parameters, ContactPoint))
	{
		VB_LOG(Log, "[HitFlash] %s cue=%s contact=none - no flash, no warp (damage without a contact point)",
			*GetNameSafe(GetOwner()), *Parameters.OriginalTag.ToString());
		return;
	}

	// 왜곡은 플래시와 따로 켜고 끈다(설정이 둘이다). 플래시 머티리얼이 없어도 왜곡은 난다.
	SpawnDistortion(*World, ContactPoint, Parameters);

	// 태그로 프로필 해석. OriginalTag 는 클라에서도 채워져 타입별 플래시의 분기 키로 쓸 수 있다.
	const UVBHitFlashConfig* Config = GetHitFlashConfig();
	if (!Config)
	{
		// 프로젝트 설정이 비었다 = 플래시를 끈 것. 한 번만 경고한다.
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			VB_LOG(Warning, "[HitFlash] %s 에 쓸 플래시 설정이 없다 - Project Settings > VB Hit Flash > DefaultConfig 가 비었다.",
				*GetNameSafe(GetOwner()));
		}

		return;
	}

	const FVBHitFlashProfile& Profile = Config->ResolveProfile(Parameters.OriginalTag);
	if (!Profile.OverlayMaterial)
	{
		// 머티리얼 미할당 = 이 프로젝트에 아직 플래시 자산이 없다는 뜻. 무연출로 빠진다(코스메틱이라 무해).
		return;
	}

	// 시계는 실시간이다. 셰이더의 View.RealTime 과 같은 축이라 히트스톱 중에도 플래시 길이가 늘지 않는다.
	const double NowRealTime = World->GetRealTimeSeconds();
	const float FlashDuration = FMath::Max(Profile.Duration, GMinFlashDuration);

	// 강도 = 프로필 기본값 x 데미지 커브. 빈 커브거나 피해 없는 큐(처형 타격)면 커브를 쓰지 않는다.
	float FlashIntensity = Profile.Intensity;
	if (const FRichCurve* IntensityCurve = Parameters.RawMagnitude > 0.0f ? Profile.IntensityByDamage.GetRichCurveConst() : nullptr)
	{
		if (IntensityCurve->GetNumKeys() > 0)
		{
			FlashIntensity *= IntensityCurve->Eval(Parameters.RawMagnitude, 1.0f);
		}
	}

	// 플래시는 진행 중인 발광(예고)을 덮는다. 발광은 만료 콜백에서 되살아난다.
	if (!ApplyProfile(Profile, NowRealTime, FlashDuration, FlashIntensity, &ContactPoint))
	{
		return;
	}

	VB_LOG(Log, "[HitFlash] %s cue=%s contact=point radius=%.0fcm mi=%s",
		*GetNameSafe(GetOwner()), *Parameters.OriginalTag.ToString(), Profile.LocalRadiusCm, *GetNameSafe(Profile.OverlayMaterial));

	FlashEndRealTime = NowRealTime + FlashDuration;

	// 실초 값을 게임시간 타이머에 그대로 건다. 팽창 중엔 늦게 울릴 뿐이고 OnFlashExpired 가 실시간으로 다시 확인한다.
	// 같은 핸들 SetTimer 는 기존 것을 clear 하므로 연타해도 최신 요청이 교체한다.
	World->GetTimerManager().SetTimer(FlashTimerHandle, this, &UVBHitFlashComponent::OnFlashExpired,
		FlashDuration, false);
}

void UVBHitFlashComponent::OnFlashExpired()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 타이머는 게임시간 축이라 실시간과 어긋난다. 진실은 실시간이므로 여기서 다시 판정한다.
	const double Remaining = FlashEndRealTime - World->GetRealTimeSeconds();
	if (Remaining > GRearmEpsilon)
	{
		// 아직 안 끝났다(D>1 구간이면 게임시간 타이머가 먼저 온다). 같은 핸들로 잔여만큼 재무장한다.
		World->GetTimerManager().SetTimer(FlashTimerHandle, this, &UVBHitFlashComponent::OnFlashExpired,
			static_cast<float>(Remaining), false);
		return;
	}

	// 만료 확정. 예고 발광이 아직 살아 있으면 원래 시작 시각으로 되살린다 - 셰이더 감쇠가 플래시 전 위치에서 이어진다.
	const double NowRealTime = World->GetRealTimeSeconds();
	if (IsGlowAlive(NowRealTime))
	{
		ApplyProfile(GlowProfile, GlowStartRealTime, static_cast<float>(GlowEndRealTime - GlowStartRealTime), GlowProfile.Intensity,
			nullptr);
		return;
	}

	// 오버레이는 해제하지 않고 강도만 0 으로 누른다 - SetOverlayMaterial 은 렌더 상태를 재생성해 비싸다.
	if (IsValid(FlashMID))
	{
		FlashMID->SetScalarParameterValue(GParamFlashIntensity, 0.0f);
	}
}

bool UVBHitFlashComponent::ApplyProfile(const FVBHitFlashProfile& Profile, double StartRealTime, float Duration, float Intensity,
	const FVector* ContactPoint)
{
	USkeletalMeshComponent* TargetMesh = ResolveTargetMesh();
	if (!TargetMesh || !Profile.OverlayMaterial)
	{
		return false;
	}

	// MID 는 프로필 머티리얼당 1개(오버레이는 전 섹션을 덮는다). 프로필이 바뀔 때만 다시 만든다.
	if (!IsValid(FlashMID) || FlashSourceMaterial != Profile.OverlayMaterial)
	{
		FlashMID = UMaterialInstanceDynamic::Create(Profile.OverlayMaterial, this);
		FlashSourceMaterial = Profile.OverlayMaterial;
	}

	if (!FlashMID)
	{
		return false;
	}

	FlashMID->SetScalarParameterValue(GParamFlashStartTime, static_cast<float>(StartRealTime));
	FlashMID->SetScalarParameterValue(GParamFlashDuration, FMath::Max(Duration, GMinFlashDuration));
	FlashMID->SetScalarParameterValue(GParamFlashFalloffExp, Profile.FalloffExponent);
	FlashMID->SetScalarParameterValue(GParamFlashIntensity, Intensity);
	FlashMID->SetVectorParameterValue(GParamFlashColor, Profile.Color);
	FlashMID->SetScalarParameterValue(GParamFlashFresnelStrength, Profile.FresnelStrength);
	FlashMID->SetScalarParameterValue(GParamFlashFresnelExp, Profile.FresnelExponent);
	// 접촉점이 없으면 반경 0 = 메시 전체(예고 발광 전용). 앞 플래시의 점이 남지 않게 매번 설정한다.
	// 점은 메시 로컬 좌표로 넣어 이동 중에도 메시를 따라간다. 반경도 메시 배율로 나눠 cm 를 유지한다.
	const FTransform& MeshTransform = TargetMesh->GetComponentTransform();
	const FVector LocalPoint = ContactPoint ? MeshTransform.InverseTransformPosition(*ContactPoint) : FVector::ZeroVector;
	const double MeshScale = FMath::Max(MeshTransform.GetMaximumAxisScale(), UE_KINDA_SMALL_NUMBER);
	FlashMID->SetVectorParameterValue(GParamFlashHitLocal, FLinearColor(LocalPoint));
	FlashMID->SetScalarParameterValue(GParamFlashLocalRadius, ContactPoint ? static_cast<float>(Profile.LocalRadiusCm / MeshScale) : 0.0f);
	FlashMID->SetScalarParameterValue(GParamFlashCoreSeconds, Profile.CoreSeconds);

	// 같은 MID 면 다시 걸지 않는다(SetOverlayMaterial 은 렌더 상태를 재생성한다).
	if (TargetMesh->GetOverlayMaterial() != FlashMID)
	{
		TargetMesh->SetOverlayMaterial(FlashMID);
	}

	if (!FMath::IsNearlyEqual(TargetMesh->GetOverlayMaterialMaxDrawDistance(), Profile.MaxDrawDistance))
	{
		TargetMesh->SetOverlayMaterialMaxDrawDistance(Profile.MaxDrawDistance);
	}

	return true;
}

void UVBHitFlashComponent::BeginGlow(const FVBHitFlashProfile& Profile)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double NowRealTime = World->GetRealTimeSeconds();
	GlowProfile = Profile;
	GlowStartRealTime = NowRealTime;
	GlowEndRealTime = NowRealTime + FMath::Max(Profile.Duration, GMinFlashDuration);

	// 플래시가 진행 중이면 플래시가 이긴다. 만료 콜백이 이 발광을 되살린다.
	if (IsFlashAlive(NowRealTime))
	{
		return;
	}

	ApplyProfile(GlowProfile, GlowStartRealTime, static_cast<float>(GlowEndRealTime - GlowStartRealTime), GlowProfile.Intensity,
		nullptr);
}

void UVBHitFlashComponent::EndGlow()
{
	GlowEndRealTime = 0.0;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 플래시가 진행 중이면 그대로 둔다 - 그 플래시의 만료 콜백이 (이제 죽은) 발광 대신 강도 0 을 쓴다.
	if (IsFlashAlive(World->GetRealTimeSeconds()))
	{
		return;
	}

	if (IsValid(FlashMID))
	{
		FlashMID->SetScalarParameterValue(GParamFlashIntensity, 0.0f);
	}
}

void UVBHitFlashComponent::ClearFlash()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlashTimerHandle);
	}

	// 오버레이 해제. 사망 정리 경로에서만 불린다.
	if (USkeletalMeshComponent* TargetMesh = ResolveTargetMesh())
	{
		if (TargetMesh->GetOverlayMaterial() != nullptr)
		{
			TargetMesh->SetOverlayMaterial(nullptr);
		}
	}

	FlashEndRealTime = 0.0;
	GlowEndRealTime = 0.0;
}
