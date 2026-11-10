#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Character/VBCharacter.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Engine/World.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "MotionWarpingComponent.h"
#include "Traversal/VBTraversalComponent.h"

// 몸 소유 판정의 옛 식(독립 기준 함수)과 정책 결과를 입력 조합 전부에서 대조한다. 기준은 정책 도우미를 부르지 않는다 -
//   같은 틀린 식을 나눠 쓰면 전수 대조도 통과하기 때문이다. 증명하는 것은 '같은 사실 -> 같은 결과' 뿐이고,
//   배선, 읽는 시점, 네트워크는 아니다(배선은 아래 CharacterFactGathering 이 일부를 본다).
namespace VBBodyControlPolicyTestBaseline
{
	static const TCHAR* Revision = TEXT("d3ad4bd");

	static bool Bit(uint32 Mask, int32 Index) { return (Mask >> Index) & 1u; }

	// 캡슐 회전 - 두 목록과 사망 연출을 합친 결정 자체다(옛 식의 사본이 아니다).
	//   비트: 0 처형 장면, 1 래그돌, 2 스마트 오브젝트, 3 환경 상호작용, 4 공격 워프, 5 트래버설 동작, 6 매달림, 등반, 사다리, 7 사망 연출.
	static bool CapsuleRotation(uint32 M)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			if (Bit(M, Index))
			{
				return true;
			}
		}

		return false;
	}
	// 락온 몸 돌리기, 전투 카메라의 몸 조건 - 공격 워프, 트래버설 동작, 사망 연출은 막지 않는다.
	static bool CombatCamera(uint32 M)
	{
		return Bit(M, 0) || Bit(M, 1) || Bit(M, 2) || Bit(M, 3) || Bit(M, 6);
	}
	// VBEnvironmentCameraSolver 의 bAllowComposition 에서 조준을 뺀 부정.
	static bool EnvironmentCamera(uint32 M)
	{
		const bool bRagdoll = Bit(M, 0);
		const bool bSpecialTraversal = Bit(M, 1);
		return !(!bRagdoll && !bSpecialTraversal);
	}

	// UVBAnimInstance 의 bPoseOwnedByMontage = bDeadNow || bInFinisherScene || IsTraversalOwningPose().
	static bool MontagePose(uint32 M)
	{
		return Bit(M, 0) || Bit(M, 1) || Bit(M, 2);
	}

	// UVBPlayerAttentionComponent::IsPoseSuppressed - 소유자 확인 뒤.
	static bool AttentionPose(uint32 M)
	{
		if (Bit(M, 0))
		{
			return true; // IsActorDead(GetOwner())
		}

		if (Bit(M, 1) || Bit(M, 2))
		{
			return true; // Traversal->IsDoingTraversal() || Traversal->OwnsCapsuleFacing()
		}

		if (Bit(M, 3))
		{
			return true; // Interactor->OwnsCapsuleFacing()
		}

		return Bit(M, 4) || Bit(M, 5) || Bit(M, 6) || Bit(M, 7) || Bit(M, 8) || Bit(M, 9); // 태그 여섯
	}

	// UVBLookAtPOIComponent::ResolveTarget 의 차단(켜짐, 캐릭터, 메시 확인 뒤).
	static bool PlayerLookAt(uint32 M)
	{
		if (Bit(M, 0))
		{
			return true; // GetMesh()->IsAnySimulatingPhysics()
		}

		if (Bit(M, 1))
		{
			return true; // ASC State_Dead
		}

		if (Bit(M, 2))
		{
			return true; // Finisher->IsPlaying()
		}

		if (Bit(M, 3))
		{
			return true; // Ragdoll->IsRagdollActive()
		}

		if (Bit(M, 4))
		{
			return true; // SmartObject->IsInteracting()
		}

		if (Bit(M, 5))
		{
			return true; // Attention->IsPoseSuppressed()
		}

		return false;
	}

	// UVBEnemyLookAtComponent::ResolveTarget 의 적 전용 차단(대상 사망, Super 앞).
	static bool EnemyLookAt(uint32 M)
	{
		if (Bit(M, 0) || Bit(M, 1) || Bit(M, 2))
		{
			return true; // IsDead || UVBEnemyRagdollComponent::IsRecoverableActive || IsActorInScene
		}

		return Bit(M, 3) || Bit(M, 4) || Bit(M, 5) || Bit(M, 6); // Staggered, Downed, HitReacting, Attacking
	}

	// UVBTraversalComponent::TryTraversalAction 의 시작 차단.
	static bool TraversalStart(uint32 M)
	{
		return Bit(M, 0) || Bit(M, 1); // SmartObject->IsInteracting() || Ragdoll->IsRagdollActive()
	}

	// AVBCharacter::IsSlideJumpSceneAllowed 의 부정, C5 가 정책으로 옮기기 전 몸의 줄 그대로(C2b 뒤 트리).
	static bool SlideJump(uint32 M)
	{
		if (Bit(M, 0))
		{
			return true; // InteractorComponent->OwnsCapsuleFacing()
		}

		if (Bit(M, 1))
		{
			return true; // SmartObjectInteraction->IsInteracting()
		}

		if (Bit(M, 2))
		{
			return true; // PlayerRagdollComponent->IsRagdollActive()
		}

		if (Bit(M, 3))
		{
			return true; // UVBAbilitySystemStatics::IsActorFinishing
		}

		if (Bit(M, 4))
		{
			return true; // UVBAbilitySystemStatics::IsActorDead
		}

		return Bit(M, 5) || Bit(M, 6); // Traversal->IsDoingTraversal() || Traversal->OwnsCapsuleFacing()
	}
}

