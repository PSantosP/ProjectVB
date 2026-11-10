// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Image.h"
#include "Enemy/VBEnemyRegistrySubsystem.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBMapDefinition.h"
#include "Map/VBMapSettings.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UI/Map/VBMinimapWidget.h"
#include "UObject/Package.h"
#include "Unit/VBEnemyBarTestBoss.h"
#include "Unit/VBUITestFixture.h"

// 미니맵 위젯: 숨는 조건과 깨우는 신호, 이번 프레임 아이콘 기록.
//   지도 그림, 머티리얼 픽셀, 실제 HUD 자리는 여기서 안 본다(user PIE). 목표의 테두리 화살표 계산은 Map.* 순수 시험이 본다.

namespace
{
	// 저장점 ID 는 에디터에서만 정한다(EditInstanceOnly). 시험은 반사로 쓴다 - 친구 클래스를 또 만들면 유니티 빌드에서 겹친다.
	void SetPointId(AVBSavePoint& Point, const FGameplayTag& Id)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(AVBSavePoint::StaticClass(), TEXT("PointId"));
		if (Property)
		{
			*Property->ContainerPtrToValuePtr<FGameplayTag>(&Point) = Id;
		}
	}

	AVBSavePoint* SpawnPoint(UWorld& World, const FGameplayTag& Id, const FVector& Location)
	{
		AVBSavePoint* Point = World.SpawnActor<AVBSavePoint>(Location, FRotator::ZeroRotator);
		if (Point)
		{
			SetPointId(*Point, Id);
			Point->DispatchBeginPlay();
		}

		return Point;
	}

	const FVBMinimapDrawRecord* FindKind(const TArray<FVBMinimapDrawRecord>& Records, EVBMinimapIconKind Kind)
	{
		return Records.FindByPredicate([Kind](const FVBMinimapDrawRecord& Record) { return Record.Kind == Kind; });
	}

	int32 CountKind(const TArray<FVBMinimapDrawRecord>& Records, EVBMinimapIconKind Kind)
	{
		return Records.FilterByPredicate([Kind](const FVBMinimapDrawRecord& Record) { return Record.Kind == Kind; }).Num();
	}

	// 지도 정의 픽스처: 2000 m 정사각형, 가운데가 원점. 그림은 임시 텍스처(등록된 실제 정의는 Minimap.RegisteredDefinitions 가 본다).
	UVBMapDefinition* MakeFixtureDefinition()
	{
		UVBMapDefinition* Definition = NewObject<UVBMapDefinition>(GetTransientPackage());
		Definition->Texture = UTexture2D::CreateTransient(4, 4);
		Definition->WorldMin = FVector2D(-100000.0, -100000.0);
		Definition->WorldMax = FVector2D(100000.0, 100000.0);
		return Definition;
	}

	// 원점의 폰(yaw 90)을 빙의하고 카메라를 yaw 0 으로 세운다 -> 지도 위 = +X, 폰은 카메라의 오른쪽.
	//   맨 APawn 은 뿌리 컴포넌트가 없어 자리, 방향을 못 든다 - 뿌리를 달고 돌린다.
	APawn* PossessPawnFacingRight(FAutomationTestBase& Test, VBUITest::FRowFixture& Fixture)
	{
		UWorld& World = *Fixture.World;
		APawn* Pawn = World.SpawnActor<APawn>();
		if (!Test.TestNotNull(TEXT("pawn"), Pawn))
		{
			return nullptr;
		}

		USceneComponent* PawnRoot = NewObject<USceneComponent>(Pawn, TEXT("Root"));
		Pawn->SetRootComponent(PawnRoot);
		PawnRoot->RegisterComponent();
		Pawn->SetActorRotation(FRotator(0.0f, 90.0f, 0.0f));
		Fixture.Controller->Possess(Pawn);
		// 빙의가 조준을 폰 방향(90)으로 맞춘다 - 카메라를 0 으로 돌린다.
		//   GetPlayerViewPoint 는 카메라 캐시 시각이 0 보다 클 때만 캐시를 읽고, 아니면 시점 대상(폰)의 회전을 쓴다(엔진 PlayerController.cpp).
		//   격리 월드의 시각은 0 이라 시각을 세운 뒤 한 번 갱신한다 - 게임에서는 카메라가 매 프레임 갱신된다.
		Fixture.Controller->SetControlRotation(FRotator::ZeroRotator);
		APlayerCameraManager* Camera = Fixture.Controller->PlayerCameraManager;
		if (!Test.TestNotNull(TEXT("camera manager"), Camera))
		{
			return nullptr;
		}

		World.TimeSeconds = 1.0;
		Camera->UpdateCamera(0.0f);
		if (!Test.TestTrue(TEXT("camera cache is live"), Camera->GetCameraCacheTime() > 0.0f))
		{
			return nullptr;
		}

		return Pawn;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMinimapWidgetTest, "VB.Unit.Minimap.Widget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMinimapWidgetTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UWorld& World = *Fixture.World;
	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(&World);
	if (!TestNotNull(TEXT("travel subsystem"), Travel))
	{
		return false;
	}

	UVBMapDefinition* Definition = MakeFixtureDefinition();
	if (!TestTrue(TEXT("fixture definition is usable"), Definition->IsUsable()))
	{
		return false;
	}

	UVBMinimapWidget* Widget = Fixture.MakeRow<UVBMinimapWidget>(*this, nullptr);
	if (!Widget)
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);

	// 이 격리 월드에는 지도 설정 줄이 없다 -> 정의 없음 -> 접힘.
	TestTrue(TEXT("no map definition for this world: collapsed"), Widget->GetVisibility() == ESlateVisibility::Collapsed);
	Widget->SetMapDefinition(Definition);
	TestTrue(TEXT("definition but no pawn: still collapsed"), Widget->GetVisibility() == ESlateVisibility::Collapsed);

	// 빙의 신호가 접힌 위젯을 깨운다(접힌 위젯은 Tick 이 없다).
	APawn* Pawn = PossessPawnFacingRight(*this, Fixture);
	if (!Pawn)
	{
		return false;
	}

	TestTrue(TEXT("possession wakes it"), Widget->GetVisibility() == ESlateVisibility::HitTestInvisible);

	// 기본 반지름 60 m. 앞(+X) 30 m 에 발견한 점, 오른쪽 20 m 에 안 발견한 점.
	const FGameplayTag NearId = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.A"));
	const FGameplayTag HiddenId = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.B"));
	AVBSavePoint* Near = SpawnPoint(World, NearId, FVector(3000.0, 0.0, 0.0));
	AVBSavePoint* Hidden = SpawnPoint(World, HiddenId, FVector(0.0, 2000.0, 0.0));
	if (!TestNotNull(TEXT("near point"), Near) || !TestNotNull(TEXT("hidden point"), Hidden))
	{
		return false;
	}

	TestTrue(TEXT("discover the near point"), Travel->Discover(NearId));

	// 200 x 200 칸 -> 지도 반지름 100 UI 단위.
	const FGeometry Square = FGeometry::MakeRoot(FVector2D(200.0, 200.0), FSlateLayoutTransform());
	Widget->UpdateFrame(Square, 0.0f);
	const TArray<FVBMinimapDrawRecord>& Records = Widget->GetDrawRecords();
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Fixture.Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		TestTrue(TEXT("the map reads the camera yaw (0), not the pawn yaw (90)"), FMath::IsNearlyZero(ViewRotation.Yaw, 0.01));
	}

	if (TestEqual(TEXT("only the near discovered point and the player"), Records.Num(), 2))
	{
		const FVBMinimapDrawRecord* Point = FindKind(Records, EVBMinimapIconKind::SavePoint);
		if (TestNotNull(TEXT("near discovered save point drawn"), Point))
		{
			// 카메라 앞 = 위. 30 m / 60 m = 반지름의 절반.
			TestTrue(TEXT("forward point is straight up"), FMath::IsNearlyZero(Point->LocalOffset.X, 0.01));
			TestTrue(TEXT("half the radius above the centre"), FMath::IsNearlyEqual(Point->LocalOffset.Y, -50.0, 0.01));
		}

		TestTrue(TEXT("player drawn last"), Records.Last().Kind == EVBMinimapIconKind::Player);
		// 폰은 카메라에서 오른쪽으로 90 도 -> 화살표는 위에서 시계 방향 90 도(오른쪽).
		TestTrue(TEXT("player arrow turns by pawn yaw - map yaw"), FMath::IsNearlyEqual(Records.Last().AngleDegrees, 90.0f, 0.01f));
	}

	// 발견한 점도 테두리 밖(앞 300 m)이면 숨는다 - 저장점은 테두리에 붙이지 않는다.
	Near->SetActorLocation(FVector(30000.0, 0.0, 0.0));
	Widget->UpdateFrame(Square, 0.0f);
	TestEqual(TEXT("a far discovered point is hidden; only the player remains"), Widget->GetDrawRecords().Num(), 1);

	// 빙의가 풀리면 신호로 접히고 기록이 비워진다.
	Fixture.Controller->UnPossess();
	TestTrue(TEXT("unpossess collapses"), Widget->GetVisibility() == ESlateVisibility::Collapsed);
	TestEqual(TEXT("collapsing clears the records"), Widget->GetDrawRecords().Num(), 0);

	// 해체 뒤에는 신호를 안 듣는다(대조). 다시 구성하면 다시 듣는다.
	Life.Destruct(*Widget);
	Fixture.Controller->Possess(Pawn);
	TestTrue(TEXT("a destructed widget does not react to possession"), Widget->GetVisibility() == ESlateVisibility::Collapsed);
	Fixture.Controller->UnPossess();
	Life.Construct(*Widget);
	Widget->SetMapDefinition(Definition);
	Fixture.Controller->Possess(Pawn);
	TestTrue(TEXT("reconstructed widget listens again"), Widget->GetVisibility() == ESlateVisibility::HitTestInvisible);
	Life.Destruct(*Widget);
	return true;
}

