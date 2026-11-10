// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBFinisherConfig.h"

#include "ContextualAnimSceneAsset.h" // GetRoles - 앵커 드롭다운의 원천
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

TArray<FString> UVBFinisherConfig::GetSceneRoleOptions() const
{
	TArray<FString> Options;
	if (InteractionDatabase && InteractionDatabase->Schema)
	{
		for (const FPoseSearchRoledSkeleton& Role : InteractionDatabase->Schema->GetRoledSkeletons())
		{
			Options.Add(Role.Role.ToString());
		}

		return Options;
	}

	if (!Scene)
	{
		// Scene 미할당은 IsDataValid 가 따로 잡는다. 여기서는 빈 목록이 맞는 답이다.
		return Options;
	}

	for (const FName& Role : Scene->GetRoles())
	{
		Options.Add(Role.ToString());
	}

	return Options;
}

namespace
{
	// SceneTime 이 들어 있는 구간의 앞 키 번호를 찾는다. 마지막 키를 지났으면 INDEX_NONE 이다.
	int32 FindCameraSegment(const TArray<FVBFinisherCameraKey>& Keys, float SceneTime)
	{
		for (int32 i = 0; i < Keys.Num() - 1; ++i)
		{
			if (SceneTime < Keys[i + 1].Time)
			{
				return i;
			}
		}

		return INDEX_NONE;
	}

	// 이 구간을 얼마나 왔나. 「어떻게 도달하나」가 이 값의 모양을 정한다.
	float CameraSegmentAlpha(const FVBFinisherCameraKey& A, const FVBFinisherCameraKey& B, float SceneTime, bool& bOutUseSpline)
	{
		const float Span = B.Time - A.Time;
		float Alpha = 0.0f;
		bOutUseSpline = false;
		switch (B.Interp)
		{
		case EVBFinisherCameraInterp::Cut:
			Alpha = 0.0f; // B.Time 에 닿는 순간 다음 구간으로 넘어가면서 값이 바뀐다
			break;

		case EVBFinisherCameraInterp::Hold:
		{
			// (B.Time - BlendTime) 까지 A 에 머물다 B 로 간다. 앞뒤 속도가 없는 이동이라 이 모드만 이징을 건다.
			const float Start = B.Time - B.BlendTime;
			Alpha = (B.BlendTime <= KINDA_SMALL_NUMBER || SceneTime <= Start)
				? 0.0f
				: FMath::SmoothStep(0.0f, 1.0f, (SceneTime - Start) / B.BlendTime);
			break;
		}

		case EVBFinisherCameraInterp::Linear:
			Alpha = (Span <= KINDA_SMALL_NUMBER) ? 1.0f : (SceneTime - A.Time) / Span;
			break;

		case EVBFinisherCameraInterp::Curve:
		default:
			Alpha = (Span <= KINDA_SMALL_NUMBER) ? 1.0f : (SceneTime - A.Time) / Span;
			bOutUseSpline = true;
			break;
		}

		return FMath::Clamp(Alpha, 0.0f, 1.0f);
	}

	// Catmull-Rom 을 에르미트 기저로 펴서 이웃 넷의 가중치를 채운다. 키 간격이 제각각이라 접선은 시간으로 정규화한다.
	// 구간 이징은 걸지 않는다 - 걸면 키마다 속도가 0 이 되어 카메라가 선다.
	void FillSplineWeights(const TArray<FVBFinisherCameraKey>& Keys, int32 Seg, float Alpha, float (&W)[4])
	{
		const FVBFinisherCameraKey& A = Keys[Seg];
		const FVBFinisherCameraKey& B = Keys[Seg + 1];
		const float Span = B.Time - A.Time;
		const float A2 = Alpha * Alpha;
		const float A3 = A2 * Alpha;
		const float H00 = 2.0f * A3 - 3.0f * A2 + 1.0f;
		const float H10 = A3 - 2.0f * A2 + Alpha;
		const float H01 = -2.0f * A3 + 3.0f * A2;
		const float H11 = A3 - A2;

		const bool bHasPrev = Seg > 0;
		const bool bHasNext = Seg + 2 <= Keys.Num() - 1;
		const float Wp = bHasPrev
			? Span / FMath::Max(B.Time - Keys[Seg - 1].Time, KINDA_SMALL_NUMBER)
			: 0.0f;
		const float Wn = bHasNext
			? Span / FMath::Max(Keys[Seg + 2].Time - A.Time, KINDA_SMALL_NUMBER)
			: 0.0f;

		W[0] = -H10 * Wp;
		W[1] = H00 - H11 * Wn;
		W[2] = H01 + H10 * Wp;
		W[3] = H11 * Wn;
	}
}

