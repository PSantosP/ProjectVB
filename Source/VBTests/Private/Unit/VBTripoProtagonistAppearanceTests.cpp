// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 주인공의 실제 외형 에셋을 UVBCharacterAppearanceComponent::ApplyPieces 로 조합해 본다
// DA_Appearance_TripoProtagonist -> BP 몸(SK_VB_Protagonist_Tripo_Base, section 7개) + 기본 조각 2개
// VBAppearanceComposeTests 의 엔진 mesh 에는 section 이름이 없어 숨김을 못 보므로 여기서 모든 LOD 를 본다

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/Appearance/VBAppearancePiece.h"
#include "Character/Appearance/VBCharacterAppearance.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTripoProtagonistAppearanceTest, "VB.Unit.Appearance.TripoProtagonistAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTripoProtagonistAppearanceTest::RunTest(const FString& Parameters)
{
	const UVBCharacterAppearance* Definition = LoadObject<UVBCharacterAppearance>(nullptr,
		TEXT("/Game/VB/Data/Appearance/DA_Appearance_TripoProtagonist.DA_Appearance_TripoProtagonist"));
	if (!TestNotNull(TEXT("protagonist appearance asset"), Definition))
	{
		return false;
	}

	TestEqual(TEXT("character id"), Definition->CharacterId, FName(TEXT("Protagonist")));
	TArray<const UVBAppearancePiece*> Pieces;
	for (const TSoftObjectPtr<UVBAppearancePiece>& Soft : Definition->DefaultPieces)
	{
		const UVBAppearancePiece* Piece = Soft.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("default piece %s loads"), *Soft.ToString()), Piece))
		{
			return false;
		}
		// ApplyPieces 는 메시가 로드된 채로 받는다(준비 후 바꾸기) - 실행 중에는 컴포넌트가 비동기로 먼저 읽는다.
		if (!TestNotNull(*FString::Printf(TEXT("%s has a fitted protagonist mesh"), *Piece->GetName()),
			Piece->FindFittedMesh(Definition->CharacterId).LoadSynchronous()))
		{
			return false;
		}

		Pieces.Add(Piece);
	}

	if (!TestEqual(TEXT("two default pieces (top, bottom)"), Pieces.Num(), 2))
	{
		return false;
	}

	UClass* VisualClass = Definition->VisualActorClass.LoadSynchronous();
	if (!TestNotNull(TEXT("visual actor class"), VisualClass))
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
	};
	AActor* Visual = World->SpawnActor(VisualClass);
	if (!TestNotNull(TEXT("visual actor spawns"), Visual))
	{
		return false;
	}

	USkeletalMeshComponent* Body = nullptr;
	for (USkeletalMeshComponent* Component : TInlineComponentArray<USkeletalMeshComponent*>(Visual))
	{
		if (Component->GetFName() == Definition->PrimaryBodyComponent)
		{
			Body = Component;
		}
	}

	if (!TestNotNull(TEXT("primary body component"), Body) || !TestNotNull(TEXT("body mesh"), Body->GetSkeletalMeshAsset()))
	{
		return false;
	}

	TestEqual(TEXT("body is the seven-section base mesh"), Body->GetSkeletalMeshAsset()->GetName(),
		FString(TEXT("SK_VB_Protagonist_Tripo_Base")));

	TArray<TObjectPtr<USkeletalMeshComponent>> PieceComponents;
	if (!TestEqual(TEXT("both default pieces attach"),
		UVBCharacterAppearanceComponent::ApplyPieces(*Body, Definition->CharacterId, Pieces, PieceComponents), 2))
	{
		return false;
	}

	for (const USkeletalMeshComponent* Piece : PieceComponents)
	{
		TestTrue(TEXT("piece follows the body's pose"), Piece && Piece->LeaderPoseComponent.Get() == Body);
	}

	// 모든 LOD 에서 덮인 section 은 숨고 나머지는 보인다. 생성된 LOD 가 한 부위를 다른 부위에 접어 넣으면 안 된다
	const int32 LodCount = Body->GetSkeletalMeshAsset()->GetLODNum();
	TestTrue(TEXT("base body has the four generated LODs"), LodCount >= 4);
	for (int32 Lod = 0; Lod < LodCount; ++Lod)
	{
		for (const TCHAR* Hidden : { TEXT("TorsoUpperArms"), TEXT("Forearms"), TEXT("HipsLegs"), TEXT("Feet") })
		{
			const int32 Material = Body->GetMaterialIndex(Hidden);
			TestTrue(*FString::Printf(TEXT("LOD%d %s exists"), Lod, Hidden), Material != INDEX_NONE);
			TestFalse(*FString::Printf(TEXT("LOD%d %s hidden under the uniform"), Lod, Hidden),
				Body->IsMaterialSectionShown(Material, Lod));
		}

		for (const TCHAR* Shown : { TEXT("Head"), TEXT("Hair"), TEXT("Hands") })
		{
			const int32 Material = Body->GetMaterialIndex(Shown);
			TestTrue(*FString::Printf(TEXT("LOD%d %s stays visible"), Lod, Shown), Material != INDEX_NONE
				&& Body->IsMaterialSectionShown(Material, Lod));
		}
	}

	return true;
}
#endif
