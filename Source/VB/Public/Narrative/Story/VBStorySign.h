// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "VBStorySign.generated.h"

class UFont;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EVBStorySignRenderMode : uint8
{
	Text,
	Material
};

USTRUCT(BlueprintType)
struct FVBStorySignState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	TObjectPtr<UMaterialInterface> Material;
};

// 읽기만 하는 월드 표지판이다. 사실과 저장은 복제되는 story component 가 가진다
UCLASS()
class VB_API AVBStorySign : public AActor
{
	GENERATED_BODY()

public:
	AVBStorySign();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(Categories="Story"))
	FGameplayTag FactTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	EVBStorySignRenderMode RenderMode = EVBStorySignRenderMode::Material;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	FVBStorySignState Before;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	FVBStorySignState After;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(EditCondition="RenderMode == EVBStorySignRenderMode::Material"))
	TObjectPtr<UStaticMesh> PanelMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(EditCondition="RenderMode == EVBStorySignRenderMode::Material"))
	FName MaterialSlotName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(EditCondition="RenderMode == EVBStorySignRenderMode::Text"))
	TObjectPtr<UFont> TextFont;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(EditCondition="RenderMode == EVBStorySignRenderMode::Text"))
	TObjectPtr<UMaterialInterface> TextMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(ClampMin="0.01", EditCondition="RenderMode == EVBStorySignRenderMode::Text"))
	float TextWorldSize = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign",
		meta=(EditCondition="RenderMode == EVBStorySignRenderMode::Text"))
	FColor TextColor = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|StorySign")
	bool bPreviewAfter = false;

	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FString ValidateSetup() const;
	void ConfigureDisplay();
	void HideDisplay();
	void RefreshDisplay();
	void ApplyState(bool bAfter);

	UPROPERTY(VisibleAnywhere, Category="VB|StorySign")
	TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY(VisibleAnywhere, Category="VB|StorySign")
	TObjectPtr<UTextRenderComponent> Label;

	FVBStoryStateBinding StoryBinding;
	TOptional<bool> AppliedState;
	TWeakObjectPtr<AGameStateBase> WarnedGameState;
};