bool UVBFinisherConfig::EvaluateCameraAt(float SceneTime, FVector& OutRelLocation,
	FVector& OutRelLookAt, float& OutFOV) const
{
	if (CameraKeys.IsEmpty())
	{
		return false;
	}

	// 첫 키 앞은 첫 키를 그대로 쓴다. 그 앞을 보간할 대상이 없다 -
	//   게임플레이 카메라에서 이리로 오는 블렌드는 뷰타깃 전환이 이미 하고 있다.
	const int32 Last = CameraKeys.Num() - 1;
	auto TakeKey = [&](int32 Index)
	{
		const FVBFinisherCameraKey& K = CameraKeys[Index];
		OutRelLocation = K.RelativeLocation;
		OutRelLookAt = K.LookAtOffset;
		OutFOV = K.FOV;
	};

	if (SceneTime <= CameraKeys[0].Time)
	{
		TakeKey(0);
		return true;
	}

	const int32 Seg = FindCameraSegment(CameraKeys, SceneTime);
	if (Seg == INDEX_NONE)
	{
		// 마지막 키를 지났다. 거기 머문다 - 장면이 끝나는 것은 서브시스템이 보고 복귀시킨다.
		TakeKey(Last);
		return true;
	}

	const FVBFinisherCameraKey& A = CameraKeys[Seg];
	const FVBFinisherCameraKey& B = CameraKeys[Seg + 1];
	bool bUseSpline = false;
	const float Alpha = CameraSegmentAlpha(A, B, SceneTime, bUseSpline);

	// 이웃 넷의 가중합 하나로 접는다. 위치, 볼 지점, FOV 에 같은 가중치가 걸리므로 한 번만 낸다.
	//   W[0]=이전 키, W[1]=A, W[2]=B, W[3]=다음 키. 합이 1 이라 평행이동에 안전하다.
	float W[4] = { 0.0f, 1.0f - Alpha, Alpha, 0.0f };

	if (bUseSpline)
	{
		FillSplineWeights(CameraKeys, Seg, Alpha, W);
	}

	// 이웃이 없으면 가중치가 0 이므로 인덱스를 물려도 값에 영향이 없다.
	const FVBFinisherCameraKey& P = CameraKeys[FMath::Max(Seg - 1, 0)];
	const FVBFinisherCameraKey& N = CameraKeys[FMath::Min(Seg + 2, Last)];

	OutRelLocation = W[0] * P.RelativeLocation + W[1] * A.RelativeLocation
		+ W[2] * B.RelativeLocation + W[3] * N.RelativeLocation;
	OutRelLookAt = W[0] * P.LookAtOffset + W[1] * A.LookAtOffset
		+ W[2] * B.LookAtOffset + W[3] * N.LookAtOffset;

	// 커브는 끝점 밖으로 넘칠 수 있다. 화각이 0 이하로 가면 투영이 뒤집히므로 저작 범위로 자른다.
	const float RawFOV = W[0] * P.FOV + W[1] * A.FOV + W[2] * B.FOV + W[3] * N.FOV;
	OutFOV = FMath::Clamp(RawFOV, 5.0f, 170.0f);
	return true;
}

#if WITH_EDITOR
#include "Misc/DataValidation.h" // FDataValidationContext::AddError
// ContextualAnimSceneAsset.h 는 위에서 이미 들였다 - 드롭다운과 검증이 같은 GetRoles 를 쓴다.
#define LOCTEXT_NAMESPACE "VBFinisherConfig"

