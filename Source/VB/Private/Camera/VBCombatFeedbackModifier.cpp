// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Camera/VBCombatFeedbackModifier.h"

#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBFinisherDirectorSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VB/VB.h"

namespace
{
	float Envelope(float Age, float Duration, float Exponent)
	{
		return FMath::IsFinite(Duration) && Duration > SMALL_NUMBER
			? FMath::Pow(FMath::Clamp(1.0f - Age / Duration, 0.0f, 1.0f), Exponent) : 0.0f;
	}

	float FiniteNonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	// 키 없는 외부 곡선도 있다고 센다(검사가 잡게).
	bool HasMaterialEnvelope(const FRuntimeFloatCurve& Envelope)
	{
		return Envelope.ExternalCurve != nullptr || Envelope.GetRichCurveConst()->GetNumKeys() > 0;
	}

	const FName GAmountParameter(TEXT("ParryAmount"));
	const FName GAgeParameter(TEXT("ParryAge"));
}

bool UVBCombatFeedbackModifier::ValidateMaterialEnvelope(const FVBScreenImpactSettings& Settings, FString& OutReason)
{
	if (!HasMaterialEnvelope(Settings.MaterialEnvelope))
	{
		return true;
	}

	const FRichCurve& Curve = *Settings.MaterialEnvelope.GetRichCurveConst();
	if (Curve.GetNumKeys() == 0)
	{
		OutReason = TEXT("외부 곡선에 키가 없다");
		return false;
	}

	if (!FMath::IsFinite(Settings.EffectSeconds) || Settings.EffectSeconds <= 0.0f)
	{
		OutReason = FString::Printf(TEXT("곡선을 펼칠 EffectSeconds 가 %f 다(양수여야 한다)"), Settings.EffectSeconds);
		return false;
	}

	constexpr float Tolerance = 1.0e-3f;
	float Peak = 0.0f;
	auto Sample = [&Curve, &Peak, &OutReason](float Time)
	{
		const float Value = Curve.Eval(Time);
		if (!FMath::IsFinite(Value) || Value < -Tolerance || Value > 1.0f + Tolerance)
		{
			OutReason = FString::Printf(TEXT("t=%.3f 에서 %f - 세로는 0..1 이어야 한다(보간이 넘는 것 포함)"), Time, Value);
			return false;
		}

		Peak = FMath::Max(Peak, Value);
		return true;
	};
	for (auto It = Curve.GetKeyIterator(); It; ++It)
	{
		if (!FMath::IsFinite(It->Time) || It->Time < 0.0f || It->Time > 1.0f)
		{
			OutReason = FString::Printf(TEXT("키 시각 %f - 가로는 0..1 이어야 한다"), It->Time);
			return false;
		}

		if (!Sample(It->Time))
		{
			return false;
		}
	}
	// 키 사이 보간이 넘치는 것도 잡는다.
	constexpr int32 Steps = 64;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		if (!Sample(static_cast<float>(Step) / Steps))
		{
			return false;
		}
	}

	if (Curve.Eval(0.0f) > Tolerance || Curve.Eval(1.0f) > Tolerance)
	{
		OutReason = FString::Printf(TEXT("시작 %f · 끝 %f - 둘 다 0 이어야 한다"), Curve.Eval(0.0f), Curve.Eval(1.0f));
		return false;
	}

	if (Peak <= Tolerance)
	{
		OutReason = TEXT("곡선이 0 에서 오르지 않는다 - 화면 재질이 한 번도 보이지 않는다");
		return false;
	}

	return true;
}