// 적 점: 전투 대상이 나인 살아 있는 적만, 반경 안만. 풀리면, 파괴되면 서서히 사라진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMinimapThreatTest, "VB.Unit.Minimap.Threat",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMinimapThreatTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UWorld& World = *Fixture.World;
	UVBEnemyRegistrySubsystem* Registry = World.GetSubsystem<UVBEnemyRegistrySubsystem>();
	if (!TestNotNull(TEXT("enemy registry"), Registry))
	{
		return false;
	}

	UVBMinimapWidget* Widget = Fixture.MakeRow<UVBMinimapWidget>(*this, nullptr);
	if (!Widget)
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);
	Widget->SetMapDefinition(MakeFixtureDefinition());
	APawn* Pawn = PossessPawnFacingRight(*this, Fixture);
	if (!Pawn)
	{
		return false;
	}

	// AVBEnemyBase 는 Abstract 다 - 보스 바 시험의 구체 클래스를 빌린다(전투 컴포넌트, 죽음 판정은 적 베이스 그대로).
	auto SpawnEnemy = [&World, Registry](const FVector& Location)
	{
		AVBEnemyBarTestBoss* Enemy = World.SpawnActor<AVBEnemyBarTestBoss>(Location, FRotator::ZeroRotator);
		Registry->Register(Enemy);
		return Enemy;
	};
	AVBEnemyBarTestBoss* Fighter = SpawnEnemy(FVector(3000.0, 0.0, 0.0)); // 앞 30 m, 나와 싸움
	AVBEnemyBarTestBoss* Busy = SpawnEnemy(FVector(0.0, 2000.0, 0.0)); // 오른쪽 20 m, 다른 대상과 싸움
	AVBEnemyBarTestBoss* Far = SpawnEnemy(FVector(30000.0, 0.0, 0.0)); // 앞 300 m, 나와 싸움(반경 밖)
	AVBEnemyBarTestBoss* Dead = SpawnEnemy(FVector(-2000.0, 0.0, 0.0)); // 뒤 20 m, 나와 싸우지만 죽음
	AActor* Someone = World.SpawnActor<AActor>();
	if (!TestNotNull(TEXT("fighter"), Fighter) || !TestNotNull(TEXT("busy"), Busy) || !TestNotNull(TEXT("far"), Far)
		|| !TestNotNull(TEXT("dead"), Dead) || !TestNotNull(TEXT("someone"), Someone))
	{
		return false;
	}

	UAbilitySystemComponent* DeadASC = Dead->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("dead enemy ASC"), DeadASC))
	{
		return false;
	}

	DeadASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	if (!TestTrue(TEXT("dead enemy reads as dead"), Dead->IsDead()))
	{
		return false;
	}

	Fighter->GetCombatComponent()->SetCombatTarget(Pawn);
	Busy->GetCombatComponent()->SetCombatTarget(Someone);
	Far->GetCombatComponent()->SetCombatTarget(Pawn);
	Dead->GetCombatComponent()->SetCombatTarget(Pawn);

	// 200 x 200 칸 -> 지도 반지름 100 UI 단위, 기본 월드 반지름 60 m. 1 초면 나타나기(0.15 s)가 끝난다.
	const FGeometry Square = FGeometry::MakeRoot(FVector2D(200.0, 200.0), FSlateLayoutTransform());
	Widget->UpdateFrame(Square, 1.0f);
	if (TestEqual(TEXT("only the living enemy fighting me inside the radius"),
		CountKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat), 1))
	{
		const FVBMinimapDrawRecord* Dot = FindKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat);
		TestTrue(TEXT("fighter ahead is straight up, half the radius"), FMath::IsNearlyZero(Dot->LocalOffset.X, 0.01)
			&& FMath::IsNearlyEqual(Dot->LocalOffset.Y, -50.0, 0.01));
		TestTrue(TEXT("fully faded in"), FMath::IsNearlyEqual(Dot->Opacity, 1.0f));
	}

	TestTrue(TEXT("player still drawn last"), Widget->GetDrawRecords().Last().Kind == EVBMinimapIconKind::Player);

	// 전투가 풀리면 바로 안 사라지고 0.5 초에 걸쳐 흐려진다.
	Fighter->GetCombatComponent()->SetCombatTarget(nullptr);
	Widget->UpdateFrame(Square, 0.25f);
	const FVBMinimapDrawRecord* Fading = FindKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat);
	TestTrue(TEXT("dropped combat: half faded"), Fading && FMath::IsNearlyEqual(Fading->Opacity, 0.5f, 0.01f));
	Widget->UpdateFrame(Square, 0.5f);
	TestEqual(TEXT("dropped combat: gone after the fade"), CountKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat), 0);

	// 다시 싸우다 파괴되면 마지막 자리(오른쪽 30 m)에서 흐려진다.
	Fighter->GetCombatComponent()->SetCombatTarget(Pawn);
	Fighter->SetActorLocation(FVector(0.0, 3000.0, 0.0));
	Widget->UpdateFrame(Square, 1.0f);
	Fighter->Destroy();
	Widget->UpdateFrame(Square, 0.25f);
	const FVBMinimapDrawRecord* Ghost = FindKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat);
	TestTrue(TEXT("destroyed enemy fades at its last spot"), Ghost && FMath::IsNearlyEqual(Ghost->LocalOffset.X, 50.0, 0.01)
		&& FMath::IsNearlyZero(Ghost->LocalOffset.Y, 0.01) && FMath::IsNearlyEqual(Ghost->Opacity, 0.5f, 0.01f));
	Widget->UpdateFrame(Square, 0.5f);
	TestEqual(TEXT("destroyed enemy gone after the fade"), CountKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat), 0);

	// 접히면 점을 잊는다 - 다시 깨어나면 0 부터 나타난다(옛 투명도를 이어 그리지 않는다).
	Busy->GetCombatComponent()->SetCombatTarget(Pawn);
	Widget->UpdateFrame(Square, 1.0f);
	Fixture.Controller->UnPossess();
	TestEqual(TEXT("collapsing clears the records"), Widget->GetDrawRecords().Num(), 0);
	Fixture.Controller->Possess(Pawn);
	Widget->UpdateFrame(Square, 0.0f);
	const FVBMinimapDrawRecord* Again = FindKind(Widget->GetDrawRecords(), EVBMinimapIconKind::Threat);
	TestTrue(TEXT("woken widget starts the dot from zero"), Again && FMath::IsNearlyZero(Again->Opacity));
	Life.Destruct(*Widget);
	return true;
}

