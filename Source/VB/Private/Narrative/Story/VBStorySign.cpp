// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBStorySign.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Interaction/VBSetupValidation.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "VB/VB.h"

AVBStorySign::AVBStorySign()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(RootComponent);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetGenerateOverlapEvents(false);
	Panel->SetCanEverAffectNavigation(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	HideDisplay();
}

FString AVBStorySign::ValidateSetup() const
{
	if (!FactTag.IsValid() || !FGameplayTag::RequestGameplayTag(FactTag.GetTagName(), false).IsValid())
	{
		return TEXT("FactTag must be a registered fact");
	}
	// Categories meta 는 고르는 창만 거른다 - Story 밖의 fact 는 참이 되지 않아 영원히 Before 를 보인다
	if (!FactTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Story"), false)))
	{
		return TEXT("FactTag must be under Story");
	}

	if (RenderMode == EVBStorySignRenderMode::Text)
	{
		if (Before.Text.ToString().TrimStartAndEnd().IsEmpty())
		{
			return TEXT("Before.Text is empty");
		}

		if (After.Text.ToString().TrimStartAndEnd().IsEmpty())
		{
			return TEXT("After.Text is empty");
		}

		if (!TextFont)
		{
			return TEXT("TextFont is missing");
		}

		if (TextFont->FontCacheType != EFontCacheType::Offline)
		{
			return TEXT("TextFont must use an offline atlas for TextRender");
		}

		if (TextFont->GetMaxCharHeight() <= 0.0f)
		{
			return TEXT("TextFont has no usable character height");
		}

		if (!TextMaterial)
		{
			return TEXT("TextMaterial is missing");
		}

		if (!FMath::IsFinite(TextWorldSize) || TextWorldSize <= 0.0f)
		{
			return TEXT("TextWorldSize must be finite and positive");
		}
	}
	else if (RenderMode == EVBStorySignRenderMode::Material)
	{
		if (!Before.Material)
		{
			return TEXT("Before.Material is missing");
		}

		if (!After.Material)
		{
			return TEXT("After.Material is missing");
		}

		if (!PanelMesh)
		{
			return TEXT("PanelMesh is missing");
		}

		if (MaterialSlotName.IsNone() || PanelMesh->GetMaterialIndex(MaterialSlotName) == INDEX_NONE)
		{
			return TEXT("MaterialSlotName does not resolve on PanelMesh");
		}
	}
	else
	{
		return TEXT("RenderMode is unsupported");
	}

	return FString();
}

void AVBStorySign::ConfigureDisplay()
{
	if (RenderMode == EVBStorySignRenderMode::Material)
	{
		Panel->SetStaticMesh(PanelMesh);
	}
	else
	{
		Label->SetFont(TextFont);
		Label->SetTextMaterial(TextMaterial);
		Label->SetWorldSize(TextWorldSize);
		Label->SetTextRenderColor(TextColor);
	}
}

void AVBStorySign::HideDisplay()
{
	Panel->SetVisibility(false);
	Label->SetVisibility(false);
	AppliedState.Reset();
}

void AVBStorySign::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Construction 은 편집 중에도 다시 돌므로, 미리보기가 실제 이야기 상태를 덮으면 안 된다
	if (HasActorBegunPlay())
	{
		return;
	}

	HideDisplay();
	if (GetWorld() && !GetWorld()->IsGameWorld() && ValidateSetup().IsEmpty())
	{
		ConfigureDisplay();
		ApplyState(bPreviewAfter);
	}
}

void AVBStorySign::BeginPlay()
{
	Super::BeginPlay();
	HideDisplay();
	const FString Problem = ValidateSetup();
	if (!Problem.IsEmpty())
	{
		VB_LOG(Error, "%s: story sign unavailable: %s", *GetPathName(), *Problem);
		return;
	}

	ConfigureDisplay();
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &AVBStorySign::RefreshDisplay));
}

void AVBStorySign::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StoryBinding.Disconnect();
	HideDisplay();
	Super::EndPlay(EndPlayReason);
}

void AVBStorySign::RefreshDisplay()
{
	const UVBStoryStateComponent* Story = StoryBinding.GetStory();
	if (!Story)
	{
		HideDisplay();
		AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		if (GameState && WarnedGameState.Get() != GameState)
		{
			WarnedGameState = GameState;
			VB_LOG(Error, "%s: GameState %s has no UVBStoryStateComponent", *GetPathName(), *GetNameSafe(GameState));
		}

		return;
	}

	WarnedGameState.Reset();
	ApplyState(Story->HasFact(FactTag));
}

void AVBStorySign::ApplyState(bool bAfter)
{
	if (AppliedState.IsSet() && AppliedState.GetValue() == bAfter)
	{
		return;
	}

	AppliedState = bAfter;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FVBStorySignState& State = bAfter ? After : Before;
	if (RenderMode == EVBStorySignRenderMode::Text)
	{
		Label->SetText(State.Text);
		Label->SetVisibility(true);
	}
	else
	{
		Panel->SetMaterial(PanelMesh->GetMaterialIndex(MaterialSlotName), State.Material);
		Panel->SetVisibility(true);
	}
}

#if WITH_EDITOR
EDataValidationResult AVBStorySign::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult BaseResult = Super::IsDataValid(Context);
	const EDataValidationResult Result = VBReportSetupProblem(Context, *this, ValidateSetup(),
		BaseResult == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : BaseResult);
	if (Result != EDataValidationResult::Invalid
		&& ((RenderMode == EVBStorySignRenderMode::Text && Before.Text.EqualTo(After.Text))
			|| (RenderMode == EVBStorySignRenderMode::Material && Before.Material == After.Material)))
	{
		Context.AddWarning(FText::FromString(FString::Printf(TEXT("%s: Before and After have the same display"), *GetPathName())));
	}

	return Result;
}
#endif
