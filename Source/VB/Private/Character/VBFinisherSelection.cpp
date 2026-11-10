// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherSelection.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBInteractionIK.h"
#include "Animation/VBPoseSearchIndex.h"
#include "Character/VBFinisherActorQueries.h"
#include "Character/VBFinisherApproachPolicy.h"
#include "Character/VBFinisherSpaceRules.h"
#include "Character/VBInteractionReplay.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VB/VB.h"

namespace
{
	// 짝을 짓는 쪽. chooser 와 Pose History 이름을 들어야 하므로 플레이어 AnimInstance 여야 한다.
	UVBAnimInstance* FindAttackerAnim(const AActor* Actor)
	{
		return Cast<UVBAnimInstance>(VBFinisherActorQueries::FindAnimInstance(Actor));
	}

	// 이 캐릭터가 루트모션을 받을 수 있는가 - MOVE_None 이면 CMC 가 버린다. 이동을 켜지는 않는다(담당은 따로 있다).
	bool CanConsumeRootMotion(const AActor* Participant)
	{
		const ACharacter* Character = Cast<ACharacter>(Participant);
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		const UAnimInstance* Anim = VBFinisherActorQueries::FindAnimInstance(Participant);
		const UPrimitiveComponent* Capsule = Movement ? Movement->UpdatedPrimitive : nullptr;
		const bool bConsumesRootMotion = Anim &&
			(Anim->RootMotionMode == ERootMotionMode::RootMotionFromMontagesOnly ||
				Anim->RootMotionMode == ERootMotionMode::RootMotionFromEverything);
		if (!Movement || Movement->MovementMode == MOVE_None || !Movement->IsActive() ||
			!Movement->IsComponentTickEnabled() || !Capsule ||
			Capsule->Mobility != EComponentMobility::Movable || Capsule->IsSimulatingPhysics() || !bConsumesRootMotion)
		{
			VB_LOG(Warning, "InteractionFinisher blocked: actor=%s movementMode=%d active=%d tick=%d rootMotionMode=%d capsulePhysics=%d",
				*GetNameSafe(Participant), Movement ? static_cast<int32>(Movement->MovementMode) : -1,
				Movement && Movement->IsActive(), Movement && Movement->IsComponentTickEnabled(),
				Anim ? static_cast<int32>(Anim->RootMotionMode) : -1, Capsule && Capsule->IsSimulatingPhysics());
			return false;
		}

		return true;
	}

	// 후보 DB 마다 역할을 달아 질의를 만든다. 역할은 고정이다(공격자 = Attacker, 대상 = Victim).
	TArray<FPoseSearchMotionMatchMultiQuery> MakeRoleQueries(const TArray<UPoseSearchDatabase*>& Databases,
		UAnimInstance* AttackerAnim, UAnimInstance* VictimAnim)
	{
		TArray<FPoseSearchMotionMatchMultiQuery> Queries;
		Queries.Reserve(Databases.Num());
		for (UPoseSearchDatabase* Database : Databases)
		{
			FPoseSearchMotionMatchMultiQuery& Query = Queries.AddDefaulted_GetRef();
			Query.Database = Database;

			FPoseSearchAnimContextRoles& AttackerRoles = Query.AnimContextsRoles.AddDefaulted_GetRef();
			AttackerRoles.AnimContext = AttackerAnim;
			AttackerRoles.Roles.Add(VBFinisherActorQueries::RoleAttacker);

			FPoseSearchAnimContextRoles& VictimRoles = Query.AnimContextsRoles.AddDefaulted_GetRef();
			VictimRoles.AnimContext = VictimAnim;
			VictimRoles.Roles.Add(VBFinisherActorQueries::RoleVictim);
		}

		return Queries;
	}

	// 공격자 몫의 결과. 그것이 없으면 짝이 안 지어진 것이다.
	const FPoseSearchBlueprintResult* FindAttackerResult(const TArray<FPoseSearchBlueprintResult>& Results, const AActor* Owner)
	{
		return Results.FindByPredicate([Owner](const FPoseSearchBlueprintResult& R)
		{
			return R.bIsInteraction && R.SelectedAnim && UPoseSearchLibrary::GetActor(R) == Owner;
		});
	}