namespace
{
	using namespace VBBodyControlPolicy;
	using VBBodyControlPolicyTestBaseline::Bit;

	// 비트 번호는 위 기준 함수의 주석 순서와 같다. 구조 분해로 채워 사실 칸이 늘면 여기서 컴파일이 깨진다 -
	//   생성기가 새 칸을 안 채우면 늘 false 인 칸으로 전수 대조가 그대로 통과하기 때문이다.
	//   깨지면 생성기, 기준 함수, 비트 수를 같이 고친다.
	FCapsuleRotationFacts CapsuleFacts(uint32 M)
	{
		FCapsuleRotationFacts F;
		auto& [Finisher, Ragdoll, SmartFacing, EnvironmentFacing, Warp, TraversalAction, TraversalFacing, DeathPresented] = F;
		Finisher = Bit(M, 0); Ragdoll = Bit(M, 1); SmartFacing = Bit(M, 2); EnvironmentFacing = Bit(M, 3);
		Warp = Bit(M, 4); TraversalAction = Bit(M, 5); TraversalFacing = Bit(M, 6); DeathPresented = Bit(M, 7);
		return F;
	}

	FEnvironmentCameraFacts EnvironmentFacts(uint32 M)
	{
		FEnvironmentCameraFacts F;
		auto& [Ragdoll, TraversalFacing] = F;
		Ragdoll = Bit(M, 0); TraversalFacing = Bit(M, 1);
		return F;
	}

	FMontagePoseFacts PoseFacts(uint32 M)
	{
		FMontagePoseFacts F;
		auto& [Dead, Finisher, TraversalPose] = F;
		Dead = Bit(M, 0); Finisher = Bit(M, 1); TraversalPose = Bit(M, 2);
		return F;
	}

	FAttentionPoseFacts AttentionFacts(uint32 M)
	{
		FAttentionPoseFacts F;
		auto& [Dead, TraversalAction, TraversalFacing, EnvironmentFacing, Attacking, DodgeCommit, HitReacting, Staggered, Downed,
			Interacting] = F;
		Dead = Bit(M, 0); TraversalAction = Bit(M, 1); TraversalFacing = Bit(M, 2);
		EnvironmentFacing = Bit(M, 3); Attacking = Bit(M, 4); DodgeCommit = Bit(M, 5);
		HitReacting = Bit(M, 6); Staggered = Bit(M, 7); Downed = Bit(M, 8); Interacting = Bit(M, 9);
		return F;
	}

	FPlayerLookAtFacts PlayerLookFacts(uint32 M)
	{
		FPlayerLookAtFacts F;
		auto& [MeshPhysics, Dead, FinisherPlaying, Ragdoll, SmartInteracting, AttentionSuppressed] = F;
		MeshPhysics = Bit(M, 0); Dead = Bit(M, 1); FinisherPlaying = Bit(M, 2);
		Ragdoll = Bit(M, 3); SmartInteracting = Bit(M, 4); AttentionSuppressed = Bit(M, 5);
		return F;
	}

