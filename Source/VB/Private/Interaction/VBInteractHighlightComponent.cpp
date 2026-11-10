// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractHighlightComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "VB/VB.h"

UVBInteractHighlightComponent::UVBInteractHighlightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBInteractHighlightComponent::BeginPlay()
{
	Super::BeginPlay();
	// 조종 여부를 여기서 판정하지 않고 늘 듣는다 - 원격 클라는 컨트롤러 복제가, 플레이 중 스폰은 빙의가 BeginPlay 뒤에 온다.
	//   내 화면의 플레이어인지는 신호를 받을 때 HandleFocusChanged 가 본다(Interactor 가 틱에서 판정하는 것과 같은 이유).
	UVBInteractorComponent* Interactor = GetOwner() ? GetOwner()->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (!Interactor)
	{
		VB_LOG(Warning, "%s: 같은 액터에 UVBInteractorComponent 가 없다 - 상호작용 외곽선이 안 나온다", *GetNameSafe(GetOwner()));
		return;
	}

	BoundInteractor = Interactor;
	Interactor->OnFocusChanged.AddUniqueDynamic(this, &UVBInteractHighlightComponent::HandleFocusChanged);
	HandleFocusChanged(Interactor->GetFocusedInteractable());
}

void UVBInteractHighlightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBInteractorComponent* Interactor = BoundInteractor.Get())
	{
		Interactor->OnFocusChanged.RemoveDynamic(this, &UVBInteractHighlightComponent::HandleFocusChanged);
	}

	BoundInteractor.Reset();
	SetHighlightedActor(nullptr);
	Super::EndPlay(EndPlayReason);
}

void UVBInteractHighlightComponent::HandleFocusChanged(UVBInteractableComponent* NewFocus)
{
	// 같은 캐릭터 클래스를 쓰는 NPC(AVBGASPNPC)도 Interactor 를 갖고, AI 컨트롤러는 서버에서 로컬 조종이라 포커스를 고른다.
	//   플레이어가 조종하는 내 폰만 찍는다 - 아니면 NPC 가 보는 소품에 선이 뜨고, 둘이 같은 대상을 찍으면 선이 남는다.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bMyPlayer = Pawn && Pawn->IsPlayerControlled() && Pawn->IsLocallyControlled();
	SetHighlightedActor(bMyPlayer && NewFocus ? NewFocus->GetOwner() : nullptr);
}

void UVBInteractHighlightComponent::SetHighlightedActor(AActor* NewActor)
{
	if (NewActor == HighlightedActor.Get() && NewActor)
	{
		return;
	}

	RestoreStamped();
	HighlightedActor = NewActor;
	if (!NewActor || OutlineStencilValue <= 0)
	{
		return;
	}
	// 실루엣은 보이는 것 전부다 - 소품은 대리 정적 메시 조각 여럿, NPC 는 스켈레탈 메시라 한 종류만 찍으면 선이 끊긴다.
	TArray<UPrimitiveComponent*> Primitives;
	NewActor->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive || !Primitive->IsVisible())
		{
			continue;
		}

		Stamped.Add({Primitive, static_cast<bool>(Primitive->bRenderCustomDepth), Primitive->CustomDepthStencilValue});
		Primitive->SetCustomDepthStencilValue(OutlineStencilValue);
		Primitive->SetRenderCustomDepth(true);
	}
}

void UVBInteractHighlightComponent::RestoreStamped()
{
	for (const FStampedPrimitive& Entry : Stamped)
	{
		if (UPrimitiveComponent* Primitive = Entry.Primitive.Get())
		{
			Primitive->SetCustomDepthStencilValue(Entry.PreviousStencil);
			Primitive->SetRenderCustomDepth(Entry.bHadCustomDepth);
		}
	}

	Stamped.Reset();
	HighlightedActor.Reset();
}