	// 검색 결과 두 개를 선택 값으로 옮긴다. 역할이 한 번씩, 같은 자산이 아니면 false. 진행 칸은 건드리지 않는다.
	bool FillChoiceFromResults(const TArray<FPoseSearchBlueprintResult>& Results, const FPoseSearchBlueprintResult& AttackerResult,
		const UMultiAnimAsset& Asset, const UVBFinisherConfig* Config, const AActor* Owner, const AActor* Target,
		FVBInteractionReplay& Choice)
	{
		Choice.Asset = const_cast<UMultiAnimAsset*>(&Asset);
		Choice.Database = const_cast<UPoseSearchDatabase*>(AttackerResult.SelectedDatabase.Get());
		Choice.Config = Config;
		Choice.RootTransforms = AttackerResult.ActorRootTransforms;
		Choice.RootBoneTransforms = AttackerResult.ActorRootBoneTransforms;
		Choice.BlendParameters = AttackerResult.BlendParameters;
		Choice.Actors.SetNum(2);
		Choice.StartTimes.SetNum(2);
		Choice.PlayRates.SetNum(2);
		Choice.SupersededMontagePlayIds.SetNum(2);
		for (const FPoseSearchBlueprintResult& Result : Results)
		{
			AActor* Actor = const_cast<AActor*>(UPoseSearchLibrary::GetActor(Result));
			if (!Choice.Actors.IsValidIndex(Result.RoleIndex) || Choice.Actors[Result.RoleIndex] ||
				Result.SelectedAnim != &Asset || (Actor != Owner && Actor != Target) ||
				Result.Role != (Actor == Owner ? VBFinisherActorQueries::RoleAttacker : VBFinisherActorQueries::RoleVictim))
			{
				return false;
			}

			Choice.Actors[Result.RoleIndex] = Actor;
			Choice.StartTimes[Result.RoleIndex] = Result.SelectedTime;
			Choice.PlayRates[Result.RoleIndex] = Result.WantedPlayRate;
			// 서버의 이전 GAS 재생 id 를 실제 재생 전에 잡는다 - 원격은 이 값으로 자기 쪽 낡은 몽타주를 거른다.
			if (const UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(
				UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor)))
			{
				Choice.SupersededMontagePlayIds[Result.RoleIndex] = ASC->GetMontagePlayInstanceId();
			}
		}

		return true;
	}
}

bool FVBFinisherSelection::HasInteractionFinisherData(const AActor* Owner) const
{
	// 자료 유무만 묻는다 - 지금 성립 여부로 거르면 옛 경로로 샌다.
	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Owner, Databases);
	return !Databases.IsEmpty();
}

