// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 기본 조각을 한 번에 시각 몸에 입힌다. 엔진 mesh 둘(SkeletalCube 몸, 맞는 조각, DefaultSkeletalMesh 다른 skeleton)을 쓴다.
// 가릴 section 판정은 VBAppearancePieceRules::SectionsToHide 몫이고, 여기서는 가리기가 아니라 component 만 본다.

#include "Misc/AutomationTest.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/Appearance/VBAppearancePiece.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace VBAppearanceComposeTest
{
	const TCHAR* const CubePath = TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube");
	const TCHAR* const OtherSkeletonPath = TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh");
	const FName Hero(TEXT("Protagonist"));

	UVBAppearancePiece* MakePiece(const FGameplayTag& Slot, USkeletalMesh* Mesh)
	{
		UVBAppearancePiece* Piece = NewObject<UVBAppearancePiece>(GetTransientPackage());
		Piece->AppearanceSlot = Slot;
		Piece->FittedMeshes.Add(Hero, Mesh);
		return Piece;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceApplyPiecesTest, "VB.Unit.Appearance.ApplyPieces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceApplyPiecesTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearanceComposeTest;
	namespace Tags = VBGameplayTags;

	USkeletalMesh* Cube = LoadObject<USkeletalMesh>(nullptr, CubePath);
	USkeletalMesh* OtherSkeleton = LoadObject<USkeletalMesh>(nullptr, OtherSkeletonPath);
	if (!TestNotNull(TEXT("engine cube mesh"), Cube) || !TestNotNull(TEXT("engine mesh with another skeleton"), OtherSkeleton))
	{
		return false;
	}

	if (!TestTrue(TEXT("the two engine meshes really have different skeletons"), Cube->GetSkeleton() != OtherSkeleton->GetSkeleton()))
	{
		return false;
	}

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
		OwnedPackage->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());

	AActor* Visual = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("visual actor"), Visual))
	{
		return false;
	}

	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Visual, TEXT("Body"));
	Visual->SetRootComponent(Body);
	Body->RegisterComponent();
	Body->SetSkeletalMeshAsset(Cube);

	const TStrongObjectPtr<UVBAppearancePiece> Top(MakePiece(Tags::Appearance_Slot_Top, Cube));
	const TStrongObjectPtr<UVBAppearancePiece> Bottom(MakePiece(Tags::Appearance_Slot_Bottom, Cube));
	const TStrongObjectPtr<UVBAppearancePiece> WrongBones(MakePiece(Tags::Appearance_Slot_Hands, OtherSkeleton));

	TArray<TObjectPtr<USkeletalMeshComponent>> Components;
	TestEqual(TEXT("two fitting pieces go on, the other-skeleton piece does not"),
		UVBCharacterAppearanceComponent::ApplyPieces(*Body, Hero, {Top.Get(), Bottom.Get(), WrongBones.Get()}, Components), 2);
	for (const USkeletalMeshComponent* Piece : Components)
	{
		if (!TestNotNull(TEXT("piece component"), Piece))
		{
			continue;
		}

		TestTrue(TEXT("piece is attached to the body"), Piece->GetAttachParent() == Body);
		TestTrue(TEXT("piece follows the body's pose"), Piece->LeaderPoseComponent.Get() == Body);
		TestTrue(TEXT("piece does not collide"), Piece->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("piece shows its fitted mesh"), Piece->GetSkeletalMeshAsset() == Cube);
		TestTrue(TEXT("piece belongs to the visual actor"), Piece->GetOwner() == Visual);
	}

	// 두 번째 조립은 첫 번째를 갈아끼운다 - 옛 component 는 쌓이지 않고 사라진다.
	const TArray<TObjectPtr<USkeletalMeshComponent>> Before = Components;
	TestEqual(TEXT("recompose with only the top"), UVBCharacterAppearanceComponent::ApplyPieces(*Body, Hero, {Top.Get()}, Components), 1);
	for (const USkeletalMeshComponent* Old : Before)
	{
		TestTrue(TEXT("the previous piece components were destroyed"), !IsValid(Old) || Old->IsBeingDestroyed());
	}

	// 조각이 맞지 않는 캐릭터는 아무것도 못 받고 (경고), 빈 목록은 모두 지운다.
	TestEqual(TEXT("a piece without a mesh for this character"), UVBCharacterAppearanceComponent::ApplyPieces(*Body, FName(TEXT("Gayeon")), {Top.Get()}, Components), 0);
	TestEqual(TEXT("no pieces"), UVBCharacterAppearanceComponent::ApplyPieces(*Body, Hero, {}, Components), 0);
	TestEqual(TEXT("nothing left over"), Components.Num(), 0);
	return true;
}