float UVBCombatFeedbackModifier::EvaluateMaterialAmount(float Age) const
{
	if (!HasMaterialEnvelope(ActiveSettings.MaterialEnvelope))
	{
		return ActiveSettings.EdgeStrength * Envelope(Age, ActiveSettings.EffectSeconds, ActiveSettings.DecayExponent);
	}

	const float Normalized = Age / ActiveSettings.EffectSeconds;
	return Normalized >= 1.0f ? 0.0f
		: ActiveSettings.EdgeStrength * FMath::Clamp(ActiveSettings.MaterialEnvelope.GetRichCurveConst()->Eval(FMath::Max(0.0f,
			Normalized)), 0.0f, 1.0f);
}

UVBCombatFeedbackModifier::UVBCombatFeedbackModifier()
{
	AlphaInTime = 0.0f;
	AlphaOutTime = 0.0f;
	DisableModifier(true);
}

UVBCombatFeedbackModifier::FDirectionalKick UVBCombatFeedbackModifier::ComputeDirectionalKick(const FVector& WorldDirection,
	const FRotator& ViewRotation,
	float KickDegrees, float KickForwardFOV)
{
	const FVector Direction = WorldDirection.GetSafeNormal();
	const FRotationMatrix View(ViewRotation);
	FDirectionalKick Kick;
	Kick.Yaw = KickDegrees * FVector::DotProduct(Direction, View.GetUnitAxis(EAxis::Y));
	Kick.Pitch = KickDegrees * FVector::DotProduct(Direction, View.GetUnitAxis(EAxis::Z));
	Kick.FOV = -KickForwardFOV * FVector::DotProduct(Direction, View.GetUnitAxis(EAxis::X));
	return Kick;
}

FVBScreenImpactSettings UVBCombatFeedbackModifier::ScaleSettings(const FVBScreenImpactSettings& Settings, float Scale)
{
	// 모르는 배율(NaN, 음수)은 1 - 연출을 키우거나 뒤집지 않는다. 0 은 화면을 안 흔든다는 유효한 값이다.
	const float Safe = FMath::IsFinite(Scale) && Scale >= 0.0f ? Scale : 1.0f;
	FVBScreenImpactSettings Scaled = Settings;
	Scaled.RotationAmplitude *= Safe;
	Scaled.FOVAmplitude *= Safe;
	Scaled.KickDegrees *= Safe;
	Scaled.KickForwardFOV *= Safe;
	return Scaled;
}