// 실제 WBP_Minimap: 자기 트리 없이 C++ 가 지도 칸을 짓고, 그 그림이 M_UI_Minimap 에서 만든 동적 머티리얼이다(값만 든 블루프린트).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMinimapAssetTest, "VB.Unit.Minimap.Asset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMinimapAssetTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UVBMinimapWidget* Widget = Fixture.MakeRow<UVBMinimapWidget>(*this, TEXT("/Game/VB/UI/Map/WBP_Minimap.WBP_Minimap_C"));
	if (!Widget)
	{
		return false;
	}

	VBUITest::FSlateLife Life;
	Life.Construct(*Widget);
	const UImage* MapImage = Widget->WidgetTree ? Cast<UImage>(Widget->WidgetTree->FindWidget(TEXT("MapImage"))) : nullptr;
	if (TestNotNull(TEXT("C++ built the map image"), MapImage))
	{
		const UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(MapImage->GetBrush().GetResourceObject());
		if (TestNotNull(TEXT("the map image draws a dynamic material"), Instance))
		{
			TestEqual(TEXT("made from M_UI_Minimap"), GetPathNameSafe(Instance->Parent),
				FString(TEXT("/Game/VB/UI/Map/M_UI_Minimap.M_UI_Minimap")));
		}
	}

	Life.Destruct(*Widget);
	return true;
}

