// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBGA_HitReact.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Data/VBHitReactConfig.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Enemy/VBEnemyBase.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace VBSoldierSideHeavyTest
{
	// Soldier 의 선 자세 옆 Heavy 반동 - 전에는 옆 Heavy 칸이 비어 옆 피격이 밀린 방향의 앞/뒤 클립으로 떨어졌다.
	const TCHAR* Config = TEXT("/Game/VB/Data/DA_HitReact_Soldier.DA_HitReact_Soldier");
	const TCHAR* MannySkeleton = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
	const TCHAR* Left = TEXT("AM_Enemy_FabFighter_Hit_L_Manny");
	const TCHAR* Right = TEXT("AM_Enemy_FabFighter_Hit_R_Manny");
	// 앞/뒤 Heavy 칸은 원래 내용 그대로 남아야 한다.
	const TCHAR* Front = TEXT("AM_Enemy_Stagger");
	const TCHAR* Back = TEXT("AM_Enemy_HeavyHit_B");

	// Montage 가 트는 애니메이션이 병사 skeleton 으로 retarget 된 것인지 본다(UEFN 클립 이름만 바꾼 것이 아니라).
	bool PlaysOnManny(const UAnimMontage& Montage)
	{
		if (!Montage.GetSkeleton() || Montage.GetSkeleton()->GetPathName() != MannySkeleton)
		{
			return false;
		}

		for (const FSlotAnimationTrack& Track : Montage.SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				const UAnimSequenceBase* Clip = Segment.GetAnimReference();
				if (!Clip || !Clip->GetSkeleton() || Clip->GetSkeleton()->GetPathName() != MannySkeleton)
				{
					return false;
				}
			}
		}

		return true;
	}
}

