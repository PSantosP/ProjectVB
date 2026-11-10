// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBPowerSwitch.h"

#include "Components/StaticMeshComponent.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBPowerPanel.h"
#include "VB/VB.h"

AVBPowerSwitch::AVBPowerSwitch()
{
	// 켜짐(보여 주기 값)이 클라의 표시등, 프롬프트로 가야 한다 - 컴포넌트 복제는 액터가 복제돼야 돈다.
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;

	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Body);
	// 클래스 기본값으로 끈다 - 컴포넌트가 등록되며 대기 중인 저장을 읽기 전에 꺼져 있어야 옛 저장의 켜짐이 안 들어온다.
	Interactable->SetSaveState(false);
}

void AVBPowerSwitch::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBPowerSwitch::HandleInteracted);
	}
}

void AVBPowerSwitch::HandleInteracted(AActor* InteractingActor)
{
	if (!Panel)
	{
		VB_LOG(Error, "%s: 제어함이 비었다 - 누름을 버린다(배치 오류)", *GetName());
		return;
	}

	Panel->RequestToggle(*this, InteractingActor);
}