// 등록된 지도 정의 전부: DefaultGame.ini 의 줄마다 그 맵이 실제로 있고, 구운 정의를 쓸 수 있고,
//   그림은 2048 정사각형 BC1, 밉 없음, UI 그룹, 범위는 정사각형이다. 굽는 도구가 그림과 범위를 함께 쓴다 - 한쪽만 고치면 걸린다.
//   맵을 지우고 줄만 남기면 여기서 걸린다. 줄이 없으면 지도 없는 맵뿐이라는 뜻이고 정상이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMinimapRegisteredDefinitionsTest, "VB.Unit.Minimap.RegisteredDefinitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMinimapRegisteredDefinitionsTest::RunTest(const FString& Parameters)
{
	const TArray<FVBMapEntry>& Entries = GetDefault<UVBMapSettings>()->Maps;
	if (Entries.IsEmpty())
	{
		AddInfo(TEXT("no map definition registered - every map is map-less"));
		return true;
	}

	for (const FVBMapEntry& Entry : Entries)
	{
		const FString WorldPackage = Entry.World.ToSoftObjectPath().GetLongPackageName();
		if (!TestTrue(FString::Printf(TEXT("%s exists"), *WorldPackage), FPackageName::DoesPackageExist(WorldPackage)))
		{
			continue;
		}

		const UVBMapDefinition* Definition = GetDefault<UVBMapSettings>()->FindForPackage(WorldPackage);
		if (!TestNotNull(FString::Printf(TEXT("%s has a usable map definition"), *WorldPackage), Definition))
		{
			continue;
		}

		const FVector2D Extent = Definition->WorldMax - Definition->WorldMin;
		TestTrue(FString::Printf(TEXT("%s baked bounds are square"), *WorldPackage), FMath::IsNearlyEqual(Extent.X, Extent.Y, 1.0));
		if (!TestNotNull(FString::Printf(TEXT("%s has a baked texture"), *WorldPackage), Definition->Texture.Get()))
		{
			continue;
		}

		const UTexture2D* Texture = Definition->Texture;
		// 헤드리스(렌더러 없음)에서는 플랫폼 데이터가 없어 GetSizeX 가 0 이다 - 원본 크기와 설정을 본다.
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("source width"), static_cast<int32>(Texture->Source.GetSizeX()), 2048);
		TestEqual(TEXT("source height"), static_cast<int32>(Texture->Source.GetSizeY()), 2048);
		TestTrue(TEXT("no mipmaps generated"), Texture->MipGenSettings == TMGS_NoMipmaps);
#endif
		TestTrue(TEXT("no alpha (BC1, 2 MiB)"), Texture->CompressionNoAlpha);
		TestTrue(TEXT("default compression"), Texture->CompressionSettings == TC_Default);
		TestTrue(TEXT("UI group"), Texture->LODGroup == TEXTUREGROUP_UI);
		TestTrue(TEXT("never streamed"), Texture->NeverStream);
	}

	return true;
}
#endif