// 실제 애셋: 두 병사 archetype 이 이 설정을 읽는 hit-react ability 를 주고, yaw 0 피해자의 각 사분면 Heavy 피격이
//   옆은 옆 클립, 앞/뒤는 기존 클립으로 풀린다(공격자 +X 앞, -X 뒤, -Y 왼쪽, +Y 오른쪽).
//   새 Montage 는 Manny skeleton 의 제자리 선 반동: DefaultSlot, blend 0.08/0.10, rate 1, notify 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSoldierSideHeavyHitTest, "VB.Unit.HitReact.SoldierSideHeavy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSoldierSideHeavyHitTest::RunTest(const FString& Parameters)
{
	using namespace VBSoldierSideHeavyTest;
	const UVBHitReactConfig* HitReact = LoadObject<UVBHitReactConfig>(nullptr, Config);
	if (!TestNotNull(TEXT("DA_HitReact_Soldier"), HitReact))
	{
		return false;
	}

	// 로드된 그대로의 소비 사슬: archetype CDO AbilitySet -> hit-react ability CDO -> HitReactConfig(protected 라 reflection 으로 읽는다).
	const FObjectPropertyBase* ConfigProperty = FindFProperty<FObjectPropertyBase>(UVBGA_HitReact::StaticClass(), TEXT("HitReactConfig"));
	if (!TestNotNull(TEXT("UVBGA_HitReact::HitReactConfig property"), ConfigProperty))
	{
		return false;
	}

	for (const TCHAR* Archetype : { TEXT("/Game/VB/Blueprints/Enemy/BP_EnemySoldier.BP_EnemySoldier_C"),
		TEXT("/Game/VB/Blueprints/Enemy/BP_EnemySoldierCombo.BP_EnemySoldierCombo_C") })
	{
		const UClass* Class = LoadClass<AVBEnemyBase>(nullptr, Archetype);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Archetype), Class))
		{
			continue;
		}

		const UVBAbilitySet* Set = Class->GetDefaultObject<AVBEnemyBase>()->AbilitySet;
		if (!TestNotNull(*FString::Printf(TEXT("%s ability set"), *Class->GetName()), Set))
		{
			continue;
		}

		int32 Readers = 0;
		for (const FVBAbilitySetAbility& Entry : Set->GetGrantedAbilities())
		{
			const TSubclassOf<UGameplayAbility>& Ability = Entry.Ability;
			if (!Ability || !Ability->IsChildOf(UVBGA_HitReact::StaticClass()))
			{
				continue;
			}

			++Readers;
			TestTrue(*FString::Printf(TEXT("%s hit react reads the soldier config"), *Class->GetName()),
				ConfigProperty->GetObjectPropertyValue_InContainer(Ability->GetDefaultObject()) == HitReact);
		}

		TestEqual(*FString::Printf(TEXT("%s grants one hit react ability"), *Class->GetName()), Readers, 1);
	}

	// 사분면에 놓은 공격자를 게임의 방향 규칙에 태운 뒤 Heavy 판정을 본다.
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	const AActor* Victim = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("victim"), Victim))
	{
		return false;
	}

	struct FQuadrant { FVector Attacker; EVBHitDirection Direction; const TCHAR* Montage; };
	for (const FQuadrant& Q : { FQuadrant{ FVector(150, 0, 0), EVBHitDirection::Front, Front }, FQuadrant{ FVector(-150, 0, 0), EVBHitDirection::Back, Back },
		FQuadrant{ FVector(0, -150, 0), EVBHitDirection::Left, Left }, FQuadrant{ FVector(0, 150, 0), EVBHitDirection::Right, Right } })
	{
		const AActor* Attacker = World->SpawnActor<AStaticMeshActor>(Q.Attacker, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("attacker"), Attacker))
		{
			continue;
		}

		const EVBHitDirection Direction = VBComputeHitDirection(Victim, Attacker);
		TestEqual(*FString::Printf(TEXT("attacker at %s is quadrant %d"), *Q.Attacker.ToCompactString(),
			static_cast<int32>(Q.Direction)), Direction, Q.Direction);
		const UAnimMontage* Played = HitReact->ResolveHeavyMontage(EVBWeaponType::None, Direction);
		TestEqual(*FString::Printf(TEXT("Heavy hit from %s plays"), *Q.Attacker.ToCompactString()), GetNameSafe(Played),
			FString(Q.Montage));
	}

	for (const TCHAR* Name : { Left, Right })
	{
		const UAnimMontage* Montage = HitReact->ResolveHeavyMontage(EVBWeaponType::None,
			Name == Left ? EVBHitDirection::Left : EVBHitDirection::Right);
		if (!TestNotNull(*FString::Printf(TEXT("%s resolves"), Name), Montage))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s plays a Manny clip"), Name), PlaysOnManny(*Montage));
		TestEqual(*FString::Printf(TEXT("%s one slot track"), Name), Montage->SlotAnimTracks.Num(), 1);
		if (Montage->SlotAnimTracks.Num() == 1)
		{
			TestEqual(*FString::Printf(TEXT("%s slot"), Name), Montage->SlotAnimTracks[0].SlotName, FName(TEXT("DefaultSlot")));
			for (const FAnimSegment& Segment : Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
			{
				const UAnimSequence* Clip = Cast<UAnimSequence>(Segment.GetAnimReference());
				TestTrue(*FString::Printf(TEXT("%s clip has no root motion (the hit's push owns the movement)"), Name), Clip
					&& !Clip->bEnableRootMotion);
			}
		}

		TestEqual(*FString::Printf(TEXT("%s blend in"), Name), Montage->BlendIn.GetBlendTime(), 0.08f, 1e-4f);
		TestEqual(*FString::Printf(TEXT("%s blend out"), Name), Montage->BlendOut.GetBlendTime(), 0.10f, 1e-4f);
		TestEqual(*FString::Printf(TEXT("%s rate"), Name), Montage->RateScale, 1.0f, 1e-4f);
		TestEqual(*FString::Printf(TEXT("%s has no notifies (a reaction, not an attack)"), Name), Montage->Notifies.Num(), 0);
	}

	// 시험 안의 대조: 클립의 UEFN 원본은 같은 skeleton 검사에서 떨어져야 한다 - 아니면 검사가 아무것도 증명하지 않는다.
	const UAnimSequence* Source = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/VB/Animations/UEFN/FabFighter/07_Hit/Hit_L_Seq.Hit_L_Seq"));
	if (TestNotNull(TEXT("UEFN source clip"), Source))
	{
		TestTrue(TEXT("UEFN source is not on the Manny skeleton"), !Source->GetSkeleton()
			|| Source->GetSkeleton()->GetPathName() != MannySkeleton);
	}

	return true;
}
#endif