bool UVBCombatFeedbackModifier::StartFeedback(APawn* Pawn, const FVBScreenImpactSettings& Settings,
	const FVector& WorldDirection, const TArray<UNiagaraComponent*>& Effects,
	const UObject* SourceProfile)
{
	UWorld* World = GetWorld();
	APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	if (!World || !IsValid(Pawn) || !PC || !PC->IsLocalController() || PC->GetPawn() != Pawn ||
		!Pawn->IsLocallyControlled() || World->IsPaused() || !IsAllowedViewTarget(Pawn))
	{
		return false;
	}
	// 잘못된 곡선은 진행 중인 사건을 끊기 전에 거절한다.
	FString EnvelopeError;
	if (!ValidateMaterialEnvelope(Settings, EnvelopeError))
	{
		VB_LOG(Error, "CombatFeedback: %s 의 화면 재질 곡선이 잘못돼 화면 층을 내지 않는다 - %s", *GetNameSafe(SourceProfile), *EnvelopeError);
		return false;
	}
	// 교체 규칙. 진행 중인지는 지금 다시 잰다(Tick 전의 죽은 사건이 막지 않게).
	if (IsStillRunning(World->GetRealTimeSeconds() - StartedAt))
	{
		if (Settings.Priority < ActiveSettings.Priority)
		{
			return false;
		}
		// 같은 사건의 연타는 진행 중인 것을 그대로 둔다.
		if (Settings.bKeepPhaseOnRetrigger && SourceProfile && ActiveSource.Get() == SourceProfile &&
			ActivePawn.Get() == Pawn && Settings.Priority == ActiveSettings.Priority)
		{
			return false;
		}
	}

	StopFeedback();

	ActiveSettings = Settings;
	ActiveSettings.CameraSeconds = FiniteNonNegative(Settings.CameraSeconds);
	ActiveSettings.EffectSeconds = FiniteNonNegative(Settings.EffectSeconds);
	ActiveSettings.Frequency = FiniteNonNegative(Settings.Frequency);
	ActiveSettings.DecayExponent = FMath::IsFinite(Settings.DecayExponent) ? FMath::Max(1.0f, Settings.DecayExponent) : 1.0f;
	ActiveSettings.EdgeStrength = FiniteNonNegative(Settings.EdgeStrength);
	ActiveSettings.FOVAmplitude = FMath::IsFinite(Settings.FOVAmplitude) ? Settings.FOVAmplitude : 0.0f;
	ActiveSettings.KickDegrees = FMath::IsFinite(Settings.KickDegrees) ? Settings.KickDegrees : 0.0f;
	ActiveSettings.KickForwardFOV = FMath::IsFinite(Settings.KickForwardFOV) ? Settings.KickForwardFOV : 0.0f;
	if (Settings.RotationAmplitude.ContainsNaN())
	{
		ActiveSettings.RotationAmplitude = FRotator::ZeroRotator;
	}

	if (Settings.PhaseRadians.ContainsNaN())
	{
		ActiveSettings.PhaseRadians = FVector::ZeroVector;
	}
	// 사용자 접근성 배율 - 카메라 움직임(떨림, FOV, 반동)과 화면 섬광.
	const FVBSettingsSaveData& User = UVBSaveGameSubsystem::GetSettingsData(PC);
	ActiveSettings.RotationAmplitude *= User.CameraShakeScale;
	ActiveSettings.FOVAmplitude *= User.CameraShakeScale;
	ActiveSettings.KickDegrees *= User.CameraShakeScale;
	ActiveSettings.KickForwardFOV *= User.CameraShakeScale;
	ActiveSettings.EdgeStrength *= User.ScreenFlashScale;
	ActiveDirection = WorldDirection.ContainsNaN() ? FVector::ZeroVector : WorldDirection.GetSafeNormal();
	ActivePawn = Pawn;
	ActiveSource = SourceProfile;
	ActiveViewTarget = CameraOwner->GetViewTarget();
	StartedAt = World->GetRealTimeSeconds();

	if (EdgeSource != Settings.Material)
	{
		EdgeSource = Settings.Material;
		EdgeInstance = Settings.Material ? UMaterialInstanceDynamic::Create(Settings.Material, this) : nullptr;
	}
	// 다시 쓰는 인스턴스에 지난 사건의 나이가 남지 않게 둘 다 0 시점으로 쓴다.
	if (EdgeInstance)
	{
		EdgeInstance->SetScalarParameterValue(GAmountParameter, EvaluateMaterialAmount(0.0f));
		EdgeInstance->SetScalarParameterValue(GAgeParameter, 0.0f);
	}

	for (UNiagaraComponent* Effect : Effects)
	{
		if (!IsValid(Effect) || Effect->PoolingMethod != ENCPoolMethod::None)
		{
			continue;
		}
		// 큐가 만든 비풀링 컴포넌트만 받는다. 풀로 돌아간 객체의 새 소유자를 약참조로 추측하지 않는다.
		Effect->SetForceSolo(true);
		Effect->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		Effect->SetDesiredAge(0.0f);
		ActiveEffects.Add(Effect);
	}

	CameraOwner->OnEndPlay.AddUniqueDynamic(this, &UVBCombatFeedbackModifier::HandleCameraEndPlay);
	bActive = true;
	EnableModifier();
	return true;
}

bool UVBCombatFeedbackModifier::IsTickable() const
{
	return bActive && !IsTemplate() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
}

