// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBComboSelectionPolicy.h"

#include "Algo/Count.h"

namespace
{
	using FIndexList = TArray<int32, TInlineAllocator<8>>;

	FIndexList Collect(TConstArrayView<FVBComboCandidate> Candidates, TFunctionRef<bool(const FVBComboCandidate&)> Accept)
	{
		FIndexList Indices;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Accept(Candidates[Index]))
			{
				Indices.Add(Index);
			}
		}

		return Indices;
	}

	// 첫 자격 변형이 이긴다(Sprint, 일반 공격 - 헤더 참고)
	FVBComboChoice Choose(const FIndexList& Indices, bool bInReach, EVBComboChoiceReason Reason, EVBComboRole Band)
	{
		FVBComboChoice Choice;
		Choice.Variant = Indices.IsEmpty() ? 0 : Indices[0];
		Choice.bTargetInReach = bInReach;
		Choice.Reason = Reason;
		Choice.Band = Band;
		Choice.Qualified = Indices.Num();
		return Choice;
	}

	// 구간 목록은 돌아간다 - 마지막 시작 다음의 첫 자격 변형, 끝이면 처음으로. 마지막 시작만 기억하므로
	//   누름 사이에 표적이 움직여 자격 집합이 바뀌어도 순서가 깨지지 않는다.
	FVBComboChoice ChooseRotating(const FIndexList& Indices, bool bInReach, EVBComboChoiceReason Reason, EVBComboRole Band,
		EVBComboRole ListRole, int32 LastStart)
	{
		FVBComboChoice Choice = Choose(Indices, bInReach, Reason, Band);
		Choice.RotationRole = ListRole;
		Choice.RotationAfter = LastStart;
		for (const int32 Index : Indices)
		{
			if (Index > LastStart)
			{
				Choice.Variant = Index;
				break;
			}
		}

		return Choice;
	}
}

int32 FVBComboStartMemory::RotationSlot(EVBComboRole Role)
{
	switch (Role)
	{
	case EVBComboRole::Close: return 0;
	case EVBComboRole::Neutral: return 1;
	case EVBComboRole::Far: return 2;
	default: return INDEX_NONE;
	}
}

int32 FVBComboStartMemory::VariantFor(const UVBAttackConfig* InConfig) const
{
	return InConfig && Config.Get() == InConfig ? LastVariant : INDEX_NONE;
}

int32 FVBComboStartMemory::LastStartFor(const UVBAttackConfig* InConfig, EVBComboRole Role) const
{
	const int32 Slot = RotationSlot(Role);
	return InConfig && Config.Get() == InConfig && Slot != INDEX_NONE ? LastByRole[Slot] : INDEX_NONE;
}

void FVBComboStartMemory::NoteStart(const UVBAttackConfig* InConfig, int32 Variant, EVBComboRole Role)
{
	if (Config.Get() != InConfig)
	{
		*this = FVBComboStartMemory();
		Config = InConfig;
	}

	LastVariant = Variant;
	if (const int32 Slot = RotationSlot(Role); Slot != INDEX_NONE)
	{
		LastByRole[Slot] = Variant;
	}
}

FVBComboChoice UVBComboSelectionPolicy::Select(const FVBComboSelectContext& Context, TConstArrayView<FVBComboCandidate> Candidates,
	const FVBComboStartMemory& Memory, const UVBAttackConfig* Config) const
{
	return SelectVariant(Context, Candidates, CloseMaxDistance, FarMinDistance, Memory, Config);
}

EVBComboRole UVBComboSelectionPolicy::BandRole(float DistanceCm, float CloseMax, float FarMin)
{
	if (DistanceCm <= CloseMax)
	{
		return EVBComboRole::Close;
	}

	return DistanceCm > FarMin ? EVBComboRole::Far : EVBComboRole::Neutral;
}

