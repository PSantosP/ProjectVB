// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBFastTravelComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBPlayerRelocationComponent.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Narrative/Progress/VBProgressCatalogSubsystem.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "VB/VB.h"

UVBFastTravelComponent::UVBFastTravelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	// 서버 -> 소유 클라이언트 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다(상점 컴포넌트와 같다).
	SetIsReplicatedByDefault(true);
}

void UVBFastTravelComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (UVBPlayerRelocationComponent* Relocation = FindRelocation())
	{
		Relocation->OnRelocationStarted.AddUObject(this, &ThisClass::CloseTravel);
	}
}

bool UVBFastTravelComponent::IsTravelling() const
{
	const UVBPlayerRelocationComponent* Relocation = FindRelocation();
	return Relocation && Relocation->IsRelocating();
}

UVBPlayerRelocationComponent* UVBFastTravelComponent::FindRelocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UVBPlayerRelocationComponent>() : nullptr;
}

bool UVBFastTravelComponent::OpenTravel(AVBSavePoint* FromPoint)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "빠른 이동 열기를 권한 없는 쪽에서 불렀다 - 무시한다(이동은 서버만)");
		return false;
	}

	const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	if (!FromPoint || !Travel || Travel->FindPoint(FromPoint->GetPointId()) != FromPoint)
	{
		VB_LOG(Warning, "빠른 이동 열기 거절: %s 는 등록된 지점이 아니다(지점 ID 없음·겹침)", *GetNameSafe(FromPoint));
		return false;
	}

	const bool bTravelling = IsTravelling();
	if (IsTravelOpen() || bTravelling)
	{
		VB_LOG(Log, "빠른 이동 열기 거절: %s", bTravelling ? TEXT("이동 중") : TEXT("이미 열려 있다"));
		return false;
	}

	ServerFrom = FromPoint;
	VB_LOG(Log, "빠른 이동 열림: %s", *FromPoint->GetPointId().ToString());
	SendView();
	return true;
}

void UVBFastTravelComponent::CloseTravel()
{
	if (!IsTravelOpen())
	{
		return;
	}

	ServerFrom.Reset();
	ClientCloseTravel();
}

void UVBFastTravelComponent::RequestTravel(const FGameplayTag& Destination)
{
	if (View.IsActive())
	{
		ServerTravel(View.FromPointId, Destination);
	}
}

void UVBFastTravelComponent::RequestClose()
{
	if (View.IsActive())
	{
		ServerClose();
	}
}

EVBFastTravelResult UVBFastTravelComponent::PreviewTravel(const FGameplayTag& Destination) const
{
	if (!View.IsActive())
	{
		return EVBFastTravelResult::NotOpen;
	}

	if (Destination == View.FromPointId)
	{
		return EVBFastTravelResult::SamePoint;
	}

	const bool bListed = View.Entries.ContainsByPredicate(
		[&Destination](const FVBFastTravelEntry& Entry) { return Entry.PointId == Destination; });
	return bListed ? EVBFastTravelResult::Ok : EVBFastTravelResult::UnknownDestination;
}

const TCHAR* UVBFastTravelComponent::LexToString(EVBFastTravelResult Result)
{
	switch (Result)
	{
	case EVBFastTravelResult::Ok: return TEXT("Ok");
	case EVBFastTravelResult::NotOpen: return TEXT("NotOpen");
	case EVBFastTravelResult::UnknownDestination: return TEXT("UnknownDestination");
	case EVBFastTravelResult::SamePoint: return TEXT("SamePoint");
	case EVBFastTravelResult::NotAtPoint: return TEXT("NotAtPoint");
	case EVBFastTravelResult::InCombat: return TEXT("InCombat");
	case EVBFastTravelResult::AlreadyTravelling: return TEXT("AlreadyTravelling");
	}

	return TEXT("Unknown");
}

void UVBFastTravelComponent::BeginPlay()
{
	Super::BeginPlay();
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	// 클라는 자기 PC 만 받는다 - 권한이 없으면 곧 소유 클라이언트다.
	if (!Owner->HasAuthority())
	{
		ServerRequestDiscovered();
		return;
	}

	const APlayerController* Controller = Cast<APlayerController>(Owner);
	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	if (Controller && !Controller->IsLocalController() && Travel)
	{
		DiscoveredChangedHandle = Travel->OnDiscoveredChanged.AddUObject(this, &ThisClass::SendDiscovered);
	}
}

void UVBFastTravelComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 묶은 것은 서버 쪽 원격 PC 뿐이다. 월드 해체 중이라 서브시스템이 없으면 건너뛴다 - 서브시스템이 함께 죽는다.
	if (DiscoveredChangedHandle.IsValid())
	{
		if (UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
		{
			Travel->OnDiscoveredChanged.Remove(DiscoveredChangedHandle);
		}

		DiscoveredChangedHandle.Reset();
	}

	ServerFrom.Reset();
	View = FVBFastTravelView();
	Super::EndPlay(EndPlayReason);
}

APawn* UVBFastTravelComponent::GetPawn() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	return Controller ? Controller->GetPawn() : nullptr;
}

