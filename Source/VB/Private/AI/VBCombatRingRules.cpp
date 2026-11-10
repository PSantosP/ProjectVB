#include "AI/VBCombatRingRules.h"

namespace VBCombatRingRulesPrivate
{
	struct FPairing
	{
		double Distance = 0.0;
		int32 MemberIndex = INDEX_NONE;
		int32 SlotIndex = INDEX_NONE;
	};
}

double VBCombatRingRules::WaitingDistance(float AttackCommitDistance, float WaitingStandoff)
{
	return static_cast<double>(AttackCommitDistance + WaitingStandoff);
}

TArray<FVBCombatRingAssignment> VBCombatRingRules::AssignSlots(const FVector& Center, int32 SlotCount,
	TConstArrayView<FVBCombatRingMember> Members)
{
	TArray<FVBCombatRingAssignment> Assignments;
	if (Members.IsEmpty())
	{
		return Assignments;
	}

	SlotCount = FMath::Max(1, SlotCount);
	TArray<FVector> Directions;
	Directions.Reserve(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const double Theta = 2.0 * UE_DOUBLE_PI * static_cast<double>(SlotIndex) / static_cast<double>(SlotCount);
		Directions.Add(FVector(FMath::Cos(Theta), FMath::Sin(Theta), 0.0));
	}

	TArray<VBCombatRingRulesPrivate::FPairing> Pairings;
	Pairings.Reserve(Members.Num() * SlotCount);
	for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
	{
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			const FVector Candidate = Center + Directions[SlotIndex] * Members[MemberIndex].WaitingDistance;
			VBCombatRingRulesPrivate::FPairing Pairing;
			Pairing.Distance = FVector::DistSquared(Members[MemberIndex].Location, Candidate);
			Pairing.MemberIndex = MemberIndex;
			Pairing.SlotIndex = SlotIndex;
			Pairings.Add(Pairing);
		}
	}

	Pairings.Sort([](const VBCombatRingRulesPrivate::FPairing& A, const VBCombatRingRulesPrivate::FPairing& B)
	{
		if (A.Distance != B.Distance)
		{
			return A.Distance < B.Distance;
		}

		if (A.MemberIndex != B.MemberIndex)
		{
			return A.MemberIndex < B.MemberIndex;
		}

		return A.SlotIndex < B.SlotIndex;
	});

	TBitArray<> MemberTaken(false, Members.Num());
	TBitArray<> SlotTaken(false, SlotCount);
	for (const VBCombatRingRulesPrivate::FPairing& Pairing : Pairings)
	{
		if (MemberTaken[Pairing.MemberIndex] || SlotTaken[Pairing.SlotIndex])
		{
			continue;
		}

		FVBCombatRingAssignment Assignment;
		Assignment.MemberIndex = Pairing.MemberIndex;
		Assignment.SlotIndex = Pairing.SlotIndex;
		Assignment.Location = Center + Directions[Pairing.SlotIndex] * Members[Pairing.MemberIndex].WaitingDistance;
		Assignments.Add(Assignment);
		MemberTaken[Pairing.MemberIndex] = true;
		SlotTaken[Pairing.SlotIndex] = true;
	}

	return Assignments;
}