FVBComboChoice UVBComboSelectionPolicy::SelectVariant(const FVBComboSelectContext& Context, TConstArrayView<FVBComboCandidate> Candidates,
	float CloseMax, float FarMin, const FVBComboStartMemory& Memory,
	const UVBAttackConfig* Config)
{
	if (Candidates.IsEmpty())
	{
		return {};
	}

	const float Distance = Context.DistanceCm;
	const auto Reaches = [&Context, Distance](const FVBComboCandidate& Candidate)
	{
		return !Context.bHasTarget || Candidate.Reach.Contains(Distance);
	};

	const EVBComboRole Wanted = Context.bHasTarget ? BandRole(Distance, CloseMax, FarMin) : EVBComboRole::Neutral;
	if (Context.bSprinting)
	{
		const FIndexList Sprint = Collect(Candidates,
			[&](const FVBComboCandidate& C) { return C.Role == EVBComboRole::Sprint && Reaches(C); });
		if (!Sprint.IsEmpty())
		{
			return Choose(Sprint, true, EVBComboChoiceReason::Sprint, Wanted);
		}
	}

	if (Context.bHasTarget)
	{
		for (const EVBComboRole Role : {Wanted, EVBComboRole::Neutral})
		{
			const FIndexList Eligible = Collect(Candidates, [&](const FVBComboCandidate& C) { return C.Role == Role && Reaches(C); });
			if (!Eligible.IsEmpty())
			{
				return ChooseRotating(Eligible, true, Role == Wanted ? EVBComboChoiceReason::Band : EVBComboChoiceReason::BandNeutral,
					Wanted, Role, Memory.LastStartFor(Config, Role));
			}
		}
	}

	// 무표적이거나 자격 후보가 없다: 버튼을 버리지 않고 일반 공격으로 남긴다. 표적이 있었으면 '닿는 선택이 아니다' 를 알린다.
	//   무표적이면 Neutral 줄이 모두 자격이라 구간처럼 돌고, 닿지 않는 폴백은 첫 줄을 쓴다.
	FIndexList General = Collect(Candidates, [](const FVBComboCandidate& C) { return C.Role == EVBComboRole::Neutral; });
	if (!Context.bHasTarget && !General.IsEmpty())
	{
		return ChooseRotating(General, true, EVBComboChoiceReason::NoTarget, Wanted, EVBComboRole::Neutral,
			Memory.LastStartFor(Config, EVBComboRole::Neutral));
	}

	if (General.IsEmpty())
	{
		General = Collect(Candidates,
			[](const FVBComboCandidate& C) { return C.Role != EVBComboRole::Sprint && C.Role != EVBComboRole::Manual; });
	}

	return Choose(General, !Context.bHasTarget,
		Context.bHasTarget ? EVBComboChoiceReason::NoneInReach : EVBComboChoiceReason::NoTarget, Wanted);
}

bool UVBComboSelectionPolicy::ApplyForcedVariant(int32 ForcedVariant, int32 NumCandidates, bool bHasTarget, FVBComboChoice& InOutChoice)
{
	if (ForcedVariant < 0 || ForcedVariant >= NumCandidates)
	{
		return false;
	}

	InOutChoice.Variant = ForcedVariant;
	InOutChoice.Reason = EVBComboChoiceReason::Forced;
	// 강제 콤보는 맞는 것을 보려는 것이라 표적이 있으면 자격 선택처럼 다가간다
	InOutChoice.bTargetInReach = bHasTarget;
	InOutChoice.Qualified = 1;
	return true;
}

TArray<EVBComboRole> UVBComboSelectionPolicy::FindLoneBandsBesideManual(TConstArrayView<FVBComboCandidate> Candidates)
{
	TArray<EVBComboRole> Lone;
	if (!Candidates.ContainsByPredicate([](const FVBComboCandidate& Row) { return Row.Role == EVBComboRole::Manual; }))
	{
		return Lone;
	}

	for (const EVBComboRole Band : { EVBComboRole::Close, EVBComboRole::Neutral, EVBComboRole::Far })
	{
		const int32 Rows = Algo::CountIf(Candidates, [Band](const FVBComboCandidate& Row) { return Row.Role == Band; });
		if (Rows == 1)
		{
			Lone.Add(Band);
		}
	}

	return Lone;
}
