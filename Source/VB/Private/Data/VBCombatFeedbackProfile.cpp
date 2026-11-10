// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBCombatFeedbackProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBCombatFeedbackProfile"

EDataValidationResult UVBCombatFeedbackProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	const bool bAnyWorld = WorldEffects.ContainsByPredicate([](const FVBCombatFeedbackEffect& Effect) { return Effect.System != nullptr; });
	if (!bAnyWorld && SlowMotionSteps.IsEmpty() && !bScreenImpact)
	{
		Context.AddError(LOCTEXT("VBCombatFeedback_Empty",
			"월드 FX·슬로우·화면 층이 모두 비었습니다. 이 프로필을 꽂아도 아무 연출이 나지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}
	// 화면 층을 켜 놓고 길이가 둘 다 0 이면 StartFeedback 이 받았다고 답한 뒤 첫 틱에 끝나 화면에는 아무것도 안 보인다.
	if (bScreenImpact && !(Screen.CameraSeconds > 0.0f) && !(Screen.EffectSeconds > 0.0f))
	{
		Context.AddError(LOCTEXT("VBCombatFeedback_ZeroScreen",
			"화면 층을 켰는데 CameraSeconds 와 EffectSeconds 가 둘 다 0 입니다. 화면 연출이 한 프레임도 안 남습니다."));
		Result = EDataValidationResult::Invalid;
	}
	// 실행(StartFeedback)과 같은 검사.
	if (FString Reason; bScreenImpact && !UVBCombatFeedbackModifier::ValidateMaterialEnvelope(Screen, Reason))
	{
		Context.AddError(FText::Format(LOCTEXT("VBCombatFeedback_Envelope",
			"화면 재질 시간 곡선(MaterialEnvelope)이 잘못됐습니다: {0}. 실행에서 이 화면 층은 거절됩니다."), FText::FromString(Reason)));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
