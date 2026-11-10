// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBSavePoint.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Map/VBFastTravelComponent.h"
#include "GameFramework/Controller.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VB/VB.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AVBSavePoint::AVBSavePoint()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);

	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(RootComponent);

	ArrivalPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ArrivalPoint"));
	ArrivalPoint->SetupAttachment(RootComponent);
	ArrivalPoint->SetRelativeLocation(FVector(150.0f, 0.0f, 0.0f));

	TravelInteractable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("TravelInteractable"));
	TravelInteractable->SetupAttachment(RootComponent);
	// 켜짐 상태를 저장하지 않는다: 남길 상태가 없고, 이름표가 같은 액터 이름이라 저장 상호작용의 켜짐과 섞인다.
	TravelInteractable->SetSaveState(false);

	// 목적지 목록은 떠 있는 지점에서 나오므로 공간 로딩을 끈다(이미 배치된 저장점은 다시 저장해야 반영된다).
	// 에디터 전용 값이라 게임 빌드에는 없다.
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}

FTransform AVBSavePoint::GetArrivalTransform() const
{
	return ArrivalPoint->GetComponentTransform();
}

float AVBSavePoint::GetTravelInteractRange() const
{
	return TravelInteractable->GetConfig()->InteractRange;
}

#if WITH_EDITOR
EDataValidationResult AVBSavePoint::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 클래스 기본값(배치 전)은 ID 가 비어 있는 것이 정상이다 - 배치된 것만 본다.
	if (!IsTemplate() && !UVBTravelPointSubsystem::IsUsablePointId(PointId))
	{
		// 경고로 둔다: 저장점 노릇은 멀쩡하다. 목적지가 아닌 것을 알리기만 한다.
		Context.AddWarning(FText::FromString(FString::Printf(TEXT("%s: 지점 ID(PointId) 가 없다 - 저장은 되지만 빠른 이동 목적지가 되지 않는다."),
			*GetActorLabel())));
	}

	return Result;
}
#endif

void AVBSavePoint::BeginPlay()
{
	Super::BeginPlay();

	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	const bool bTravelPoint = Travel && Travel->Register(this);
	// 목적지가 될 수 없는 저장점(ID 없음, 겹침)에서는 이동 상호작용을 끈다.
	TravelInteractable->SetInteractionEnabled(bTravelPoint);
	if (bTravelPoint && TravelInteractable->GetConfig()->Mode == EVBInteractMode::Auto)
	{
		VB_LOG(Warning, "%s: 이동 상호작용 설정이 Auto 다 - 다가가기만 해도 목록이 열린다(DA_Interactable_FastTravel = Tap)", *GetName());
	}

	// 이동 상호작용은 눌러서 고르는 초점 대상이다. 원점(바닥)에 두면 시야 검사가 바닥에 막히고 높이 차가 커서
	//   아무도 고르지 못한다 - 줍는 물건처럼 표지 위로 띄운다.
	TravelInteractable->PlaceAboveRestingMesh(*Marker);

	if (Interactable->GetConfig()->Mode != EVBInteractMode::Auto)
	{
		VB_LOG(Warning, "%s: 설정 Mode 가 Auto 가 아니다 - 다가가도 저장되지 않고 눌러야 저장된다", *GetName());
	}

	if (HasAuthority())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBSavePoint::HandleInteracted);
		TravelInteractable->OnInteracted.AddUniqueDynamic(this, &AVBSavePoint::HandleTravelInteracted);
	}
}

void AVBSavePoint::HandleInteracted(AActor* InteractingActor)
{
	APawn* Pawn = Cast<APawn>(InteractingActor);
	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	if (!Pawn || !Save)
	{
		VB_LOG(Warning, "%s: 저장 못 함 - 폰 %s, 저장 서브시스템 %s", *GetName(),
			Pawn ? TEXT("있음") : TEXT("없음"), Save ? TEXT("있음") : TEXT("없음"));
		return;
	}
	// 발견이 먼저다 - 이 방문이 부르는 저장에 발견이 이미 들어 있게.
	if (UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this); Travel && UVBTravelPointSubsystem::IsUsablePointId(PointId))
	{
		Travel->Discover(PointId);
	}

	VB_LOG(Log, "%s: 저장 포인트 - 자동 저장", *GetName());
	Save->SaveCurrentGame(Pawn);
}

void AVBSavePoint::HandleTravelInteracted(AActor* InteractingActor)
{
	const APawn* Pawn = Cast<APawn>(InteractingActor);
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	UVBFastTravelComponent* FastTravel = Controller ? Controller->FindComponentByClass<UVBFastTravelComponent>() : nullptr;
	if (!FastTravel)
	{
		VB_LOG(Warning, "%s: 빠른 이동 못 엶 - 누른 폰의 컨트롤러에 빠른 이동 컴포넌트가 없다", *GetName());
		return;
	}
	// 여기서도 발견을 남긴다 - 설정 순서와 무관하게 이 지점에 왔음을 기록한다.
	if (UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
	{
		Travel->Discover(PointId);
	}

	FastTravel->OpenTravel(this);
}

void AVBSavePoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this))
	{
		Travel->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}