bool UVBCombatFeedbackModifier::IsAllowedViewTarget(const APawn* Pawn) const
{
	const UWorld* World = GetWorld();
	const AActor* ViewTarget = CameraOwner ? CameraOwner->GetViewTarget() : nullptr;
	if (!IsValid(Pawn) || !IsValid(ViewTarget))
	{
		return false;
	}

	if (ViewTarget == Pawn)
	{
		return true;
	}

	const UVBFinisherDirectorSubsystem* Director = World ? World->GetSubsystem<UVBFinisherDirectorSubsystem>() : nullptr;
	return Director && Director->IsFeedbackViewTarget(CameraOwner->GetOwningPlayerController(), Pawn, ViewTarget);
}

bool UVBCombatFeedbackModifier::IsStillRunning(double Age) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const APawn* Pawn = ActivePawn.Get();
	if (!bActive || !World || !PC || !IsValid(Pawn) || !PC->IsLocalController() || PC->GetPawn() != Pawn ||
		World->IsPaused() || !IsAllowedViewTarget(Pawn) ||
		(CameraOwner->CameraStyle != NAME_Default && CameraOwner->CameraStyle != NAME_None))
	{
		return false;
	}
	// 넘김을 허락한 사건은 허락된 뷰 사이를 오가도 산다. 아니면 시작 뷰 그대로여야 한다.
	if (!ActiveSettings.bFollowFinisherHandoff &&
		(!ActiveViewTarget.IsValid() || CameraOwner->GetViewTarget() != ActiveViewTarget.Get()))
	{
		return false;
	}

	return FMath::IsFinite(Age) && Age >= 0.0 && Age < FMath::Max(ActiveSettings.CameraSeconds, ActiveSettings.EffectSeconds);
}

TStatId UVBCombatFeedbackModifier::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(VBCombatFeedbackModifier, STATGROUP_Tickables);
}

void UVBCombatFeedbackModifier::BeginDestroy()
{
	// GC 중 Niagara/다른 UObject의 파괴를 새로 요청하지 않는다. camera EndPlay가 gameplay 정리를 맡는다.
	bActive = false;
	SetTickableTickType(ETickableTickType::Never);
	Super::BeginDestroy();
}

void UVBCombatFeedbackModifier::Tick(float DeltaTime)
{
	// Fixed/시네마틱 등에서는 camera modifier 평가 자체가 빠질 수 있다. 자기 MID/FX 정리는 별도로 보장한다.
	const UWorld* World = GetWorld();
	if (!IsStillRunning(World ? World->GetRealTimeSeconds() - StartedAt : -1.0))
	{
		StopFeedback();
	}
}