const UVBFinisherConfig* FVBFinisherSelection::SelectFinisher(const AActor* Owner, const AActor* Target) const
{
	if (!Owner || !Target)
	{
		return nullptr;
	}

	const UVBWeaponStateComponent* WSC = Owner->FindComponentByClass<UVBWeaponStateComponent>();
	if (!WSC)
	{
		return nullptr;
	}

	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = WSC->GetFinisherConfigs();
	if (!Configs || Configs->IsEmpty())
	{
		// 처형 자산이 아직 없는 무기는 정상적으로 온다. 연타로 들어오므로 무기당 한 번만 경고한다.
		const uint8 Key = static_cast<uint8>(WSC->GetCurrentWeaponType());
		if (!WarnedEmptyWeapons.Contains(Key))
		{
			WarnedEmptyWeapons.Add(Key);
			VB_LOG(Warning, "SelectFinisher: 무기 %d 에 FinisherConfigs 가 비어 있다 - 처형이 안 걸린다. "
				"그 무기의 정의(DA_Weapon_*)의 FinisherConfigs 에 설정 자산을 배선하라.", Key);
		}

		return nullptr;
	}

	TArray<const UVBFinisherConfig*> Candidates;
	for (const TObjectPtr<UVBFinisherConfig>& Config : *Configs)
	{
		// null 항목은 저작 사고다. 건너뛰되 무시하지 않고 알린다 - 그 자리가 비면 랜덤 폭이 줄어든다.
		if (!Config)
		{
			VB_LOG(Warning, "SelectFinisher: FinisherConfigs 에 빈 항목이 있다");
			continue;
		}

		if (Config->Scene && VBFinisherApproachPolicy::Matches(Config->Approach,
			Owner->GetActorLocation(), Target->GetActorLocation(), Target->GetActorForwardVector()))
		{
			Candidates.Add(Config);
		}
	}

	if (Candidates.IsEmpty())
	{
		return nullptr;
	}

	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

void FVBFinisherSelection::Prewarm(const AActor* Owner) const
{
	// 첫 처형이 색인 빌드를 기다리지 않게 등록된 모든 무기의 DB 를 미리 준비한다.
	//   공용 chooser 의 DB 는 여기서 못 모아 처형 시작의 EnsureReady 가 받는다.
	TArray<const UPoseSearchDatabase*> Databases;
	if (const UVBWeaponStateComponent* Weapons = Owner ? Owner->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
	{
		TArray<const UVBFinisherConfig*> Configs;
		Weapons->GetAllFinisherConfigs(Configs);
		for (const UVBFinisherConfig* Config : Configs)
		{
			if (Config->InteractionDatabase)
			{
				Databases.AddUnique(Config->InteractionDatabase);
			}
		}
	}

	VBPoseSearchIndex::Prewarm(Databases);
}

void FVBFinisherSelection::GatherInteractionDatabases(const AActor* Owner, TArray<UPoseSearchDatabase*>& OutDatabases,
	const AActor* Target) const
{
	OutDatabases.Reset();
	const UVBWeaponStateComponent* Weapons = Owner ? Owner->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = Weapons ? Weapons->GetFinisherConfigs() : nullptr;
	if (Configs && !Configs->IsEmpty())
	{
		for (const UVBFinisherConfig* Config : *Configs)
		{
			if (Config && Config->InteractionDatabase && (!Target || VBFinisherApproachPolicy::Matches(Config->Approach,
				Owner->GetActorLocation(), Target->GetActorLocation(), Target->GetActorForwardVector())))
			{
				OutDatabases.AddUnique(Config->InteractionDatabase);
			}
		}

		return; // 무기 전용 목록을 Fighter 공용 chooser와 섞지 않는다.
	}

	if (const UVBAnimInstance* Anim = FindAttackerAnim(Owner))
	{
		Anim->EvaluateFinisherDatabases(OutDatabases);
	}
}

const UVBFinisherConfig* FVBFinisherSelection::FindInteractionConfig(const AActor* Owner, const UPoseSearchDatabase* Database) const
{
	const UVBWeaponStateComponent* Weapons = Owner ? Owner->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	const TArray<TObjectPtr<UVBFinisherConfig>>* Configs = Weapons ? Weapons->GetFinisherConfigs() : nullptr;
	if (Configs)
	{
		for (const UVBFinisherConfig* Config : *Configs)
		{
			if (Config && Config->InteractionDatabase == Database)
			{
				return Config;
			}
		}
	}

	return nullptr;
}

bool FVBFinisherSelection::CanStartInteractionFinisher(const FVBFinisherStartQuery& Query) const
{
	const AActor* Owner = Query.Owner;
	const AActor* Target = Query.Target;
	if (!Owner || !Target)
	{
		return false;
	}

	// 옛 경로와 같은 이유로 권한에서만 시작한다 - 두 액터의 상태가 갈리면 안 된다.
	if (!Owner->HasAuthority() || Target == Owner || Query.bOwnerPlaying || Query.bTargetInScene)
	{
		return false;
	}

	// 공격자는 UVBAnimInstance, 피해자는 인터페이스만 있으면 된다. 없으면 시작하지 않는다(옛 경로로 가지도 않는다).
	const UVBAnimInstance* AttackerAnim = FindAttackerAnim(Owner);
	if (!AttackerAnim || !VBFinisherActorQueries::FindInteractionAnim(Target))
	{
		return false;
	}

	// 워핑 컴포넌트가 없으면 엔진은 말없이 두 캐릭터를 어긋난 채로 돌리므로 여기서 막는다.
	if (!Owner->FindComponentByClass<UMotionWarpingComponent>() ||
		!Target->FindComponentByClass<UMotionWarpingComponent>())
	{
		VB_LOG(Warning, "CanStartInteractionFinisher: MotionWarpingComponent 가 없다 "
			"(attacker=%s victim=%s). 상호작용 처형은 그것으로 자리를 맞춘다",
			*GetNameSafe(Owner), *GetNameSafe(Target));
		return false;
	}

	if (!CanConsumeRootMotion(Owner) || !CanConsumeRootMotion(Target))
	{
		return false;
	}

	// chooser 미배선이거나 이 무기에 상호작용 처형이 없다 = 이 기능이 아직 안 켜진 것이다.
	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Owner, Databases, Target);
	FilterDatabasesBySkeleton(Databases, Owner, Target);
	return !Databases.IsEmpty();
}

void FVBFinisherSelection::FilterDatabasesBySkeleton(TArray<UPoseSearchDatabase*>& InOutDatabases,
	const AActor* Attacker, const AActor* Victim)
{
	const UAnimInstance* AttackerAnim = VBFinisherActorQueries::FindAnimInstance(Attacker);
	const UAnimInstance* VictimAnim = VBFinisherActorQueries::FindAnimInstance(Victim);

	InOutDatabases.RemoveAll([AttackerAnim, VictimAnim](const UPoseSearchDatabase* Database)
	{
		const bool bOk =
			UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Database, VBFinisherActorQueries::RoleAttacker, AttackerAnim) &&
			UVBInteractionIKLibrary::DoesSkeletonMatchSchema(Database, VBFinisherActorQueries::RoleVictim, VictimAnim);

		if (!bOk)
		{
			// 두 골격에 안 맞는 DB 다(남기면 엔진이 크래시한다). 버리고 DB 당 한 번 경고한다.
			static TSet<TWeakObjectPtr<const UPoseSearchDatabase>> Warned;
			if (!Warned.Contains(Database))
			{
				Warned.Add(Database);
				VB_LOG(Log, "상호작용 처형: %s 는 골격이 안 맞아 후보에서 뺀다 "
					"(스키마가 선언한 역할 골격과 이 두 액터의 골격이 다르다)", *GetNameSafe(Database));
			}
		}

		return !bOk;
	});
}