	FEnemyLookAtFacts EnemyLookFacts(uint32 M)
	{
		FEnemyLookAtFacts F;
		auto& [Dead, RecoverableRagdoll, Finisher, Staggered, Downed, HitReacting, Attacking] = F;
		Dead = Bit(M, 0); RecoverableRagdoll = Bit(M, 1); Finisher = Bit(M, 2);
		Staggered = Bit(M, 3); Downed = Bit(M, 4); HitReacting = Bit(M, 5); Attacking = Bit(M, 6);
		return F;
	}

	FTraversalStartFacts TraversalFacts(uint32 M)
	{
		FTraversalStartFacts F;
		auto& [SmartInteracting, Ragdoll] = F;
		SmartInteracting = Bit(M, 0); Ragdoll = Bit(M, 1);
		return F;
	}

	FSlideJumpSceneFacts SlideJumpFacts(uint32 M)
	{
		FSlideJumpSceneFacts F;
		auto& [EnvironmentFacing, SmartInteracting, Ragdoll, Finishing, Dead, TraversalAction, TraversalFacing] = F;
		EnvironmentFacing = Bit(M, 0); SmartInteracting = Bit(M, 1); Ragdoll = Bit(M, 2); Finishing = Bit(M, 3);
		Dead = Bit(M, 4); TraversalAction = Bit(M, 5); TraversalFacing = Bit(M, 6);
		return F;
	}

