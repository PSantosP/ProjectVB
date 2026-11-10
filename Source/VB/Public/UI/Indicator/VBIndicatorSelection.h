// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UI/Indicator/VBIndicatorTypes.h"

// 한 프레임의 떼어 낸 입력. 번호는 한 목록 스냅샷에서 온, 겹치지 않는 0 아닌 번호여야 한다.
struct FVBIndicatorSelectionCandidate
{
	uint64 Id = 0;
	FVBIndicatorRequest Request;
	double DistanceSquared = 0.0;
	bool bPassesScreenPolicy = true;
};

struct FVBIndicatorSubjectClaim
{
	FGameplayTag Group;
	TWeakObjectPtr<AActor> Subject;
};

// 점유 표시는 정확히 이 Viewer 의 것이고, 위젯, 클래스, 선택 판단보다 먼저 만든다.
struct FVBIndicatorViewerClaims
{
	TWeakObjectPtr<APlayerController> Viewer;
	TArray<FVBIndicatorSubjectClaim> Claims;
};

struct FVBIndicatorSelectionParams
{
	FGameplayTag Group;
	float Radius = TNumericLimits<float>::Max();
	int32 MaxCount = 1;
	EVBIndicatorSortOrder SortOrder = EVBIndicatorSortOrder::Nearest;
	FGameplayTagContainer ExcludeClaimedBy;
	bool bFilterDead = true;
};

namespace VBIndicatorSelection
{
	VB_API bool MatchesViewer(const FVBIndicatorRequest& Request, const APlayerController* Viewer);

	// 화면 밖, 가림, 위젯 없음이어도 점유 표시를 거두지 않는다.
	VB_API FVBIndicatorViewerClaims BuildClaims(TConstArrayView<FVBIndicatorSelectionCandidate> Candidates,
		const FGameplayTagContainer& ClaimingGroups, const APlayerController* Viewer);

	VB_API bool IsClaimed(const FVBIndicatorViewerClaims& Claims, const APlayerController* Viewer,
		const AActor* Subject, const FGameplayTagContainer& ExcludedGroups);

	// 목록을 바꾸거나 월드, GAS 를 묻지 않는다. 반경 0 이하는 무제한이 아니라 반경 0 이다.
	// 거리, 우선순위가 같으면 번호 오름차순(등록 순) - 입력 순서가 섞여 와도 같다.
	VB_API TArray<uint64> Select(TConstArrayView<FVBIndicatorSelectionCandidate> Candidates,
		const FVBIndicatorSelectionParams& Params, const APlayerController* Viewer,
		const FVBIndicatorViewerClaims& Claims, TFunctionRef<bool(const AActor*)> IsDead);
}