bool FVBFinisherSelection::TrySelectInteraction(const FVBFinisherStartQuery& Query,
	TArray<FPoseSearchBlueprintResult>& OutResults, FVBInteractionReplay& OutChoice) const
{
	OutResults.Reset();
	OutChoice = FVBInteractionReplay();
	// 그 사이 상태가 바뀔 수 있어 같은 함수로 다시 묻는다.
	if (!CanStartInteractionFinisher(Query))
	{
		return false;
	}

	AActor* Owner = Query.Owner;
	AActor* Target = Query.Target;
	UVBAnimInstance* AttackerAnim = FindAttackerAnim(Owner);
	UAnimInstance* VictimAnim = VBFinisherActorQueries::FindAnimInstance(Target);

	TArray<UPoseSearchDatabase*> Databases;
	GatherInteractionDatabases(Owner, Databases, Target);
	FilterDatabasesBySkeleton(Databases, Owner, Target);
	// 색인이 아직 빌드 중이면 엔진이 그 검색을 건너뛰어 결과 0 이 된다 - 취소하지 않고 여기서 기다린다(에디터 전용).
	VBPoseSearchIndex::EnsureReady(Databases, TEXT("상호작용 처형"));

	while (!Databases.IsEmpty())
	{
		const TArray<FPoseSearchMotionMatchMultiQuery> Queries = MakeRoleQueries(Databases, AttackerAnim, VictimAnim);
		// 버튼을 누른 프레임에 시작해야 하므로 한 프레임 늦는 MotionMatchInteraction 대신 동기 검색을 쓴다.
		TArray<FPoseSearchBlueprintResult> Results;
		UPoseSearchInteractionLibrary::MotionMatchMulti(
			Queries, AttackerAnim->GetInteractionPoseHistoryName(), FPoseSearchContinuingProperties(), Results);
		const FPoseSearchBlueprintResult* AttackerResult = FindAttackerResult(Results, Owner);
		if (!AttackerResult)
		{
			VB_LOG(Log, "StartInteractionFinisher: no pair (databases=%d results=%d)", Databases.Num(), Results.Num());
			return false;
		}
		const UMultiAnimAsset* Asset = Cast<UMultiAnimAsset>(AttackerResult->SelectedAnim);
		if (!Asset || Asset->GetNumRoles() != 2 || Results.Num() != 2)
		{
			return false;
		}

		FVBInteractionReplay Choice;
		const UVBFinisherConfig* Config = FindInteractionConfig(Owner, AttackerResult->SelectedDatabase.Get());
		if (!FillChoiceFromResults(Results, *AttackerResult, *Asset, Config, Owner, Target, Choice))
		{
			return false;
		}
		if (VBFinisherSpaceRules::HasInteractionSpace(Results, Config))
		{
			OutResults = MoveTemp(Results);
			OutChoice = MoveTemp(Choice);
			return true;
		}
		VB_LOG(Log, "InteractionFinisher: reject candidate without clear space (config=%s)", *GetNameSafe(Config));
		if (Databases.Remove(const_cast<UPoseSearchDatabase*>(AttackerResult->SelectedDatabase.Get())) == 0)
		{
			return false;
		}
	}
	return false;
}