bool UVBCombatFeedbackModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// DeltaTime은 슬로우가 적용된 게임초다. 화면 충격은 현재 실시간의 경과로 평가한다.
	const UWorld* World = GetWorld();
	const double RealAge = World ? World->GetRealTimeSeconds() - StartedAt : -1.0;
	if (!IsStillRunning(RealAge))
	{
		StopFeedback();
		return false;
	}

	const float Age = static_cast<float>(RealAge);
	const float CameraAmount = Envelope(Age, ActiveSettings.CameraSeconds, ActiveSettings.DecayExponent);
	const float Phase = UE_TWO_PI * ActiveSettings.Frequency * Age;
	const FRotator& Amplitude = ActiveSettings.RotationAmplitude;
	FRotator Offset(
		Amplitude.Pitch * FMath::Cos(Phase + ActiveSettings.PhaseRadians.X),
		Amplitude.Yaw * FMath::Cos(Phase + ActiveSettings.PhaseRadians.Y),
		Amplitude.Roll * FMath::Cos(Phase + ActiveSettings.PhaseRadians.Z));
	float KickFOV = 0.0f;
	if (!ActiveDirection.IsZero() && (ActiveSettings.KickDegrees != 0.0f || ActiveSettings.KickForwardFOV != 0.0f))
	{
		// 지금 카메라 축에 투영한다 - 시작 축을 굳히면 카메라가 돌 때 엉뚱한 쪽으로 민다.
		const FDirectionalKick Kick = ComputeDirectionalKick(ActiveDirection, InOutPOV.Rotation, ActiveSettings.KickDegrees,
			ActiveSettings.KickForwardFOV);
		Offset.Yaw += Kick.Yaw;
		Offset.Pitch += Kick.Pitch;
		KickFOV = Kick.FOV;
	}

	InOutPOV.Rotation += Offset * CameraAmount;
	InOutPOV.FOV += (ActiveSettings.FOVAmplitude + KickFOV) * CameraAmount;

	// 0 인 표본도 재질에 쓰고, 후처리에 거는 것만 건너뛴다.
	const float MaterialAmount = EvaluateMaterialAmount(Age);
	if (EdgeInstance)
	{
		EdgeInstance->SetScalarParameterValue(GAmountParameter, MaterialAmount);
		EdgeInstance->SetScalarParameterValue(GAgeParameter, Age);
		if (MaterialAmount > 0.0f)
		{
			FPostProcessSettings Settings;
			Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, EdgeInstance));
			CameraOwner->AddCachedPPBlend(Settings, 1.0f);
		}
	}

	for (int32 Index = ActiveEffects.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* Effect = ActiveEffects[Index].Get();
		if (!IsValid(Effect) || !Effect->IsActive())
		{
			ActiveEffects.RemoveAtSwap(Index);
			continue;
		}

		if (Age >= ActiveSettings.EffectSeconds)
		{
			ReleaseEffect(Effect);
			ActiveEffects.RemoveAtSwap(Index);
		}
		else
		{
			Effect->SetDesiredAge(Age);
		}
	}

	return false; // 다른 카메라 modifier의 평가를 막지 않는다.
}

void UVBCombatFeedbackModifier::StopFeedback()
{
#if !UE_BUILD_SHIPPING
	if (bActive)
	{
		UWorld* World = GetWorld();
		APlayerController* PC = CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
		APawn* Pawn = ActivePawn.Get();
		VB_LOG(Log, "CombatFeedbackEnd: age=%.6f limit=%.6f priority=%d style=%s paused=%d local=%d pawn=%d view=%d ownedFX=%d",
			World ? World->GetRealTimeSeconds() - StartedAt : -1.0f,
			FMath::Max(ActiveSettings.CameraSeconds, ActiveSettings.EffectSeconds), ActiveSettings.Priority,
			CameraOwner ? *CameraOwner->CameraStyle.ToString() : TEXT("None"),
			World && World->IsPaused(), PC && PC->IsLocalController(),
			PC && IsValid(Pawn) && PC->GetPawn() == Pawn,
			CameraOwner && ActiveViewTarget.IsValid() && CameraOwner->GetViewTarget() == ActiveViewTarget.Get(), ActiveEffects.Num());
	}
#endif
	bActive = false;
	if (EdgeInstance)
	{
		EdgeInstance->SetScalarParameterValue(GAmountParameter, 0.0f);
	}

	for (const TWeakObjectPtr<UNiagaraComponent>& WeakEffect : ActiveEffects)
	{
		if (UNiagaraComponent* Effect = WeakEffect.Get())
		{
			ReleaseEffect(Effect);
		}
	}

	ActiveEffects.Reset();
	ActivePawn.Reset();
	ActiveSource.Reset();
	ActiveSettings = FVBScreenImpactSettings();
	ActiveViewTarget.Reset();
	ActiveDirection = FVector::ZeroVector;
	DisableModifier(true);
}

void UVBCombatFeedbackModifier::ReleaseEffect(UNiagaraComponent* Effect)
{
	// 지우지 않고 보통 시계로 돌려준다 - 일회 auto-destroy 라 스스로 사라지고, 일찍 지우면 입자가 날다가 잘린다.
	Effect->SetAgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime);
}

void UVBCombatFeedbackModifier::HandleCameraEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	StopFeedback();
}