	// 어긋난 첫 조합을 정책 이름, 비트마스크, 기준 값, 새 값, 기준 revision 으로 낸다. 어긋난 조합 수도 낸다.
	template <typename BaselineFn, typename PolicyFn>
	void CompareBodyControlPolicy(FAutomationTestBase& Test, const TCHAR* Policy, int32 Bits, BaselineFn Baseline, PolicyFn NewPolicy)
	{
		int32 Mismatches = 0;
		for (uint32 Mask = 0; Mask < (1u << Bits); ++Mask)
		{
			const bool bExpected = Baseline(Mask);
			const bool bActual = NewPolicy(Mask);
			if (bExpected != bActual && Mismatches++ == 0)
			{
				Test.AddError(FString::Printf(TEXT("%s mask=0x%X baseline=%d policy=%d baseline-revision=%s"),
					Policy, Mask, bExpected, bActual, VBBodyControlPolicyTestBaseline::Revision));
			}
		}

		if (Mismatches > 1)
		{
			Test.AddError(FString::Printf(TEXT("%s: %d of %d combinations differ"), Policy, Mismatches, 1 << Bits));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBodyControlPolicyMatchesBaselineTest, "VB.Unit.BodyControl.MatchesBaseline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBBodyControlPolicyMatchesBaselineTest::RunTest(const FString& Parameters)
{
	namespace B = VBBodyControlPolicyTestBaseline;
	CompareBodyControlPolicy(*this, TEXT("CapsuleRotation"), 8, B::CapsuleRotation, [](uint32 M) { return IsCapsuleRotationOwned(CapsuleFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("LockFacing"), 8, B::CombatCamera, [](uint32 M) { return BlocksLockFacing(CapsuleFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("CombatCamera"), 8, B::CombatCamera, [](uint32 M) { return BlocksCombatCamera(CapsuleFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("EnvironmentCamera"), 2, B::EnvironmentCamera, [](uint32 M) { return BlocksEnvironmentCameraComposition(EnvironmentFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("MontagePose"), 3, B::MontagePose, [](uint32 M) { return IsPoseOwnedByMontage(PoseFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("AttentionPose"), 10, B::AttentionPose, [](uint32 M) { return IsAttentionPoseSuppressed(AttentionFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("PlayerLookAt"), 6, B::PlayerLookAt, [](uint32 M) { return BlocksPlayerLookAt(PlayerLookFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("EnemyLookAt"), 7, B::EnemyLookAt, [](uint32 M) { return BlocksEnemyLookAt(EnemyLookFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("TraversalStart"), 2, B::TraversalStart, [](uint32 M) { return BlocksTraversalStart(TraversalFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("SlideJump"), 7, B::SlideJump, [](uint32 M) { return BlocksSlideJump(SlideJumpFacts(M)); });

	// 기본값 경로. 위 생성기는 모든 칸을 덮어써서 기본값을 안 지난다 - 누가 칸의 기본값을 true 로 바꾸면 여기서 잡힌다.
	TestFalse(TEXT("default capsule facts block nothing"), IsCapsuleRotationOwned({}));
	TestFalse(TEXT("default capsule facts do not block lock facing"), BlocksLockFacing({}));
	TestFalse(TEXT("default capsule facts do not block the combat camera"), BlocksCombatCamera({}));
	TestFalse(TEXT("default environment camera facts block nothing"), BlocksEnvironmentCameraComposition({}));
	TestFalse(TEXT("default pose facts block nothing"), IsPoseOwnedByMontage({}));
	TestFalse(TEXT("default attention facts block nothing"), IsAttentionPoseSuppressed({}));
	TestFalse(TEXT("default player look-at facts block nothing"), BlocksPlayerLookAt({}));
	TestFalse(TEXT("default enemy look-at facts block nothing"), BlocksEnemyLookAt({}));
	TestFalse(TEXT("default traversal start facts block nothing"), BlocksTraversalStart({}));
	TestFalse(TEXT("default slide-jump scene facts block nothing"), BlocksSlideJump({}));
	return true;
}

// 캐릭터의 수집이 원본을 맞는 칸에 담는지(배선) - 위 진리표 시험은 이것을 못 본다. 공개 표면만 쓴다.

namespace
{
	// 칸마다 단언한다 - 칸 하나를 다른 칸에 옮겨 담은 수집은 '참인 칸 수' 가 같아도 여기서 걸린다.
	// 비트 번호 = 사실 구조체의 칸 순서.
	void ExpectCapsuleFacts(FAutomationTestBase& Test, const TCHAR* Step, const FCapsuleRotationFacts& F, uint32 Expected)
	{
		const bool Actual[] = {F.bFinisherScene, F.bPlayerRagdoll, F.bSmartObjectFacing, F.bEnvironmentMotionFacing, F.bAttackWarp, F.bTraversalAction,
			F.bTraversalFacing, F.bDeathPresented};
		const TCHAR* Names[] = {TEXT("bFinisherScene"), TEXT("bPlayerRagdoll"), TEXT("bSmartObjectFacing"), TEXT("bEnvironmentMotionFacing"), TEXT("bAttackWarp"),
			TEXT("bTraversalAction"), TEXT("bTraversalFacing"), TEXT("bDeathPresented")};
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Actual)); ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: capsule %s"), Step, Names[Index]), Actual[Index], Bit(Expected, Index));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBodyControlCharacterFactGatheringTest, "VB.Unit.BodyControl.CharacterFactGathering",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBBodyControlCharacterFactGatheringTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	AVBCharacter* Character = World->SpawnActor<AVBCharacter>();
	UMotionWarpingComponent* Warping = Character ? Character->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (!TestNotNull(TEXT("character"), Character) || !TestNotNull(TEXT("motion warping"), Warping))
	{
		World->DestroyWorld(false);
		return false;
	}

	// 사실을 모으는 쪽은 각 컴포넌트의 FindFor 로 찾는다. FindFor 는 그 타입의 첫 번째를 돌려주므로 타입마다 하나뿐이어야 한다.
	TArray<UVBGASPSmartObjectComponent*> SmartComponents;
	Character->GetComponents(SmartComponents);
	TArray<UVBInteractorComponent*> InteractorComponents;
	Character->GetComponents(InteractorComponents);
	TArray<UVBPlayerRagdollComponent*> RagdollComponents;
	Character->GetComponents(RagdollComponents);
	TArray<UVBTraversalComponent*> TraversalComponents;
	Character->GetComponents(TraversalComponents);
	TestEqual(TEXT("one smart object component"), SmartComponents.Num(), 1);
	TestEqual(TEXT("one interactor component"), InteractorComponents.Num(), 1);
	TestEqual(TEXT("one player ragdoll component"), RagdollComponents.Num(), 1);
	TestEqual(TEXT("one traversal component"), TraversalComponents.Num(), 1);

	// 틱 없이 같은 호출 경로에서 차례로 바꾸며 읽는다.
	constexpr uint32 AttackWarpBit = 1u << 4;
	ExpectCapsuleFacts(*this, TEXT("1 default"), Character->GatherCapsuleRotationFacts(), 0);
	TestFalse(TEXT("1 default: capsule rotation free"), IsCapsuleRotationOwned(Character->GatherCapsuleRotationFacts()));

	// 이름을 보는지 - 다른 워프 대상은 주인이 아니다.
	const FName OtherTarget(TEXT("VBUnitOtherWarpTarget"));
	Warping->AddOrUpdateWarpTargetFromLocation(OtherTarget, FVector(100.0, 0.0, 0.0));
	ExpectCapsuleFacts(*this, TEXT("2 other warp target"), Character->GatherCapsuleRotationFacts(), 0);

	// 공격 워프는 캡슐 회전을 쥐지만 락온 몸 돌리기, 전투 카메라는 막지 않는다.
	Warping->AddOrUpdateWarpTargetFromLocation(VBMotionWarpingNames::AttackTarget, FVector(200.0, 0.0, 0.0));
	ExpectCapsuleFacts(*this, TEXT("3 attack warp"), Character->GatherCapsuleRotationFacts(), AttackWarpBit);
	TestTrue(TEXT("3 attack warp: capsule rotation owned"), IsCapsuleRotationOwned(Character->GatherCapsuleRotationFacts()));
	TestFalse(TEXT("3 attack warp: lock facing free"), BlocksLockFacing(Character->GatherCapsuleRotationFacts()));

	// 넣은 값을 들고 있지 않은지 - 빼면 기본 마스크로 돌아온다.
	Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	ExpectCapsuleFacts(*this, TEXT("4 attack warp removed"), Character->GatherCapsuleRotationFacts(), 0);
	TestFalse(TEXT("4 attack warp removed: capsule rotation free"), IsCapsuleRotationOwned(Character->GatherCapsuleRotationFacts()));

	// 원천이 없으면 그 칸은 false, 크래시 없음 - 순수 수집에 빈 원천을 넘긴다. 처형 장면, 공격 워프, 사망 연출이 제 칸(0, 4, 7)에 담기는지도 본다.
	FBodyControlSources NoSources;
	ExpectCapsuleFacts(*this, TEXT("5 no sources"), GatherCapsuleRotationFacts(NoSources), 0);
	FBodyControlSources WarpOnly;
	WarpOnly.bAttackWarp = true;
	ExpectCapsuleFacts(*this, TEXT("6 warp only"), GatherCapsuleRotationFacts(WarpOnly), AttackWarpBit);
	FBodyControlSources SceneOnly;
	SceneOnly.bFinisherScene = true;
	ExpectCapsuleFacts(*this, TEXT("7 finisher scene only"), GatherCapsuleRotationFacts(SceneOnly), 1u << 0);
	FBodyControlSources DeathOnly;
	DeathOnly.bDeathPresented = true;
	ExpectCapsuleFacts(*this, TEXT("7 death presented only"), GatherCapsuleRotationFacts(DeathOnly), 1u << 7);
	// 슬라이드 점프 장면 - 마무리 태그와 죽음은 처형 장면이 아닌 자기 출처이고, 다른 것은 켜지지 않는다
	const auto SlideMask = [](const FSlideJumpSceneFacts& F)
	{
		const bool Slots[] = { F.bEnvironmentMotionFacing, F.bSmartObjectInteracting, F.bPlayerRagdoll, F.bFinishing, F.bDead,
			F.bTraversalAction, F.bTraversalFacing };
		uint32 Mask = 0;
		for (int32 i = 0; i < UE_ARRAY_COUNT(Slots); ++i)
		{
			Mask |= Slots[i] ? (1u << i) : 0u;
		}

		return Mask;
	};
	TestEqual(TEXT("8 slide jump: no sources"), SlideMask(GatherSlideJumpSceneFacts(NoSources)), 0u);
	TestEqual(TEXT("8 slide jump: finisher scene is not the finishing tag"), SlideMask(GatherSlideJumpSceneFacts(SceneOnly)), 0u);
	FBodyControlSources FinishingOnly;
	FinishingOnly.bFinishing = true;
	TestEqual(TEXT("8 slide jump: finishing only"), SlideMask(GatherSlideJumpSceneFacts(FinishingOnly)), 1u << 3);
	FBodyControlSources DeadOnly;
	DeadOnly.bDead = true;
	TestEqual(TEXT("8 slide jump: dead only"), SlideMask(GatherSlideJumpSceneFacts(DeadOnly)), 1u << 4);
	Warping->RemoveWarpTarget(OtherTarget);

	World->DestroyWorld(false);
	return true;
}

#endif