EVBFastTravelResult UVBFastTravelComponent::CheckTravel(const FGameplayTag& FromPointId, const FGameplayTag& Destination) const
{
	if (IsTravelling())
	{
		return EVBFastTravelResult::AlreadyTravelling;
	}

	const AVBSavePoint* From = ServerFrom.Get();
	if (!From || From->GetPointId() != FromPointId)
	{
		return EVBFastTravelResult::NotOpen;
	}

	if (Destination == FromPointId)
	{
		return EVBFastTravelResult::SamePoint;
	}

	const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	if (!Travel || !Travel->IsDiscovered(Destination) || !Travel->FindPoint(Destination))
	{
		return EVBFastTravelResult::UnknownDestination;
	}

	const APawn* Pawn = GetPawn();
	// 거리 상한 = 저장점의 이동 상호작용 범위 + 여유. 누른 자리 근처에서만 쓴다(목록을 연 채 걸어가 다른 곳에서 쓰지 못하게).
	const float MaxDistance = From->GetTravelInteractRange() + DistanceSlack;
	if (!Pawn || UVBAbilitySystemStatics::IsActorDead(Pawn)
		|| FVector::DistSquared(Pawn->GetActorLocation(), From->GetActorLocation()) > FMath::Square(MaxDistance))
	{
		return EVBFastTravelResult::NotAtPoint;
	}

	const UVBCombatCoordinatorSubsystem* Combat = UVBCombatCoordinatorSubsystem::Get(this);
	if (Combat && Combat->IsEngaged(Pawn))
	{
		return EVBFastTravelResult::InCombat;
	}

	return EVBFastTravelResult::Ok;
}

void UVBFastTravelComponent::ServerTravel_Implementation(const FGameplayTag& FromPointId, const FGameplayTag& Destination)
{
	const EVBFastTravelResult Result = CheckTravel(FromPointId, Destination);
	if (Result != EVBFastTravelResult::Ok)
	{
		VB_LOG(Log, "빠른 이동 거절: %s -> %s - %s", *FromPointId.ToString(), *Destination.ToString(), LexToString(Result));
		// 목록이 낡았을 수 있다(그 사이 지점이 사라짐) - 열려 있으면 지금 목록으로 다시 그리게 한다.
		if (IsTravelOpen())
		{
			SendView();
		}

		return;
	}

	UVBPlayerRelocationComponent* Relocation = FindRelocation();
	if (!Relocation)
	{
		VB_LOG(Error, "빠른 이동: PC 에 UVBPlayerRelocationComponent 가 없다 - 이동 취소");
		return;
	}

	VB_LOG(Log, "빠른 이동: %s -> %s", *FromPointId.ToString(), *Destination.ToString());
	Relocation->StartPointTravel(Destination, FadeOutSeconds, FadeInSeconds);
}

void UVBFastTravelComponent::ServerClose_Implementation()
{
	CloseTravel();
}

void UVBFastTravelComponent::ClientShowTravel_Implementation(const FVBFastTravelView& InView)
{
	View = InView;
	OnViewChanged.Broadcast();
}

void UVBFastTravelComponent::ClientCloseTravel_Implementation()
{
	View = FVBFastTravelView();
	OnViewChanged.Broadcast();
}

void UVBFastTravelComponent::ServerRequestDiscovered_Implementation()
{
	// 요청 횟수를 막지 않는다: 목록이 작고, 되풀이 요청은 그 클라의 reliable 버퍼만 넘쳐 그 클라만 끊긴다(의도적으로 둠).
	SendDiscovered();
}

void UVBFastTravelComponent::ClientSetDiscovered_Implementation(const TArray<FGameplayTag>& ServerDiscovered)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return;
	}

	if (UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
	{
		Travel->ApplyServerDiscovered(ServerDiscovered);
	}
}

void UVBFastTravelComponent::SendDiscovered()
{
	if (const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
	{
		ClientSetDiscovered(Travel->GetDiscovered());
	}
}

void UVBFastTravelComponent::SendView()
{
	const AVBSavePoint* From = ServerFrom.Get();
	const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	if (!From || !Travel)
	{
		CloseTravel();
		return;
	}

	FVBFastTravelView NewView;
	NewView.FromPointId = From->GetPointId();
	for (const AVBSavePoint* Point : Travel->GetPoints())
	{
		if (Point != From && Travel->IsDiscovered(Point->GetPointId()))
		{
			FVBFastTravelEntry& Entry = NewView.Entries.AddDefaulted_GetRef();
			Entry.PointId = Point->GetPointId();
			Entry.DisplayName = Point->GetDisplayName();
			Entry.RegionId = Point->GetRegionId();
		}
	}
	// 지역 순서(지역 정의의 SortOrder, 없는 지역은 뒤) -> 이름. 같은 상황이면 같은 순서.
	const UVBProgressCatalogSubsystem* Catalog = UVBProgressCatalogSubsystem::Get(this);
	auto RegionOrder = [Catalog](const FGameplayTag& RegionId)
	{
		const UVBRegionDefinition* Region = Catalog ? Catalog->FindRegion(RegionId) : nullptr;
		return Region ? Region->SortOrder : MAX_int32;
	};
	NewView.Entries.Sort([&RegionOrder](const FVBFastTravelEntry& A, const FVBFastTravelEntry& B)
	{
		const int32 OrderA = RegionOrder(A.RegionId);
		const int32 OrderB = RegionOrder(B.RegionId);
		return OrderA != OrderB ? OrderA < OrderB : A.DisplayName.CompareTo(B.DisplayName) < 0;
	});
	ClientShowTravel(NewView);
}
