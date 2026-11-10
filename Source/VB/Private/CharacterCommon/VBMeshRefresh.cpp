#include "CharacterCommon/VBMeshRefresh.h"

#include "Animation/AnimClassInterface.h" // IsBasePolicySafe - 클래스의 연결 노드 목록
#include "Components/SkeletalMeshComponent.h"
#include "VB/VB.h"

namespace
{
	struct FRefreshState
	{
		int32 Count = 0;
		EVisibilityBasedAnimTickOption SavedTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
		bool bSavedUpdateRateOptimizations = true;
	};

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FRefreshState>& States()
	{
		static TMap<TWeakObjectPtr<USkeletalMeshComponent>, FRefreshState> Map;
		return Map;
	}

	void PruneDeadEntries()
	{
		for (auto It = States().CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
}

void FVBMeshRefresh::Acquire(USkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if (!Mesh)
	{
		return;
	}

	PruneDeadEntries();
	FRefreshState& State = States().FindOrAdd(Mesh);
	if (State.Count == 0)
	{
		State.SavedTickOption = Mesh->VisibilityBasedAnimTickOption;
		State.bSavedUpdateRateOptimizations = Mesh->bEnableUpdateRateOptimizations;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
	}

	++State.Count;
}

void FVBMeshRefresh::Release(USkeletalMeshComponent* Mesh)
{
	check(IsInGameThread());
	if (!Mesh)
	{
		return;
	}

	FRefreshState* State = States().Find(Mesh);
	if (!State || State->Count <= 0)
	{
		// Acquire 없는 Release 는 아무것도 되돌리지 않는다 - 되돌리면 다른 요청자가 유지 중인 갱신을 꺼 버린다.
		VB_LOG(Warning, "VBMeshRefresh: %s 에 쥔 요청이 없는데 반납이 왔다 - 무시한다", *GetNameSafe(Mesh));
		return;
	}

	--State->Count;
	if (State->Count == 0)
	{
		Mesh->VisibilityBasedAnimTickOption = State->SavedTickOption;
		Mesh->bEnableUpdateRateOptimizations = State->bSavedUpdateRateOptimizations;
		States().Remove(Mesh);
	}
}

int32 FVBMeshRefresh::GetRequestCount(const USkeletalMeshComponent* Mesh)
{
	const FRefreshState* State = Mesh ? States().Find(const_cast<USkeletalMeshComponent*>(Mesh)) : nullptr;
	return State ? State->Count : 0;
}

void FVBMeshRefresh::SetBasePolicy(USkeletalMeshComponent* Mesh, EVisibilityBasedAnimTickOption TickOption, bool bUpdateRateOptimizations)
{
	check(IsInGameThread());
	if (!Mesh)
	{
		return;
	}
	// 두 쓰기 분기보다 앞이다 - Acquire 가 유지되는 동안 저장값으로 들어가면 마지막 Release 가 위험한 값을 되살린다.
	//   적마다 한 줄씩 낸다(한 번으로 줄이지 않음): 잘못된 설정은 시끄러워야 하고, 클래스별 기억은 PIE 를 넘어 남는 상태가 된다.
	if (!IsBasePolicySafe(TickOption, Mesh->GetAnimClass()))
	{
		VB_LOG(Error,
			"%s: 애니메이션 클래스 %s 에 연결 층이 있어 OnlyTickMontagesAndRefreshBonesWhenPlayingMontages 대신 AlwaysTickPose 로 둔다(UE 5.8 크래시, Batch_Perf1 §5)",
			*GetPathNameSafe(Mesh), *GetNameSafe(Mesh->GetAnimClass()));
		TickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	}

	if (FRefreshState* State = States().Find(Mesh); State && State->Count > 0)
	{
		State->SavedTickOption = TickOption;
		State->bSavedUpdateRateOptimizations = bUpdateRateOptimizations;
		return;
	}

	Mesh->VisibilityBasedAnimTickOption = TickOption;
	Mesh->bEnableUpdateRateOptimizations = bUpdateRateOptimizations;
}

bool FVBMeshRefresh::IsBasePolicySafe(EVisibilityBasedAnimTickOption TickOption, const UClass* AnimClass)
{
	if (TickOption != EVisibilityBasedAnimTickOption::OnlyTickMontagesAndRefreshBonesWhenPlayingMontages)
	{
		return true;
	}

	const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimClass);
	return !AnimClassInterface
		|| (AnimClassInterface->GetLinkedAnimGraphNodeProperties().IsEmpty()
			&& AnimClassInterface->GetLinkedAnimLayerNodeProperties().IsEmpty());
}