EDataValidationResult UVBFinisherConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Space.IsValid())
	{
		Context.AddError(LOCTEXT("VBFinisher_Space", "Space 검사 간격·여유·표본 수가 유효하지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}

	if (!CameraCollision.IsValid())
	{
		Context.AddError(LOCTEXT("VBFinisher_CameraCollision", "CameraCollision 반경·거리와 보정 범위가 유효하지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}

	// MMI로 이관한 설정은 DB가 정본이고, Scene은 기존 저작 미리보기용으로 남길 수 있다.
	if (!Scene && !InteractionDatabase)
	{
		Context.AddError(LOCTEXT("VBFinisher_NoScene",
			"Scene과 InteractionDatabase가 모두 비어 있습니다. 처형 안무를 연결하세요."));
		Result = EDataValidationResult::Invalid;
	}
	else if (!InteractionDatabase && Scene && Scene->GetNumSections() == 0)
	{
		// 씬 자산은 있는데 섹션이 없으면 재생할 것이 없다. 자산만 걸어 두고 채우지 않은 상태가 이렇게 보인다.
		Context.AddError(LOCTEXT("VBFinisher_EmptyScene",
			"Scene 에 섹션이 하나도 없습니다. 씬 자산을 열어 역할과 클립을 채우세요."));
		Result = EDataValidationResult::Invalid;
	}

	if (InteractionDatabase && (!InteractionDatabase->Schema || InteractionDatabase->GetNumAnimationAssets() != 1))
	{
		Context.AddError(LOCTEXT("VBFinisher_InvalidInteraction", "InteractionDatabase에는 역할 스키마와 처형 한 벌이 있어야 합니다."));
		Result = EDataValidationResult::Invalid;
	}

	// 카메라 키는 시각 오름차순이어야 한다. 뒤섞이면 카메라가 앞뒤로 튄다.
	for (int32 Index = 1; Index < CameraKeys.Num(); ++Index)
	{
		if (CameraKeys[Index].Time < CameraKeys[Index - 1].Time)
		{
			Context.AddError(FText::Format(LOCTEXT("VBFinisher_CameraKeyOrder",
				"CameraKeys[{0}] 의 Time({1})이 앞 키({2})보다 빠릅니다. 시각 오름차순으로 정렬하세요."),
				FText::AsNumber(Index), FText::AsNumber(CameraKeys[Index].Time), FText::AsNumber(CameraKeys[Index - 1].Time)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 슬로우 구간도 오름차순. 겹침은 서브시스템이 min-wins 로 해소하지만 순서 뒤집힘은 저작 실수다.
	for (int32 Index = 1; Index < TimeDilationSegments.Num(); ++Index)
	{
		if (TimeDilationSegments[Index].Time < TimeDilationSegments[Index - 1].Time)
		{
			Context.AddError(FText::Format(LOCTEXT("VBFinisher_SlowOrder",
				"TimeDilationSegments[{0}] 의 Time 이 앞 구간보다 빠릅니다. 시각 오름차순으로 정렬하세요."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}
	// 길이 0 은 PushTimed 가 기한 없는 Push 로 바꿔 처형이 끝날 때까지 느려진다. 저작 시점에 막는다.
	for (int32 Index = 0; Index < TimeDilationSegments.Num(); ++Index)
	{
		if (!(TimeDilationSegments[Index].RealDuration > 0.0f))
		{
			Context.AddError(FText::Format(LOCTEXT("VBFinisher_SlowDuration",
				"TimeDilationSegments[{0}] 의 RealDuration 이 0 이하입니다. 0 이면 처형이 끝날 때까지 느려집니다."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 앵커 역할이 씬에 실제로 있는가. 씬을 바꾸면 옛 이름이 남아 카메라가 안 나온다.
	if ((Scene || InteractionDatabase) && !CameraKeys.IsEmpty())
	{
		if (!GetSceneRoleOptions().Contains(CameraAnchorRole.ToString()))
		{
			Context.AddError(FText::Format(LOCTEXT("VBFinisher_BadAnchor",
				"CameraAnchorRole('{0}')이 Scene 의 역할 목록에 없습니다. 카메라가 기준을 못 찾아 "
				"연출이 통째로 안 나옵니다. 드롭다운에서 다시 고르세요."),
				FText::FromName(CameraAnchorRole)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 「머물다 이동」의 블렌드 시간이 앞 키와의 간격보다 길면 보간이 앞 키 이전에서 시작한다(EvaluateCameraAt 계약).
	// 다른 방식은 이 값을 안 쓰므로 검사하지 않는다.
	for (int32 Index = 1; Index < CameraKeys.Num(); ++Index)
	{
		if (CameraKeys[Index].Interp != EVBFinisherCameraInterp::Hold)
		{
			continue;
		}

		const float Gap = CameraKeys[Index].Time - CameraKeys[Index - 1].Time;
		if (CameraKeys[Index].BlendTime > Gap + KINDA_SMALL_NUMBER)
		{
			Context.AddError(FText::Format(LOCTEXT("VBFinisher_BlendTooLong",
				"CameraKeys[{0}] 의 BlendTime({1})이 앞 키와의 간격({2})보다 깁니다. "
				"보간 시작점이 앞 키보다 이전이 되어 어느 키에서 출발하는지가 정해지지 않습니다."),
				FText::AsNumber(Index), FText::AsNumber(CameraKeys[Index].BlendTime), FText::AsNumber(Gap)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
